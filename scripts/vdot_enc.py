#!/usr/bin/env python3
"""vdot.vv 指令编码计算器。

本设计选用的编码（详见 docs/阶段二-vdot设计.md）：

    opcode  = 0x57        OP-V（与所有向量算术指令同一条主 opcode）
    funct3  = 0b000       OPIVV（向量-向量操作数形式）
    vm      = 1           unmasked（v0 不参与；vm=0 的掩码形式保留给未来扩展）
    funct6  = 0b110111    RVV 1.0 未定义值；已在香山译码表中复核为空闲

机器码布局（bits 31..0）：

    31        26 25 24    20 19  15 14  12 11  7 6        0
    +------------+--+--------+------+------+-----+----------+
    |  funct6    |vm|  vs2   | vs1  |funct3| vd  |  opcode  |
    |  110111    | 1|        |      | 000  |     | 1010111  |
    +------------+--+--------+------+------+-----+----------+

用法：
    python3 vdot_enc.py                        # 打印示例编码表
    python3 vdot_enc.py --vd 3 --vs1 1 --vs2 2 # 打印指定寄存器的编码
    python3 vdot_enc.py --decode 0xde2081d7    # 反向解码
"""

import argparse

FUNCT6 = 0b110111
FUNCT3 = 0b000
OPCODE = 0x57
VM = 1


def encode(vd: int, vs1: int, vs2: int, vm: int = VM) -> int:
    """按位域拼出 vdot.vv 的机器码。"""
    for name, val in (("vd", vd), ("vs1", vs1), ("vs2", vs2)):
        if not 0 <= val < 32:
            raise ValueError(f"{name} 必须在 0..31，收到 {val}")
    if vm not in (0, 1):
        raise ValueError("vm 只能是 0 或 1")
    return (
        (FUNCT6 << 26)
        | (vm << 25)
        | (vs2 << 20)
        | (vs1 << 15)
        | (FUNCT3 << 12)
        | (vd << 7)
        | OPCODE
    )


def decode(word: int) -> dict:
    """反向解码，便于核对汇编里手写的 .4byte 常量。"""
    return {
        "funct6": (word >> 26) & 0x3F,
        "vm": (word >> 25) & 1,
        "vs2": (word >> 20) & 0x1F,
        "vs1": (word >> 15) & 0x1F,
        "funct3": (word >> 12) & 0x7,
        "vd": (word >> 7) & 0x1F,
        "opcode": word & 0x7F,
        "is_vdot": ((word >> 26) & 0x3F) == FUNCT6
        and ((word >> 12) & 0x7) == FUNCT3
        and (word & 0x7F) == OPCODE,
    }


def main() -> None:
    ap = argparse.ArgumentParser(description="vdot.vv 编码计算器")
    ap.add_argument("--vd", type=int)
    ap.add_argument("--vs1", type=int)
    ap.add_argument("--vs2", type=int)
    ap.add_argument("--decode", type=lambda s: int(s, 0))
    args = ap.parse_args()

    if args.decode is not None:
        info = decode(args.decode)
        print(f"0x{args.decode:08x} -> " + " ".join(f"{k}={v}" for k, v in info.items()))
        return

    if args.vd is not None:
        word = encode(args.vd, args.vs1, args.vs2)
        print(f"vdot.vv v{args.vd}, v{args.vs1}, v{args.vs2}  =  0x{word:08x}")
        return

    print("示例编码（vd=3, vs1=1, vs2=2 就是测试程序里用的那条）：")
    for vd, vs1, vs2 in ((3, 1, 2), (0, 1, 2), (1, 2, 3), (15, 0, 1)):
        print(f"  vdot.vv v{vd}, v{vs1}, v{vs2}  =  0x{encode(vd, vs1, vs2):08x}")


if __name__ == "__main__":
    main()
