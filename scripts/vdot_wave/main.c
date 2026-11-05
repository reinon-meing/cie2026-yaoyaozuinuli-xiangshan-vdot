// 录屏③波形用：只执行一条 vdot，并打印它前后的绝对周期（rdcycle c0/c1）
// 用途：用 c0/c1 确定 emu --dump-wave 的 -b/-e 窗口，再在 Surfer 里看 VIAluFix 的握手信号。
#include <am.h>
#include <klib.h>
#include <stdint.h>

static inline void enable_vector(void) {
  unsigned long tmp;
  asm volatile("csrrs %0, mstatus, %1" : "=r"(tmp) : "r"(0x600UL) : "memory");
  (void)tmp;
}

static inline int32_t load_i32_le(const int8_t *p) {
  uint32_t v = (uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8)
             | ((uint32_t)(uint8_t)p[2] << 16) | ((uint32_t)(uint8_t)p[3] << 24);
  return (int32_t)v;
}

int main(void) {
  enable_vector();
  static int8_t a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0};
  static int8_t b[16] = {8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  static int8_t out[16];
  unsigned long c0, c1;

  asm volatile("vsetivli zero, 8, e8, m1, ta, ma\n\t"
               "vle8.v   v1, (%0)\n\t"
               "vle8.v   v2, (%1)\n\t"
               :: "r"(a), "r"(b) : "memory");
  asm volatile("rdcycle %0" : "=r"(c0) : : "memory");
  asm volatile(".4byte 0xde2081d7" ::: "memory");   // vdot.vv v3, v1, v2
  asm volatile("rdcycle %0" : "=r"(c1) : : "memory");
  asm volatile("vsetivli zero, 16, e8, m1, ta, ma\n\t"
               "vse8.v   v3, (%0)\n\t" :: "r"(out) : "memory");

  printf("WAVE c0=%lu c1=%lu result=%d\n", c0, c1, (int)load_i32_le(out));
  return 0;
}
