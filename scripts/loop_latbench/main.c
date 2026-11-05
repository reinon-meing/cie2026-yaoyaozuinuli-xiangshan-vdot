// 阶段三 3.3：空循环开销基准（与 vdot/base 微基准同样的循环结构，无向量指令）
// 用途：从总周期里扣掉标量循环开销（addi + bnez），得到纯向量部分的代价。
#include <am.h>
#include <klib.h>
#include <stdint.h>

#define REP 200

int main(void) {
  unsigned long c0, c1, i0, i1;
  asm volatile("rdcycle %0" : "=r"(c0) : : "memory");
  asm volatile("rdinstret %0" : "=r"(i0) : : "memory");

  asm volatile(
      "mv   t0, %0\n\t"
      "1:\n\t"
      "addi t0, t0, -1\n\t"
      "bnez t0, 1b\n\t"
      :
      : "r"((long)REP)
      : "t0", "memory");

  asm volatile("rdcycle %0" : "=r"(c1) : : "memory");
  asm volatile("rdinstret %0" : "=r"(i1) : : "memory");
  printf("LOOP BENCH DONE cycles=%lu instret=%lu\n", c1 - c0, i1 - i0);
  return 0;
}
