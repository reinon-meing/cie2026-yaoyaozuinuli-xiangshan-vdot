#!/usr/bin/env python3
# 从迷你 VCD 里按时间窗口切片，生成只含 [t0, t1] 的 VCD（打开即自动 fit 到该范围）
# 用法: python3 vcd_slice.py <in.vcd> <t0> <t1> <out.vcd>
import sys

src, t0, t1, dst = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
lines = open(src).read().splitlines()
end = next(i for i, l in enumerate(lines) if l.strip() == "$enddefinitions $end")
header = lines[:end + 1]

body = []
state = {}
started = False
t = None
for l in lines[end + 1:]:
    s = l.strip()
    if not s:
        continue
    if s.startswith("#"):
        t = int(s[1:])
        if t < t0:
            continue
        if t > t1:
            break
        if not started:
            body.append("#%d" % t0)
            for line in state.values():
                body.append(line)
            started = True
            if t == t0:
                continue
        body.append("#%d" % t)
        continue
    if s[0] in "bB":
        ident = s.rsplit(" ", 1)[-1]
        line = s
    else:
        ident = s[1:]
        line = s
    if not started:
        state[ident] = line
    else:
        body.append(line)

open(dst, "w").write("\n".join(header + body) + "\n")
print("sliced", len(body), "body lines ->", dst)
