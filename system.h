#pragma once
#include <string>
#include <vector>
#include "cpu.h"

class System;

class Arm9Bus final : public Bus {
public:
  explicit Arm9Bus(System& s) : sys(s) {}
  u8 read8(u32 a) override;
  u16 read16(u32 a) override;
  u32 read32(u32 a) override;
  void write8(u32 a, u8 v) override;
  void write16(u32 a, u16 v) override;
  void write32(u32 a, u32 v) override;

private:
  template <typename T> T rd(u32 a);
  template <typename T> void wr(u32 a, T v);
  System& sys;
};

class Arm7Bus final : public Bus {
public:
  explicit Arm7Bus(System& s) : sys(s) {}
  u8 read8(u32 a) override;
  u16 read16(u32 a) override;
  u32 read32(u32 a) override;
  void write8(u32 a, u8 v) override;
  void write16(u32 a, u16 v) override;
  void write32(u32 a, u32 v) override;

private:
  template <typename T> T rd(u32 a);
  template <typename T> void wr(u32 a, T v);
  System& sys;
};

struct RomHeader {
  char title[13] = {};
  char gameCode[5] = {};
  u32 arm9Offset = 0, arm9Entry = 0, arm9Ram = 0, arm9Size = 0;
  u32 arm7Offset = 0, arm7Entry = 0, arm7Ram = 0, arm7Size = 0;
};

class System {
public:
  System();

  bool loadRom(std::vector<u8> data);
  bool loadRomFile(const std::string& path);
  // Emulates what the DS BIOS/firmware leaves behind and starts both CPUs.
  void directBoot();
  // Runs both CPUs for the given number of ARM9 cycles (ARM7 runs at half speed).
  void runCycles(u64 arm9Cycles);

  // --- frontend API (used by the Android app and any other frontend) ---
  static constexpr int kScreenW = 256, kScreenH = 192;
  static constexpr int kAudioRate = 32768;  // DS SPU native output rate
  // Key bits: 0 A, 1 B, 2 Select, 3 Start, 4 Right, 5 Left, 6 Up, 7 Down, 8 R, 9 L, 10 X, 11 Y
  void setKeys(u32 mask) { keys = mask & 0xFFF; }
  void setTouch(bool down, int x, int y);
  // Emulates one video frame: updates input registers, runs the CPUs, renders both
  // screens into `framebuffer` and generates the frame's audio.
  void runFrame();
  // Moves pending interleaved stereo s16 samples into dst; returns samples copied.
  size_t takeAudio(s16* dst, size_t maxSamples);

  // 256x384 pixels, top screen first, bytes R,G,B,A (Android ARGB_8888 layout).
  std::vector<u32> framebuffer = std::vector<u32>(kScreenW * kScreenH * 2, 0xFF000000u);
  std::vector<s16> audioBuf;
  u32 keys = 0;
  bool touchDown = false;
  int touchX = 0, touchY = 0;
  u64 frameCount = 0;

  // --- memory ---
  std::vector<u8> mainRam = std::vector<u8>(0x400000);
  std::vector<u8> sharedWram = std::vector<u8>(0x8000);
  std::vector<u8> arm7Wram = std::vector<u8>(0x10000);
  std::vector<u8> palette = std::vector<u8>(0x800);
  std::vector<u8> oam = std::vector<u8>(0x800);
  std::vector<u8> vram = std::vector<u8>(0x100000);   // placeholder flat VRAM
  std::vector<u8> vram7 = std::vector<u8>(0x40000);   // ARM7 view (VRAM C/D)
  std::vector<u8> io9 = std::vector<u8>(0x2000);      // placeholder register file
  std::vector<u8> io7 = std::vector<u8>(0x1000);
  std::vector<u8> wifi = std::vector<u8>(0x2000);
  std::vector<u8> bios9 = std::vector<u8>(0x1000);    // mini BIOS (vectors + IRQ stub)
  std::vector<u8> bios7 = std::vector<u8>(0x4000);
  std::vector<u8> itcm = std::vector<u8>(0x8000);
  std::vector<u8> dtcm = std::vector<u8>(0x4000);

  u8 wramcnt = 3;
  bool itcmEnabled = false, dtcmEnabled = false;
  u32 itcmSize = 0, dtcmBase = 0, dtcmSize = 0;
  u32 cp15Control = 0x00052078, cp15ItcmReg = 0, cp15DtcmReg = 0;

  RomHeader header;
  std::vector<u8> rom;
  bool romLoaded = false;

  Arm9Bus bus9;
  Arm7Bus bus7;
  Cpu cpu9;
  Cpu cpu7;

  u64 arm9Clock = 0, arm7Clock = 0;

private:
  double audioAccum = 0;
  void updateInputRegs();
  void renderScreens();
  void generateAudio();
  void buildBios();
  u32 readCp15(u32 crn, u32 crm, u32 op1, u32 op2);
  void writeCp15(u32 crn, u32 crm, u32 op1, u32 op2, u32 v);
};
