# 第二阶段 2.1｜vdot.vv 设计文档（定稿·实测版）

> 状态：**语义、编码、微架构、数据通路全部定稿，并已实机跑通**（2026-10-28，11 个用例全过）。
> 剩余待办（SEW≠8 非法判定、latency 对比、录屏）见仓库根目录 [待办.md](../待办.md)。
> 相关文件：`scripts/vdot_enc.py`（编码计算器）、`scripts/check_opv_encoding.py`（编码占用复核）、
> `scripts/vdot_ref.py`（参考模型 / 用例生成）、`scripts/vdottest/`（测试程序）、
> `scripts/vdotplay/`（手动试验台）、`scripts/emu-build.sh` + `scripts/emu-status.sh`（构建脚本）。

## 1. 指令语义

### 1.1 官方定义（原文）

赛题页给出的伪代码（`vdot_semantics.jpg`）：

```python
def vdot_vv(vs1, vs2, sew=8):
    # 根据SEW设定，从两个源向量寄存器中各取8个int8数据
    result = 0
    for i in range(8):
        result += sign_extend(vs1[i]) * sign_extend(vs2[i])
    # 结果写入目标向量寄存器vd的第一个元素
    vd[0] = result
    vd[1..VLEN-1] = 0
```

### 1.2 一处必须工程化约定的空白：result 的位宽

伪代码没有规定 `result` 的位宽，而 SEW=8 时 `vd[0]` 只有 8 位，装不下累加和。位宽推导：

| 环节 | 需要位宽 | 依据 |
| --- | --- | --- |
| 单个乘积 | 16 位有符号足够 | 127×127 = 16129；(−128)×(−128) = 16384；(−128)×127 = −16256 |
| 8 项累加和 | **19 位有符号** | 上界 8×16384 = **131072**，下界 8×(−16256) = **−130048**；18 位有符号上界只有 131071，差 1 |
| 写回槽 | 本设计取 **int32** | 19 → 32 位留 13 位余量，8 项累加**不可能溢出**，无需饱和/回绕 |

三种候选读法的取舍：

| 读法 | vd 内容 | 结论 |
| --- | --- | --- |
| int32 累加（**本设计采用**） | 字节 0–3 = int32 结果（小端），字节 4–15 = 0 | 与 VNNI 的 int8→int32 累加惯例一致，无损 |
| 截断 int8 | 字节 0 = result[7:0]，其余 0 | 127 点乘 127 得 0x08，与 1 点乘 1 无法区分，功能上不可用 |
| 饱和 int8 | 饱和后写字节 0 | 伪代码未提饱和，属额外引入的行为 |

**约定原文（写入提交文档的表述）：**

> 官方伪代码未指定 `result` 位宽。本设计将 `result` 约定为 int32 有符号数，写回 vd 的最低
> 4 字节（字节 0–3，小端），其余 96 位（字节 4–15）清零。该约定与 VNNI 的 int8→int32 累加
> 惯例一致，且在 8 项累加下不存在溢出，属无损实现。阶段三的 NEMU 实现按同一约定。

### 1.3 行为约束

| 项目 | 约定 | 说明 |
| --- | --- | --- |
| SEW | 固定 8 | 只解释 8 位元素；SEW≠8 时行为不在本设计范围内（见 §6 风险） |
| 参与字节 | vs1/vs2 的**低 8 字节**（字节 0–7） | 与 vl、LMUL 无关；尾部字节不参与（用例 `bnd-tail-ignored` 专门验证） |
| vm | 只支持 1（unmasked） | funct6 之外 vm=0 的掩码形式保留给未来扩展，编码未占用 |
| vd 写回 | 整个 128 位都写 | 高 96 位清零，不保留旧值（用例会先填 0x5a 脏值再检查） |
| 实现宽度 | VLEN=128 / LMUL=1 | 与赛题给定量一致 |

## 2. 指令编码

### 2.1 选型结论：走 OP-V 的保留 funct6，不走 custom opcode

| 方案 | 是否可行 | 结论 |
| --- | --- | --- |
| OP-V 保留 **funct6** | 可行 | **采用** |
| OP-V 保留 **funct3** | **不存在** | funct3 8 个格子全被占：000 OPIVV / 001 OPFVV / 010 OPMVV / 011 OPIVI / 100 OPIVX / 101 OPFVF / 110 OPMVX / **111 = vsetvli/vsetivli/vsetvl 家族**（`VecDecoder.scala:744-746`） |
| custom opcode（0x0b/0x2b） | 可行但不选 | 香山"这条指令是不是向量指令"由 **opcode** 判定（`RiscvInst.scala:93` `isVecArith = OPCODE5Bit === OP_V`），该信号喂给 `DecodeUnit.scala:1074` → `UopInfoGen.scala:248` 的 `isComplex`。custom opcode 会让向量 uop 拆分/vtype 检查整条链路失效，需逐处打补丁，漏一处即偶发 difftest mismatch。且赛题要求"复用香山现有向量实现"，OP-V 路线改动面是"加一行表项" |

译码未命中时的行为：`DecodeUnit.scala:56` 的 `decodeDefault` 直接给 `SelImm.INVALID_INSTR`，即**未定义的 32 位模式一律判非法指令**。因此选中的编码必须是译码表里没出现过的模式。

### 2.2 位域分配

| 位域 | 值 | 说明 |
| --- | --- | --- |
| [31:26] funct6 | `0b110111` | RVV 1.0 未定义值；本仓库中未被占用（见 §2.4） |
| [25] vm | `1` | unmasked |
| [24:20] vs2 | 源向量 2 | |
| [19:15] vs1 | 源向量 1 | |
| [14:12] funct3 | `000` | OPIVV（向量-向量形式，复用 `OPIVV` 译码模板） |
| [11:7] vd | 目标向量 | |
| [6:0] opcode | `0x57` | OP-V |

机器码公式（`scripts/vdot_enc.py`）：

```
word = 0xDE000000 | (vs2 << 20) | (vs1 << 15) | (vd << 7) | 0x57
```

示例：`vdot.vv v3, v1, v2` → `0xDE2081D7`（文中测试程序用的就是这条）。

### 2.3 汇编侧写法：只能 `.4byte`

* GNU as 不认识 `vdot`；
* `.insn r 0x57, 0, 0x6f, ...` 会被拒：`Error: bad value for funct2 field, value must be 0...3`（已实测）；
* 因此测试程序直接落机器码：

```c
asm volatile("vsetivli zero, 16, e8, m1, ta, ma\n\t"
             "vle8.v   v1, (%1)\n\t"
             "vle8.v   v2, (%2)\n\t"
             ".4byte  0xde2081d7\n\t"    // vdot.vv v3, v1, v2
             "vse8.v   v3, (%0)\n\t" :: "r"(out), "r"(a), "r"(b) : "memory");
```

反汇编显示为 `.word 0xde2081d7`（addr `0x800001b8`，4 字节对齐，夹在两条 `vle8.v` 与
`vse8.v` 之间），这是预期行为。

### 2.4 占用复核（已实测）

```bash
python3 scripts/check_opv_encoding.py --root ~/xs-env/XiangShan --need 000:110111 --vm 1
# 扫描 371 个源文件，命中 OP-V（opcode=0x57）模式 333 条
#   funct3=000: 已用 42/64，空闲 22 个
# [空闲] funct3=000 funct6=110111 vm=1 没有任何表项占用，可以安全选作 vdot.vv
```

funct3=000 下当前空闲的 funct6（22 个）：
`000011 001000 001101 001111 010110 011110 011111 100100 100110 110010 110011
110100 110110 110111 111000 111001 111010 111011 111100 111101 111110 111111`

选 `110111` 而非 `111111` 只是留出最高段给将来更大扩展；**换值成本 = 改 1 行 BitPat + 重编**，
所以把复核脚本随仓库一起交，升级香山后跑一次即可。

## 3. 微架构设计

### 3.1 在香山流水线中的位置

```
取指/译码          派发           向量执行单元                    写回
VecDecoder 表项 → FuType.vialuF → VIAluFix wrapper → vfRegFile
   OPIVV 模板        (exus_2)        ├ VIAluSrcTypeModule  (源/目标类型)
                                    ├ VIntFixpAlu64b × 2  (既有 64 位通道)
                                    ├ Mgu / Mgtu          (掩码)
                                    └ 本次新增 VDot 通道 → 输出 mux
```

实现文件：`src/main/scala/xiangshan/backend/fu/wrapper/VIAluFix.scala`（**注意**：
`fu/vector/VIAluFix.scala` 整个文件已被注释掉，不是活的实现）。

### 3.2 fuOp 编码：不修改 yunsuan 子模块

`fuOpType` 为 9 位 = `{FMT(2), sign(1), opcode(6)}`（`yunsuan/package.scala:8`，`OpTypeWidth = 9`）。
`yunsuan.encoding.Opcode.VialuOpcode` 的 6 位 opcode 当前只用到 0–32、39、46、48–54，**55–63 空闲**。

```
vdot fuOp = {FMT.VVV=00, sign=1, opcode=0b110111} = 9'b001110111 = 0x77
```

这样 **只需在 XiangShan 主仓定义字面量**，不必改 `yunsuan` 子模块（子模块改动在"加密提交/PR"
时是额外负担，且 `~/xs-env/XiangShan` 处于 detached HEAD，改子模块风险更高）。

### 3.3 数据通路（设计稿）

```
vs1[63:0] ─┬─ byte 0..7 ─→ sign_extend(8→16) ─┐
vs2[63:0] ─┴─ byte 0..7 ─→ sign_extend(8→16) ─┴─ 8× mul(16b) → 加法树(32b) → sum[31:0]
                                                                              │
vd[127:0] = {96'b0, sum[31:0]}  ← 其余通道天然清零，满足 "vd[1..VLEN-1]=0"
```

要点：

* 8 个 int8 = 64 位，正好落在**低半部通道**，两条 64 位通道里只有一条真算，另一条输出 0；
* 乘法用 `8×8 → 16` 有符号乘法器 ×8，加法树保持 int32，无饱和；
* 输出挂到现成的输出 mux（`VIAluFix.scala:358` `io.out.bits.res.data := ...`），
  `vxsat` 恒 0（本指令不产生饱和标志）；
* `VIAluSrcTypeModule` 需为 0x77 返回 SEW/VsType（沿用 `FMT.VVV` 分支即可）。

### 3.4 延迟与吞吐

vdot 在 FU 内部只占**一拍**：`VialuCfg` 的 `latency = CertainLatency(1)`（`FuConfig.scala`），
实现上用一级 `RegEnable` 把结果与 `outIsVdot` 对齐到输出级，因此**不改变 `exus_2.VIAluFix` 的
流水级数、也不引入额外停顿**；既有向量指令时序不受影响（回归中 vadd 行为未变可佐证）。

基线构型（阶段三 3.3 要用）：用 RVV 已有指令组合实现同样功能，典型是 `vwmul`（int8→int16 展宽乘）
+ `vwredsum` / `vwmacc` 累加的多指令序列，还要处理展宽、清零与掩码。具体指令序列和实测周期数在
阶段三给出，口径与阶段一 1.3 一致：用 `emu` 汇总的 `instrCnt/cycleCnt`，或波形里
`exus_2.VIAluFix` 的 `io_in_valid → io_out_valid` 周期差。
**不要用 `printf` 计时**：klib 不支持 `%lu`，会打出天文数字（本仓 vaddtest 已踩）。

### 3.5 扩展性

int32 累加器可容纳 `2^31 / 16384 ≈ 13 万` 个 int8 项，因此：

* SEW 提升到 16、VLEN 提升到 512/1024 时累加器不必换宽度；
* funct6 只占 1 个保留值，后续可平移给 `vdot.vx`/`vdot.vi`（funct3=100/011 对应位置亦空闲）。

## 4. 测试计划（2.3.1–2.3.3）

* 程序：`scripts/vdottest/main.c`（自校验，打印 PASS/FAIL）；
* 期望值：`scripts/vdot_ref.py` 独立算出并生成 `cases.h`，不依赖 RTL；
* 不加 `--diff`：NEMU 尚未支持 vdot（阶段三才做），带参考模型只会报不一致。

| 组 | 用例 | vs1 / vs2 前 8 字节 | 期望 int32 |
| --- | --- | --- | --- |
| 1 正数 | pos-ramp | 1..8 / 8..1 | 120 |
| 1 正数 | pos-uniform | 3×8 / 5×8 | 120 |
| 1 正数 | pos-with-max | 127×8 / 1×8 | 1016 |
| 2 负数 | neg-mixed-sign | −1..−8 / 1,−2,3,−4,5,−6,7,−8 | 36 |
| 2 负数 | neg-nonuniform | −128,127,−100,100,−1,1,−2,2 / 1,−1,3,−3,−128,127,−99,99 | −204 |
| 2 负数 | neg-all-neg | −128..−6 / −1×8 | 276 |
| 3 边界 | bnd-max-positive | −128×8 / −128×8 | **131072** |
| 3 边界 | bnd-max-negative | 127×8 / −128×8 | **−130048** |
| 3 边界 | bnd-127-squared | 127×8 / 127×8 | **129032** |
| 3 边界 | bnd-zero | 全 0 / 全 0 | 0 |
| 3 边界 | bnd-tail-ignored | 前 8 字节为 1，后 8 字节为 127 | **8**（证明尾部不参与） |

每个用例同时检查"字节 4–15 全 0"，等价于官方"其他通道清零"的要求。
11 个用例已全部实测通过，原始输出见 §5.2 与 `docs/logs/vdot_11cases.log`。

## 5. 实测证据（2026-10-28，实机）

### 5.1 改造前：这条编码是非法指令

| 程序 | 结果 |
| --- | --- |
| `vdottest`（含 `.4byte 0xde2081d7`） | 打印 banner 后**停住**，无任何 CASE 输出 → vdot 被判非法指令、陷入陷阱 |
| `vdotprobe`（同程序，仅把该行换成 `vadd.vv v3, v1, v2`） | 正常跑完 11 个用例并逐条打印（结果为逐字节加法，故 FAIL），证明 vsetivli/vle8/vse8/回读/尾字节检查/printf 全部工作 |

两者只差那一条指令，说明"停住"是 vdot 引起的，与测试程序无关。

### 5.2 改造后：11 个用例全过

```bash
cd ~/xs-env/XiangShan && source ~/xs-env/env.sh
./build/emu -i $AM_HOME/apps/vdottest/build/vdottest-riscv64-xs.bin --no-diff 2>&1 | grep -av PERF
```

| 组 | 用例 | 期望 | 实测 | 结果 |
| --- | --- | --- | --- | --- |
| 1 正数 | pos-ramp | 120 | 120 | PASS |
| 1 正数 | pos-uniform | 120 | 120 | PASS |
| 1 正数 | pos-with-max | 1016 | 1016 | PASS |
| 2 负数 | neg-mixed-sign | 36 | 36 | PASS |
| 2 负数 | neg-nonuniform | −204 | −204 | PASS |
| 2 负数 | neg-all-neg | 276 | 276 | PASS |
| 3 边界 | bnd-max-positive | 131072 | 131072 | PASS |
| 3 边界 | bnd-max-negative | −130048 | −130048 | PASS |
| 3 边界 | bnd-127-squared | 129032 | 129032 | PASS |
| 3 边界 | bnd-zero | 0 | 0 | PASS |
| 3 边界 | bnd-tail-ignored | 8 | 8 | PASS |

全部 11 条 `tail_zero=1`（字节 4–15 全 0），末行 `VDOT RESULT: 11/11 PASSED`、`Exit with code = 0`。
原始日志：`docs/logs/vdot_11cases.log`。

### 5.3 回归：既有功能未受影响

| 程序 | 结果 |
| --- | --- |
| `vaddtest`（带 `--diff` 参考模型） | 输出 16 个 17，`Exit with code = 0`，无 mismatch |
| `hello`（带 `--diff` 参考模型） | 输出阶段一那句字符串（含 IP），`Exit with code = 0` |

日志：`docs/logs/regress_vadd.log`、`docs/logs/regress_hello.log`。

### 5.4 构建记录（实测耗时与踩坑）

```bash
cd ~/xs-env/XiangShan && source ~/xs-env/env.sh
make emu CONFIG=MinimalConfig EMU_TRACE=1 VEXTRA_FLAGS=-Wno-UNOPTFLAT -j8
```

* **耗时**：mill（Chisel→Verilog）约 9 分钟 + verilator/C++ 约 55 分钟 ≈ 65 分钟；增量构建只重编变化的 429/667 个目标，约 33 分钟；
* **产物**：`build/emu` 73 MB，带 `--dump-wave`（166 个 `*Trace*.o`）与 difftest；
* **坑 1**：后台 shell 未 `source env.sh` → mill 报 `NoSuchElementException: NOOP_HOME`；
* **坑 2**：不加 `-Wno-UNOPTFLAT` → 香山门控时钟 SRAM 的 42 条固有告警被 Verilator 当致命错误（`%Error: Exiting due to 42 warning(s)`）；
* **坑 3**：换 verilator 参数后若 `build/emu-compile/VSimTop.mk` 已存在，make 会跳过生成直接编半成品，需先 `rm -f`；
* **坑 4**：`nohup make ... &` 会丢退出码，用 `scripts/emu-build.sh`（退出码落盘 + flock 防并发）。

## 6. 风险与待办

| 项目 | 说明 | 处置 |
| --- | --- | --- |
| SEW≠8 的行为 | 伪代码未定义；香山 `villIllegal` 依赖 vtype | **已决策（2026-10-28）：补非法判定，与阶段三第一次 NEMU 改动的重编合并**。判定条件 `vdot && vsew != e8`，测试用自带 trap handler 的用例验证，详见 [待办.md](../待办.md) §2.1 |
| vm=0（掩码） | 本设计只做 unmasked | 编码未占用 vm=0，留给后续 |
| funct6 占用漂移 | 上游升级/新扩展可能占用 110111 | 随仓库交付 `check_opv_encoding.py`，升级后跑一次 |
| 重编成本 | 实测 mill 8:56 + verilator 55:30 ≈ 65 min | RTL 一次改到位；改前先在参考模型上核对第 4 节全部期望值 |
| VLEN≠128 | 本设计按 VLEN=128 实现 | 文档标注；扩展性见 §3.5 |