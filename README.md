# 香山 vdot.vv 自定义指令（2026 CIE · race id=8）

在香山昆明湖 V2 的向量执行单元（VIAluFix）中新增自定义指令 **vdot.vv**：
8 个 int8 符号扩展乘加、结果以 int32 写回；为 NEMU 补齐同语义实现并完成逐指令协同仿真；
与"已有向量指令组合"基线相比，每 8 元素点积 **3.04 vs 5.60 周期（1.84×）**。

## 结果

| 项 | 结果 |
|---|---|
| 功能用例 | 11/11 PASS |
| SEW 用例 | 5/5 PASS |
| 边界用例 | 4/4（vl=0 为未定义行为） |
| 协同仿真 | 零 mismatch |
| 性能 | 1.84×（每点积 3.04 vs 5.60 周期） |

## 从这里开始

- 想复现：看 [docs/操作手册.md](docs/操作手册.md)（从 clone 到跑通测试的完整步骤）
- 想了解设计与数据：看 [docs/作品文档.md](docs/作品文档.md)

## 目录

```text
docs/      作品文档、操作手册、测试报告、协同仿真记录、三阶段文档、日志、图
patches/   RTL 补丁（+36/−4）与 NEMU 补丁（+32/−1）
rtl/       修改后的完整 VecDecoder.scala / VIAluFix.scala
scripts/   vdottest / vdot_sew_test / vdot_edge_test / 性能微基准 / 工具脚本
```

## 环境

Ubuntu 24.04 · Verilator 4.218 · riscv64-linux-gnu-gcc 13.3 · XiangShan 16ae9ddcd · NEMU a48a11f2

## 官方提交要求对照

| 官方要求 | 本仓库位置 |
|---|---|
| 1.1 环境部署验证 | [docs/logs/stage1/stage1_deploy.clean.log](docs/logs/stage1/stage1_deploy.clean.log)（含 hello 输出与标识） |
| 1.2 操作日志 | [docs/logs/stage1/](docs/logs/stage1/)（原始日志 + 清洗版 + 环境指纹） |
| 1.3 向量加法执行过程文档 | [docs/阶段一-向量加法执行过程.md](docs/阶段一-向量加法执行过程.md) + [fig1](docs/figures/fig1-vadd-vialufix.png) |
| 2.1 vdot.vv 设计文档 | [docs/阶段二-vdot设计.md](docs/阶段二-vdot设计.md) |
| 2.2 RTL 实现 | [patches/01-xiangshan-vdot.patch](patches/01-xiangshan-vdot.patch) + [rtl/](rtl/) |
| 2.3.1–2.3.3 三类测试 | [docs/测试报告.md](docs/测试报告.md) + [scripts/vdottest/](scripts/vdottest/) |
| 3.1.1 NEMU 指令扩展 | [patches/02-nemu-vdot.patch](patches/02-nemu-vdot.patch) |
| 3.1.2 协同仿真验证 | [docs/协同仿真记录.md](docs/协同仿真记录.md) + [docs/logs/stage3/](docs/logs/stage3/) |
| 3.2 基线功能实现 | [scripts/base_latbench/](scripts/base_latbench/) |
| 3.3 延迟加速分析报告 | [docs/阶段三-延迟加速分析.md](docs/阶段三-延迟加速分析.md) |
| 总览 | [docs/作品文档.md](docs/作品文档.md) |
| 复现步骤 | [docs/操作手册.md](docs/操作手册.md) |
