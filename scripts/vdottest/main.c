// 阶段二 2.3：vdot.vv 三项基础功能测试（正数 / 负数 / 边界值）
//
// 为什么不用 difftest：NEMU 到目前为止还不认识 vdot（阶段三才做），带上 --diff 只会
// 报非法指令/不一致。所以本程序是自校验的：期望值由 scripts/vdot_ref.py 独立算出并写进
// cases.h，程序把 RTL 的结果与期望值比较，自己打印 PASS/FAIL。
//
// 判定一个用例通过需要同时满足两条：
//   1. vd 的最低 4 字节（小端 int32）= 期望的点积结果；
//   2. vd 的其余 12 字节（字节 4..15）全为 0 —— 这是"其他通道清零"的语义检查。
#include <am.h>
#include <klib.h>
#include <stdint.h>
#include "cases.h"

// ---------------------------------------------------------------------------
// vdot.vv v3, v1, v2 的机器码
//   opcode = 0x57 (OP-V) | funct3 = 000 (OPIVV) | vm = 1 | funct6 = 0b110111
//   由 scripts/vdot_enc.py --vd 3 --vs1 1 --vs2 2 算出 = 0xde2081d7
// 为什么只能 .4byte：GNU as 不认识 vdot；而 .insn r 对 opcode=0x57 会报
//   "bad value for funct2 field"（已实测），所以用 .4byte 直接落机器码。
// ---------------------------------------------------------------------------
#define VDOT_VV_V3_V1_V2 0xde2081d7

static inline void enable_vector(void) {
  // RVV 规定 mstatus.VS == Off 时任何向量指令都触发非法指令异常
  unsigned long tmp;
  asm volatile("csrrs %0, mstatus, %1" : "=r"(tmp) : "r"(0x600UL) : "memory");
  (void)tmp;
}

// 一次完整的 vdot 调用：vsetivli -> 载入 vs1/vs2 -> vdot.vv -> 存回 16 字节
static inline void vdot_run(int8_t *out, const int8_t *a, const int8_t *b) {
  asm volatile(
      "vsetivli zero, 16, e8, m1, ta, ma\n\t"
      "vle8.v   v1, (%1)\n\t"
      "vle8.v   v2, (%2)\n\t"
      ".4byte  0xde2081d7\n\t"          // vdot.vv v3, v1, v2
      "vse8.v   v3, (%0)\n\t"
      :
      : "r"(out), "r"(a), "r"(b)
      : "memory");
}

static inline int32_t load_i32_le(const int8_t *p) {
  uint32_t v = (uint32_t)(uint8_t)p[0]
             | ((uint32_t)(uint8_t)p[1] << 8)
             | ((uint32_t)(uint8_t)p[2] << 16)
             | ((uint32_t)(uint8_t)p[3] << 24);
  return (int32_t)v;
}

static void dump16(const int8_t *p) {
  for (int i = 0; i < 16; i++) {
    printf("%02x", (unsigned)((uint8_t)p[i]));
  }
  printf("\n");
}

int main(void) {
  enable_vector();
  printf("VDOT TEST BEGIN (VLEN=128 SEW=8 LMUL=1, cases=%d)\n", VDOT_NCASES);

  int npass = 0;
  int8_t out[16];

  for (int i = 0; i < VDOT_NCASES; i++) {
    const vdot_case_t *c = &vdot_cases[i];

    // 先填脏值：如果 RTL 没写某几个字节，这里就会露馅
    for (int k = 0; k < 16; k++) {
      out[k] = (int8_t)0x5a;
    }

    vdot_run(out, c->a, c->b);

    int32_t got = load_i32_le(out);
    int tail_zero = 1;
    for (int k = 4; k < 16; k++) {
      if (out[k] != 0) {
        tail_zero = 0;
      }
    }

    int ok = (got == c->expect) && tail_zero;
    printf("[CASE %d] %s expect=%d got=%d tail_zero=%d %s\n",
           c->group, c->name, (int)c->expect, (int)got, tail_zero, ok ? "PASS" : "FAIL");
    if (!ok) {
      printf("         got_bytes=");
      dump16(out);
    }
    if (ok) {
      npass++;
    }
  }

  printf("VDOT RESULT: %d/%d PASSED\n", npass, VDOT_NCASES);
  return (npass == VDOT_NCASES) ? 0 : 1;
}
