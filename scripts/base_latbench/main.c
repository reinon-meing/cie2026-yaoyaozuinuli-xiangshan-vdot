// 阶段三 3.3：基线：vwmul.vv + vwredsum.vs 组合实现同一点积
//
// 只做计算、不做内存访问（源数据在循环外加载），循环 {N} 次，
// 用 emu 输出的 instrCnt / cycleCnt 比较两种实现的单次点积代价。
// 结果必须与 vdottest 的 pos-ramp 用例一致（120），否则说明基线写错了。
#include <am.h>
#include <klib.h>
#include <stdint.h>

#define REP 200
#define VDOT_VV_V3_V1_V2 0xde2081d7

static inline void enable_vector(void) {
  unsigned long tmp;
  asm volatile("csrrs %0, mstatus, %1" : "=r"(tmp) : "r"(0x600UL) : "memory");
  (void)tmp;
}

static inline int32_t load_i32_le(const int8_t *p) {
  uint32_t v = (uint32_t)(uint8_t)p[0]
             | ((uint32_t)(uint8_t)p[1] << 8)
             | ((uint32_t)(uint8_t)p[2] << 16)
             | ((uint32_t)(uint8_t)p[3] << 24);
  return (int32_t)v;
}

int main(void) {
  enable_vector();
  static int8_t a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0};
  static int8_t b[16] = {8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  static int8_t out[16];

  printf("BASE_LATBENCH BENCH BEGIN (REP=%d)\n", REP);

  unsigned long c0, c1, i0, i1;
  asm volatile("rdcycle %0" : "=r"(c0) : : "memory");
  asm volatile("rdinstret %0" : "=r"(i0) : : "memory");
  asm volatile(
      "vsetivli zero, 8, e8, m1, ta, ma\n\t"
      "vle8.v   v1, (%1)\n\t"
      "vle8.v   v2, (%2)\n\t"
      "vmv.v.i  v0, 0\n\t"                // vwredsum 的初始值 vs1
      "mv       t0, %0\n\t"
      "1:\n\t"
      "vwmul.vv    v4, v1, v2\n\t"        // e8 x e8 -> e16
      "vsetivli zero, 8, e16, m1, ta, ma\n\t"
      "vwredsum.vs v5, v4, v0\n\t"        // e16 -> e32 归约
      "vsetivli zero, 8, e8, m1, ta, ma\n\t"  // 还原 vtype 供下一轮 vwmul
      "addi     t0, t0, -1\n\t"
      "bnez     t0, 1b\n\t"
      "vsetivli zero, 16, e8, m1, ta, ma\n\t"
      "vse8.v   v5, (%3)\n\t"
      :
      : "r"((long)REP), "r"(a), "r"(b), "r"(out)
      : "t0", "memory");
  asm volatile("rdcycle %0" : "=r"(c1) : : "memory");
  asm volatile("rdinstret %0" : "=r"(i1) : : "memory");
  printf("BASE_LATBENCH BENCH DONE result=%d cycles=%lu instret=%lu\n",
         (int)load_i32_le(out), c1 - c0, i1 - i0);
  return 0;
}
