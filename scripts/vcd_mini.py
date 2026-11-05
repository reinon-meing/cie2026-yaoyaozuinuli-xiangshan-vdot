#!/usr/bin/env python3
# 从大 VCD 里抽出「指定模块的几个关键信号」，生成一个几十 KB 的迷你 VCD
# 用法: python3 vcd_mini.py <in.vcd> <scope子串> <out.vcd>
import sys

src, key, dst = sys.argv[1], sys.argv[2], sys.argv[3]

WANT = [
    "clock",
    "io_in_valid",
    "io_out_valid",
    # 不同版本的 CtrlPipe 下标不同，两个都收
    "io_in_bits_ctrlPipe_0_fuOpType",
    "io_in_bits_ctrlPipe_1_fuOpType",
    "io_in_bits_ctrlPipe_0_vpu_vsew",
    "io_in_bits_ctrlPipe_1_vpu_vsew",
    "io_in_bits_ctrlPipe_0_pdest",
    "io_in_bits_ctrlPipe_1_pdest",
    "io_out_bits_res_data",
]

path = []
keep = {}      # id -> (width, name)
scope_of = {}  # id -> scope path

with open(src, "rb") as f:
    for raw in f:
        line = raw.decode("utf-8", "ignore").strip()
        if line.startswith("$scope"):
            p = line.split()
            if len(p) >= 3:
                path.append(p[2])
        elif line.startswith("$upscope"):
            if path:
                path.pop()
        elif line.startswith("$var"):
            p = line.split("$end")[0].split()
            if len(p) >= 5:
                width, ident, name = p[2], p[3], p[4]
                if key in ".".join(path) and name in WANT:
                    keep[ident] = (width, name)
                    scope_of[ident] = ".".join(path)
        elif line.startswith("$enddefinitions"):
            break

    if not keep:
        sys.exit("没有匹配到信号")

    scopes = sorted(set(scope_of.values()))
    out = []
    out.append("$date mini $end")
    out.append("$version extracted by vcd_mini.py $end")
    out.append("$timescale 1ps $end")
    # 每个来源 scope 单独建一个层次
    for s in scopes:
        parts = s.split(".")
        for comp in parts:
            out.append("$scope module %s $end" % comp)
        for ident, (w, n) in keep.items():
            if scope_of[ident] == s:
                out.append("$var wire %s %s %s $end" % (w, ident, n))
        for _ in parts:
            out.append("$upscope $end")
    out.append("$enddefinitions $end")

    body = []
    t = 0
    pending = None
    for raw in f:
        line = raw.decode("utf-8", "ignore").strip()
        if not line:
            continue
        if line[0] == "#":
            nt = int(line[1:])
            if t != nt:
                if pending is None:
                    pending = "#%d" % nt
                t = nt
            continue
        if line[0] in "bB":
            body_line = line
            ident = line.rsplit(" ", 1)[-1]
        elif line[0] in "01xXzZ":
            body_line = line
            ident = line[1:]
        else:
            continue
        if ident in keep:
            if pending:
                body.append(pending)
                pending = None
            body.append(body_line)

    with open(dst, "w") as g:
        g.write("\n".join(out + body) + "\n")

print("kept signals:", len(keep), "scopes:", len(scopes), "body lines:", len(body))
