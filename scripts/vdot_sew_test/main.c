// 阶段三：vdot.vv 的 SEW 非法判定测试（配合 RTL + NEMU 的 SEW≠8 illegal）
//
// 覆盖 4 件事：
//   1) SEW=8：合法执行，int32 结果正确、其余字节清零、不触发陷阱；
//   2) SEW=16 / SEW=32：与设计约定一致，vdot 触发非法指令异常（mcause=2）；
//   3) 被陷阱打断的 vdot 不得写 vd：vd 内容保持陷阱前的 0x5a；
//   4) 陷阱返回后再切回 SEW=8，vdot 仍能正确执行（状态没跑飞）。
//
// 与 vdottest 的关键差别：本程序自带 M 模式 trap handler（vdottest 不需要，它不测非法指令）：
//   mtvec -> 判 mcause==2 -> g_trap_flag=1 -> mepc+=4 -> mret
// 只能跑在带 SEW 判定的 emu 上（阶段二的 emu 里 SEW=16/32 不会陷阱，本程序会判 FAIL）。
//
// 汇编注意：GNU as 不认识 vdot，统一用 .4byte 落机器码（0xde2081d7 = vdot.vv v3, v1, v2）。
#include <am.h>
#include <klib.h>
#include <stdint.h>

#define VDOT_VV_V3_V1_V2 0xde2081d7

// 陷阱记录（trap handler 里用 sw 写，用 unsigned long 保证符号可见性）
volatile unsigned long g_trap_flag;    // 1 = 捕获到非法指令异常
volatile unsigned long g_trap_mcause;  // 非预期的 mcause（诊断用）
extern void vdot_trap_entry(void);

// M 模式 trap handler：只处理 illegal instruction（mcause=2）。
// 只破坏 t0..t3（x5..x7/x28），调用处的 asm 已把这些寄存器列进 clobber 列表。
__asm__(
    ".align 2\n"
    ".global vdot_trap_entry\n"
    "vdot_trap_entry:\n"
    "  csrr t0, mcause\n"
    "  li   t1, 2\n"
    "  bne  t0, t1, 1f\n"
    "  la   t2, g_trap_flag\n"
    "  li   t3, 1\n"
    "  sw   t3, 0(t2)\n"
    "  csrr t0, mepc\n"
    "  addi t0, t0, 4\n"
    "  csrw mepc, t0\n"
    "  mret\n"
    "1:\n"
    "  la   t2, g_trap_mcause\n"
    "  sw   t0, 0(t2)\n"
    "  csrr t0, mepc\n"
    "  addi t0, t0, 4\n"
    "  csrw mepc, t0\n"
    "  mret\n");

static inline void enable_vector(void) {
  // RVV 规定 mstatus.VS == Off 时任何向量指令都触发非法指令异常
  unsigned long tmp;
  asm volatile("csrrs %0, mstatus, %1" : "=r"(tmp) : "r"(0x600UL) : "memory");
  (void)tmp;
}

static inline void trap_setup(void) {
  unsigned long tvec = (unsigned long)vdot_trap_entry;
  // 直接模式：mtvec[1:0] = 00，vdot_trap_entry 用 .align 2 保证 4 字节对齐
  asm volatile("csrw mtvec, %0" :: "r"(tvec) : "memory");
}

// SEW=8 合法路径：vsetivli e8 -> 载入 -> vdot.vv v3,v1,v2 -> 存回 16 字节
static inline void vdot_e8(int8_t *out, const int8_t *a, const int8_t *b) {
  asm volatile(
      "vsetivli zero, 16, e8, m1, ta, ma\n\t"
      "vle8.v   v1, (%1)\n\t"
      "vle8.v   v2, (%2)\n\t"
      ".4byte  0xde2081d7\n\t"
      "vse8.v   v3, (%0)\n\t"
      :
      : "r"(out), "r"(a), "r"(b)
      : "memory", "x5", "x6", "x7", "x28");
}

// SEW=16 非法路径：只执行 vsetivli + vdot，不碰内存；期望在 .4byte 处陷入陷阱
static inline void vdot_e16(void) {
  asm volatile(
      "vsetivli zero, 8, e16, m1, ta, ma\n\t"
      ".4byte  0xde2081d7\n\t"
      "nop\n\t"
      :
      :
      : "memory", "x5", "x6", "x7", "x28");
}

// SEW=32 非法路径
static inline void vdot_e32(void) {
  asm volatile(
      "vsetivli zero, 4, e32, m1, ta, ma\n\t"
      ".4byte  0xde2081d7\n\t"
      "nop\n\t"
      :
      :
      : "memory", "x5", "x6", "x7", "x28");
}

// 用 0x5a 填满 v3（16 个字节），用于检查"被陷阱打断的 vdot 不能写 vd"
// 注意 vmv.v.i 的立即数是 5 位有符号（-16..15），0x5a 只能用 vmv.v.x 传
static inline void fill_v3_5a(void) {
  unsigned long val = 0x5a;
  asm volatile(
      "vsetivli zero, 16, e8, m1, ta, ma\n\t"
      "vmv.v.x  v3, %0\n\t"
      :
      : "r"(val)
      : "memory");
}

static inline void store_v3(int8_t *out) {
  asm volatile(
      "vsetivli zero, 16, e8, m1, ta, ma\n\t"
      "vse8.v   v3, (%0)\n\t"
      :
      : "r"(out)
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
  trap_setup();
  printf("VDOT SEW TEST BEGIN (VLEN=128, 期望: SEW=8 合法 / SEW=16,32 触发非法指令)\n");

  int npass = 0;
  int ncase = 0;
  int8_t a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0};
  int8_t b[16] = {8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  int8_t out[16];

  // ---- case 1: SEW=8 合法执行，期望 120，尾部 12 字节为 0，无陷阱 ----
  ncase++;
  g_trap_flag = 0;
  g_trap_mcause = 0;
  for (int i = 0; i < 16; i++) out[i] = (int8_t)0x5a;
  vdot_e8(out, a, b);
  int32_t got = load_i32_le(out);
  int tail_zero = 1;
  for (int i = 4; i < 16; i++) {
    if (out[i] != 0) tail_zero = 0;
  }
  int ok1 = (g_trap_flag == 0) && (got == 120) && tail_zero;
  printf("[CASE 1] SEW=8 legal        : trap=%lu got=%d expect=120 tail_zero=%d %s\n",
         g_trap_flag, (int)got, tail_zero, ok1 ? "PASS" : "FAIL");

  // ---- case 2: SEW=16，期望陷阱，且 vd 保持 0x5a ----
  ncase++;
  g_trap_flag = 0;
  g_trap_mcause = 0;
  fill_v3_5a();
  vdot_e16();
  int ok2 = (g_trap_flag == 1);
  printf("[CASE 2] SEW=16 must trap   : trap=%lu mcause=%lu %s\n",
         g_trap_flag, g_trap_mcause, ok2 ? "PASS" : "FAIL");

  // ---- case 3: SEW=32，期望陷阱 ----
  ncase++;
  g_trap_flag = 0;
  g_trap_mcause = 0;
  vdot_e32();
  int ok3 = (g_trap_flag == 1);
  printf("[CASE 3] SEW=32 must trap   : trap=%lu mcause=%lu %s\n",
         g_trap_flag, g_trap_mcause, ok3 ? "PASS" : "FAIL");

  // ---- case 4: 两次被陷阱打断的 vdot 都不能写 vd（v3 仍是 0x5a） ----
  ncase++;
  store_v3(out);
  int unchanged = 1;
  for (int i = 0; i < 16; i++) {
    if ((uint8_t)out[i] != 0x5a) unchanged = 0;
  }
  printf("[CASE 4] trapped vdot no-wb : vd_unchanged=%d %s\n",
         unchanged, unchanged ? "PASS" : "FAIL");
  printf("         got_bytes=");
  dump16(out);

  // ---- case 5: 陷阱返回后切回 SEW=8，仍能正确执行 ----
  ncase++;
  g_trap_flag = 0;
  for (int i = 0; i < 16; i++) out[i] = (int8_t)0x5a;
  vdot_e8(out, a, b);
  int32_t got5 = load_i32_le(out);
  int ok5 = (g_trap_flag == 0) && (got5 == 120);
  printf("[CASE 5] SEW=8 after traps : trap=%lu got=%d expect=120 %s\n",
         g_trap_flag, (int)got5, ok5 ? "PASS" : "FAIL");

  npass = ok1 + ok2 + ok3 + unchanged + ok5;
  printf("VDOT SEW RESULT: %d/%d PASSED\n", npass, ncase);
  return (npass == ncase) ? 0 : 1;
}
