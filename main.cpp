#include <cstdio>
#include <cstdlib>
#include <memory>

#include "system.h"

static void dump(const char* name, const Cpu& c) {
  std::printf("%s: pc=%08X cpsr=%08X %s%s\n", name, c.r[15], c.cpsr, c.inThumb() ? "THUMB" : "ARM",
              c.halted ? " HALTED" : "");
  for (int i = 0; i < 16; i += 4)
    std::printf("  r%-2d=%08X r%-2d=%08X r%-2d=%08X r%-2d=%08X\n", i, c.r[i], i + 1, c.r[i + 1], i + 2,
                c.r[i + 2], i + 3, c.r[i + 3]);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <game.nds> [frames]\n", argv[0]);
    return 1;
  }
  auto sys = std::make_unique<System>();
  if (!sys->loadRomFile(argv[1])) {
    std::fprintf(stderr, "failed to load ROM: %s\n", argv[1]);
    return 1;
  }
  std::printf("Title: %s  Code: %s\n", sys->header.title, sys->header.gameCode);
  sys->directBoot();

  int frames = argc > 2 ? std::atoi(argv[2]) : 60;
  constexpr u64 kCyclesPerFrame = 1120380;  // ARM9 cycles per 59.83 Hz frame
  for (int f = 0; f < frames; f++) sys->runCycles(kCyclesPerFrame);

  dump("ARM9", sys->cpu9);
  dump("ARM7", sys->cpu7);
  return 0;
}
