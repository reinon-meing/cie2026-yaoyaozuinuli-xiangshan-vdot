// vdot 边界审查测试：LMUL=2 / vd 与 vs1 重叠 / vl=0 / vstart>0
// 目的：确认 RTL 与 NEMU 在这些官方用例没覆盖的边界上是否一致（跑 --diff 看是否 mismatch）。
#include <am.h>
#include <klib.h>
#include <stdint.h>

#define VDOT_V3_V1_V2 0xde2081d7
#define VDOT_V1_V1_V2 0xde2080d7   // vd=v1, vs1=v1, vs2=v2

static inline void enable_vector(void) {
  unsigned long tmp;
  asm volatile("csrrs %0, mstatus, %1" : "=r"(tmp) : "r"(0x600UL) : "memory");
  (void)tmp;
}
static inline int32_t le32(const int8_t *p) {
  return (int32_t)((uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8) |
                   ((uint32_t)(uint8_t)p[2] << 16) | ((uint32_t)(uint8_t)p[3] << 24));
}
static void dump(const char *tag, const int8_t *p, int n) {
  printf("%s:", tag);
  for (int i = 0; i < n; i++) printf(" %02x", (unsigned)(uint8_t)p[i]);
  printf("\n");
}

static int8_t a[16] = {1,2,3,4,5,6,7,8,127,127,127,127,127,127,127,127};
static int8_t b[16] = {8,7,6,5,4,3,2,1,127,127,127,127,127,127,127,127};
static int8_t out[32];

int main(void) {
  enable_vector();
  printf("VDOT EDGE BEGIN\n");

  // case 1: LMUL=2（vd 组 = v3,v4），检查低 128 位结果与 v4 是否被写
  for (int i = 0; i < 32; i++) out[i] = 0x5a;
  asm volatile("vsetivli zero, 16, e8, m2, ta, ma\n\t"
               "vle8.v v4, (%0)\n\t vle8.v v6, (%1)\n\t"
               ".4byte 0xde620157\n\t"
               "vse8.v v2, (%2)\n\t" :: "r"(a), "r"(b), "r"(out) : "memory");
  printf("[C1 LMUL=2] got=%d tail0=", (int)le32(out));
  int t0 = 1; for (int i = 4; i < 16; i++) if (out[i]) t0 = 0;
  printf("%d\n", t0); dump("C1 v3", out, 16); dump("C1 v4", out + 16, 16);

  // case 2: vd 与 vs1 重叠（vdot v1, v1, v2）
  for (int i = 0; i < 16; i++) out[i] = 0x5a;
  asm volatile("vsetivli zero, 8, e8, m1, ta, ma\n\t"
               "vle8.v v1, (%0)\n\t vle8.v v2, (%1)\n\t"
               ".4byte 0xde2080d7\n\t"
               "vse8.v v1, (%2)\n\t" :: "r"(a), "r"(b), "r"(out) : "memory");
  printf("[C2 vd=vs1] got=%d expect=120\n", (int)le32(out));

  // case 3: vl=0
  asm volatile("vsetivli zero, 16, e8, m1, ta, ma\n\t"
               "li t0, 0x5a\n\t vmv.v.x v3, t0\n\t" ::: "t0", "memory");
  asm volatile("vsetivli zero, 0, e8, m1, ta, ma\n\t"
               "vle8.v v1, (%0)\n\t vle8.v v2, (%1)\n\t"
               ".4byte 0xde2081d7\n\t"
               "vsetivli zero, 16, e8, m1, ta, ma\n\t"
               "vse8.v v3, (%2)\n\t" :: "r"(a), "r"(b), "r"(out) : "memory");
  printf("[C3 vl=0] vd0=%d\n", (int)le32(out)); dump("C3 v3", out, 16);

  // case 4: vstart=4（vl=8）
  asm volatile("vsetivli zero, 16, e8, m1, ta, ma\n\t"
               "li t0, 0x5a\n\t vmv.v.x v3, t0\n\t" ::: "t0", "memory");
  asm volatile("li t0, 4\n\t csrrw zero, 0x008, t0\n\t" ::: "t0", "memory");
  asm volatile("vsetivli zero, 8, e8, m1, ta, ma\n\t"
               "vle8.v v1, (%0)\n\t vle8.v v2, (%1)\n\t"
               ".4byte 0xde2081d7\n\t"
               "vsetivli zero, 16, e8, m1, ta, ma\n\t"
               "vse8.v v3, (%2)\n\t" :: "r"(a), "r"(b), "r"(out) : "memory");
  printf("[C4 vstart=4] got=%d\n", (int)le32(out)); dump("C4 v3", out, 16);

  printf("VDOT EDGE DONE\n");
  return 0;
}
