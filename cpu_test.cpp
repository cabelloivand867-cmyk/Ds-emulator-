// CPU / boot unit tests. Programs are hand-assembled machine code loaded
// through a synthetic .nds image, so no ROM or BIOS files are needed.
#include <cstdio>
#include <memory>
#include <vector>

#include "system.h"

static int failures = 0;

#define CHECK_EQ(actual, expected)                                                         \
  do {                                                                                     \
    unsigned long long _a = (unsigned long long)(actual), _e = (unsigned long long)(expected); \
    if (_a != _e) {                                                                        \
      std::printf("FAIL line %d: %s = 0x%llX, expected 0x%llX\n", __LINE__, #actual, _a, _e); \
      failures++;                                                                          \
    }                                                                                      \
  } while (0)

static void put32(std::vector<u8>& v, size_t off, u32 x) { std::memcpy(&v[off], &x, 4); }

// Builds a ROM whose ARM9 code loads at 0x02000000 and ARM7 code at 0x02380000.
static std::unique_ptr<System> boot(const std::vector<u32>& a9, const std::vector<u32>& a7) {
  const size_t a9off = 0x4000, a7off = a9off + a9.size() * 4;
  std::vector<u8> rom(a7off + a7.size() * 4, 0);
  std::memcpy(&rom[0], "TESTROM", 7);
  std::memcpy(&rom[0x0C], "TEST", 4);
  put32(rom, 0x20, (u32)a9off);
  put32(rom, 0x24, 0x02000000);
  put32(rom, 0x28, 0x02000000);
  put32(rom, 0x2C, (u32)(a9.size() * 4));
  put32(rom, 0x30, (u32)a7off);
  put32(rom, 0x34, 0x02380000);
  put32(rom, 0x38, 0x02380000);
  put32(rom, 0x3C, (u32)(a7.size() * 4));
  std::memcpy(&rom[a9off], a9.data(), a9.size() * 4);
  std::memcpy(&rom[a7off], a7.data(), a7.size() * 4);
  auto sys = std::make_unique<System>();
  if (!sys->loadRom(rom)) { std::printf("FAIL: loadRom\n"); failures++; }
  sys->directBoot();
  return sys;
}

static const std::vector<u32> kSpin7 = {0xE3A00007, 0xEAFFFFFE};  // mov r0,#7 ; b .

static void run9(System& s, int n) { for (int i = 0; i < n; i++) s.cpu9.step(); }

static void testBootState() {
  auto s = boot({0xEAFFFFFE}, kSpin7);
  CHECK_EQ(s->cpu9.r[15], 0x02000000);
  CHECK_EQ(s->cpu7.r[15], 0x02380000);
  CHECK_EQ(s->cpu9.r[13], 0x03002F7C);
  CHECK_EQ(s->cpu7.r[13], 0x0380FD80);
  CHECK_EQ(s->cpu9.cpsr & 0x1F, MODE_SYS);
  CHECK_EQ(s->bus9.read32(0x027FFE0C), 0x54534554);  // "TEST" game code copied to header area
  CHECK_EQ(s->cpu9.vectorBase, 0xFFFF0000);
  CHECK_EQ(s->bus9.read32(0xFFFF0018), 0xEA000038);   // mini BIOS IRQ vector
}

static void testArmBasics() {
  auto s = boot(
      {
          0xE3A00005,  // mov r0,#5
          0xE2801003,  // add r1,r0,#3
          0xE0812000,  // add r2,r1,r0
          0xE1A03102,  // mov r3,r2,lsl #2
          0xE3A04000,  // mov r4,#0
          0xE2844001,  // add r4,r4,#1       <-+
          0xE3540003,  // cmp r4,#3            |
          0x1AFFFFFC,  // bne                -/
          0xE3A05402,  // mov r5,#0x02000000
          0xE2855C01,  // add r5,r5,#0x100
          0xE5852000,  // str r2,[r5]
          0xE5956000,  // ldr r6,[r5]
          0xEAFFFFFE,  // b .
      },
      kSpin7);
  run9(*s, 100);
  CHECK_EQ(s->cpu9.r[0], 5);
  CHECK_EQ(s->cpu9.r[1], 8);
  CHECK_EQ(s->cpu9.r[2], 13);
  CHECK_EQ(s->cpu9.r[3], 52);
  CHECK_EQ(s->cpu9.r[4], 3);
  CHECK_EQ(s->cpu9.r[6], 13);
  CHECK_EQ(s->bus9.read32(0x02000100), 13);
}

static void testArmMisc() {
  auto s = boot(
      {
          0xE3A0000A,  // mov r0,#10
          0xE3A01003,  // mov r1,#3
          0xE0020190,  // mul r2,r0,r1
          0xE16F3F12,  // clz r3,r2
          0xE92D0007,  // stmfd sp!,{r0-r2}
          0xE3A00000,  // mov r0,#0
          0xE3A01000,  // mov r1,#0
          0xE8BD0007,  // ldmfd sp!,{r0-r2}
          0xE250500A,  // subs r5,r0,#10
          0x03A06001,  // moveq r6,#1
          0x13A07001,  // movne r7,#1
          0xEAFFFFFE,  // b .
      },
      kSpin7);
  run9(*s, 40);
  CHECK_EQ(s->cpu9.r[0], 10);
  CHECK_EQ(s->cpu9.r[1], 3);
  CHECK_EQ(s->cpu9.r[2], 30);
  CHECK_EQ(s->cpu9.r[3], 27);
  CHECK_EQ(s->cpu9.r[13], 0x03002F7C);
  CHECK_EQ(s->cpu9.r[5], 0);
  CHECK_EQ(s->cpu9.r[6], 1);
  CHECK_EQ(s->cpu9.r[7], 0);
}

static void testThumb() {
  auto s = boot(
      {
          0xE28F0001,  // add r0,pc,#1       (address of thumb code | 1)
          0xE12FFF10,  // bx r0
          0xF0002005,  // movs r0,#5 ; bl prefix
          0x3001F802,  // bl suffix ; adds r0,#1
          0x3007E7FE,  // b . ; adds r0,#7   (subroutine)
          0x00004770,  // bx lr
      },
      kSpin7);
  run9(*s, 20);
  CHECK_EQ(s->cpu9.r[0], 13);
  CHECK_EQ(s->cpu9.inThumb(), true);
}

static void testIrqThroughBios() {
  auto s = boot(
      {
          0xE10F0000,  // mrs r0,cpsr
          0xE3C00080,  // bic r0,r0,#0x80
          0xE121F000,  // msr cpsr_c,r0
          0xEAFFFFFE,  // b .
          0xE2877001,  // add r7,r7,#1       <- IRQ handler @ 0x02000010
          0xE12FFF1E,  // bx lr
      },
      kSpin7);
  s->bus9.write32(0x03003FFC, 0x02000010);  // handler pointer at DTCM base + 0x3FFC
  run9(*s, 10);
  CHECK_EQ(s->cpu9.cpsr & BIT(7), 0);
  s->cpu9.irqLine = true;
  s->cpu9.step();  // takes the IRQ
  CHECK_EQ(s->cpu9.cpsr & 0x1F, MODE_IRQ);
  s->cpu9.irqLine = false;
  run9(*s, 30);
  CHECK_EQ(s->cpu9.r[7], 1);
  CHECK_EQ(s->cpu9.cpsr & 0x1F, MODE_SYS);
  CHECK_EQ(s->cpu9.r[15], 0x0200000C);
}

static void testDualCpuScheduling() {
  auto s = boot({0xEAFFFFFE}, kSpin7);
  s->runCycles(1000);
  CHECK_EQ(s->cpu7.r[0], 7);
  CHECK_EQ(s->cpu9.r[15], 0x02000000);
}

static void testSharedWram() {
  auto s = boot({0xEAFFFFFE}, kSpin7);
  // WRAMCNT = 3: ARM7 owns all 32KB of shared WRAM, ARM9 sees nothing.
  // (0x03004000 is outside the 16KB DTCM at 0x03000000, so ARM9 hits the real WRAM mapping.)
  s->bus7.write32(0x03004000, 0xCAFEBABE);
  CHECK_EQ(s->bus9.read32(0x03004000), 0);
  s->bus9.write8(0x04000247, 0);  // WRAMCNT=0: all 32KB to ARM9
  CHECK_EQ(s->wramcnt, 0);
  CHECK_EQ(s->bus9.read32(0x03004000), 0xCAFEBABE);
  s->bus9.write8(0x04000247, 1);  // ARM9 gets the 2nd 16KB mirrored, ARM7 the 1st
  CHECK_EQ(s->bus9.read32(0x03004000), 0xCAFEBABE);
  CHECK_EQ(s->bus7.read32(0x03000000), 0);
}

int main() {
  testBootState();
  testArmBasics();
  testArmMisc();
  testThumb();
  testIrqThroughBios();
  testDualCpuScheduling();
  testSharedWram();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all CPU tests passed\n");
  return 0;
}
