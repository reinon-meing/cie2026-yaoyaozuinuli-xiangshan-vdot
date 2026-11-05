#!/usr/bin/env python3
"""复核 vdot.vv 的编码在香山译码表里是否空闲。

背景：香山的译码是「32 位 BitPat 全匹配 + 查表」，没命中的模式落到 decodeDefault，
被当成非法指令（DecodeUnit.scala:56 的 SelImm.INVALID_INSTR）。所以只要我们的 32 位
模式没有撞上任何已有表项，这条指令就能进译码；反过来一旦撞上，会把别人的指令悄悄改语义
（查表是先匹配先赢，不会报错）。

扫描范围（与真实译码路径一致）：
  * rocket-chip/src/main/scala/rocket/Instructions.scala  提供所有标准 RVV BitPat
  * src/main/scala/ 下全部 .scala                         香山自己补充的向量扩展表

用法：
    python3 check_opv_encoding.py --root ~/xs-env/XiangShan
    python3 check_opv_encoding.py --root ~/xs-env/XiangShan --need 000:110111 --vm 1

退出码：0 = 候选编码空闲；1 = 与已有表项冲突；2 = 找不到源文件
"""

import argparse
import glob
import os
import re
import sys

OP_V = "1010111"
PAT_RE = re.compile(r'BitPat\("b([01?]{32})"\)')


def pat_match(pat: str, value: str) -> bool:
    """BitPat 风格的匹配：'?' 匹配任意位。"""
    if len(pat) != len(value):
        return False
    return all(p == "?" or p == v for p, v in zip(pat, value))


def bits(value: int, width: int) -> str:
    return format(value, "0{}b".format(width))


def collect_files(root: str):
    files = []
    files += glob.glob(os.path.join(root, "rocket-chip/src/main/scala/rocket/Instructions.scala"))
    files += glob.glob(os.path.join(root, "src/main/scala/**/*.scala"), recursive=True)
    return [f for f in files if os.path.isfile(f)]


def main() -> int:
    ap = argparse.ArgumentParser(description="检查 OP-V 编码占用情况")
    ap.add_argument("--root", default=os.path.expanduser("~/xs-env/XiangShan"))
    ap.add_argument("--need", help="待检查的编码，格式 funct3:funct6，例如 000:110111")
    ap.add_argument("--vm", type=int, default=1, help="待检查的 vm 位（默认 1 = unmasked）")
    ap.add_argument("--exclude", help="跳过包含该子串的源码行，用于排除自己刚加的补丁（例如 VDOT_VV）")
    args = ap.parse_args()

    files = collect_files(args.root)
    if not files:
        print("找不到香山源码，检查 --root 是否正确", file=sys.stderr)
        return 2

    entries = []  # (funct3pat, funct6pat, vmpat, 来源文件)
    for path in files:
        with open(path, encoding="utf-8", errors="ignore") as fp:
            for line in fp:
                if args.exclude and args.exclude in line:
                    continue
                for m in PAT_RE.finditer(line):
                    p = m.group(1)
                    if p[25:32] != OP_V:
                        continue
                    entries.append((p[17:20], p[0:6], p[6], os.path.basename(path)))

    print(f"扫描 {len(files)} 个源文件，命中 OP-V（opcode=0x57）模式 {len(entries)} 条")

    # 按 funct3 汇总：穷举 64 个 funct6，看哪个没被任何模式覆盖
    f3_values = sorted({e[0].replace("?", "*") for e in entries})
    for f3 in f3_values:
        if "*" in f3:
            print(f"  funct3 含通配符的模式：{f3}（{sum(1 for e in entries if e[0].replace('?', '*') == f3)} 条），跳过得空统计")
            continue
        free = []
        for i in range(64):
            f6 = bits(i, 6)
            if not any(pat_match(e[1], f6) and e[0] == f3 for e in entries if e[0] == f3):
                free.append(f6)
        print(f"  funct3={f3}: 已用 {64 - len(free)}/64，空闲 {len(free)} 个")
        if free and len(free) <= 24:
            print("    空闲 funct6: " + " ".join(free))

    if args.need:
        try:
            f3, f6 = args.need.split(":")
            f3 = bits(int(f3, 2), 3)
            f6 = bits(int(f6, 2), 6)
        except ValueError:
            print("--need 格式应为 funct3:funct6 的二进制串，例如 000:110111", file=sys.stderr)
            return 2
        vm = str(args.vm)
        hits = [e for e in entries if pat_match(e[0], f3) and pat_match(e[1], f6) and pat_match(e[2], vm)]
        if hits:
            print(f"\n[冲突] funct3={f3} funct6={f6} vm={vm} 已被 {len(hits)} 条表项占用：")
            for e in hits[:10]:
                print(f"    funct3={e[0]} funct6={e[1]} vm={e[2]} <- {e[3]}")
            return 1
        print(f"\n[空闲] funct3={f3} funct6={f6} vm={vm} 没有任何表项占用，可以安全选作 vdot.vv")
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
