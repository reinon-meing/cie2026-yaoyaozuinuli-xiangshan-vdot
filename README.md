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
docs/     作品文档、操作手册、日志、波形截图
patches/  RTL 补丁（+36/−4）与 NEMU 补丁（+32/−1）
scripts/  vdottest / vdot_sew_test / vdot_edge_test / 性能微基准 / 工具脚本
```

## 环境

Ubuntu 24.04 · Verilator 4.218 · riscv64-linux-gnu-gcc 13.3 · XiangShan 16ae9ddcd · NEMU a48a11f2
