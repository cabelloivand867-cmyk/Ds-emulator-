#include "system.h"

#include <algorithm>
#include <fstream>

namespace {

template <typename T> inline T ld(const u8* p) {
  T v;
  std::memcpy(&v, p, sizeof(T));
  return v;
}
template <typename T> inline void st(u8* p, T v) { std::memcpy(p, &v, sizeof(T)); }

inline void put32(std::vector<u8>& mem, u32 off, u32 v) { st<u32>(&mem[off], v); }

u32 tcmSizeFromReg(u32 v) {
  u32 field = (v >> 1) & 0x1F;
  u64 size = (u64)512 << field;
  return size > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)size;
}

}  // namespace

// ---------------------------------------------------------------------------
// ARM9 bus
// ---------------------------------------------------------------------------

template <typename T> T Arm9Bus::rd(u32 a) {
  a &= ~(u32)(sizeof(T) - 1);
  if (sys.dtcmEnabled && (a - sys.dtcmBase) < sys.dtcmSize)
    return ld<T>(&sys.dtcm[(a - sys.dtcmBase) & 0x3FFF]);
  if (sys.itcmEnabled && a < sys.itcmSize) return ld<T>(&sys.itcm[a & 0x7FFF]);
  switch (a >> 24) {
    case 0x02: return ld<T>(&sys.mainRam[a & 0x3FFFFF]);
    case 0x03:
      switch (sys.wramcnt) {
        case 0: return ld<T>(&sys.sharedWram[a & 0x7FFF]);
        case 1: return ld<T>(&sys.sharedWram[0x4000 + (a & 0x3FFF)]);
        case 2: return ld<T>(&sys.sharedWram[a & 0x3FFF]);
        default: return 0;
      }
    case 0x04: return ld<T>(&sys.io9[a & 0x1FFF]);
    case 0x05: return ld<T>(&sys.palette[a & 0x7FF]);
    case 0x06: return ld<T>(&sys.vram[a & 0xFFFFF]);
    case 0x07: return ld<T>(&sys.oam[a & 0x7FF]);
    case 0xFF:
      if (a >= 0xFFFF0000u) return ld<T>(&sys.bios9[a & 0xFFF]);
      return 0;
    default: return 0;
  }
}

template <typename T> void Arm9Bus::wr(u32 a, T v) {
  a &= ~(u32)(sizeof(T) - 1);
  if (sys.dtcmEnabled && (a - sys.dtcmBase) < sys.dtcmSize) {
    st<T>(&sys.dtcm[(a - sys.dtcmBase) & 0x3FFF], v);
    return;
  }
  if (sys.itcmEnabled && a < sys.itcmSize) {
    st<T>(&sys.itcm[a & 0x7FFF], v);
    return;
  }
  switch (a >> 24) {
    case 0x02: st<T>(&sys.mainRam[a & 0x3FFFFF], v); break;
    case 0x03:
      switch (sys.wramcnt) {
        case 0: st<T>(&sys.sharedWram[a & 0x7FFF], v); break;
        case 1: st<T>(&sys.sharedWram[0x4000 + (a & 0x3FFF)], v); break;
        case 2: st<T>(&sys.sharedWram[a & 0x3FFF], v); break;
        default: break;
      }
      break;
    case 0x04:
      st<T>(&sys.io9[a & 0x1FFF], v);
      sys.wramcnt = sys.io9[0x247] & 3;  // TODO: real I/O register dispatch
      break;
    case 0x05: st<T>(&sys.palette[a & 0x7FF], v); break;
    case 0x06: st<T>(&sys.vram[a & 0xFFFFF], v); break;
    case 0x07: st<T>(&sys.oam[a & 0x7FF], v); break;
    default: break;
  }
}

u8 Arm9Bus::read8(u32 a) { return rd<u8>(a); }
u16 Arm9Bus::read16(u32 a) { return rd<u16>(a); }
u32 Arm9Bus::read32(u32 a) { return rd<u32>(a); }
void Arm9Bus::write8(u32 a, u8 v) { wr<u8>(a, v); }
void Arm9Bus::write16(u32 a, u16 v) { wr<u16>(a, v); }
void Arm9Bus::write32(u32 a, u32 v) { wr<u32>(a, v); }

// ---------------------------------------------------------------------------
// ARM7 bus
// ---------------------------------------------------------------------------

template <typename T> T Arm7Bus::rd(u32 a) {
  a &= ~(u32)(sizeof(T) - 1);
  switch (a >> 24) {
    case 0x00:
      if (a < 0x4000) return ld<T>(&sys.bios7[a]);
      return 0;
    case 0x02: return ld<T>(&sys.mainRam[a & 0x3FFFFF]);
    case 0x03:
      if (a >= 0x03800000) return ld<T>(&sys.arm7Wram[a & 0xFFFF]);
      switch (sys.wramcnt) {
        case 0: return ld<T>(&sys.arm7Wram[a & 0xFFFF]);
        case 1: return ld<T>(&sys.sharedWram[a & 0x3FFF]);
        case 2: return ld<T>(&sys.sharedWram[0x4000 + (a & 0x3FFF)]);
        default: return ld<T>(&sys.sharedWram[a & 0x7FFF]);
      }
    case 0x04:
      if (a < 0x04800000) return ld<T>(&sys.io7[a & 0xFFF]);
      return ld<T>(&sys.wifi[a & 0x1FFF]);
    case 0x06: return ld<T>(&sys.vram7[a & 0x3FFFF]);
    default: return 0;
  }
}

template <typename T> void Arm7Bus::wr(u32 a, T v) {
  a &= ~(u32)(sizeof(T) - 1);
  switch (a >> 24) {
    case 0x02: st<T>(&sys.mainRam[a & 0x3FFFFF], v); break;
    case 0x03:
      if (a >= 0x03800000) { st<T>(&sys.arm7Wram[a & 0xFFFF], v); break; }
      switch (sys.wramcnt) {
        case 0: st<T>(&sys.arm7Wram[a & 0xFFFF], v); break;
        case 1: st<T>(&sys.sharedWram[a & 0x3FFF], v); break;
        case 2: st<T>(&sys.sharedWram[0x4000 + (a & 0x3FFF)], v); break;
        default: st<T>(&sys.sharedWram[a & 0x7FFF], v); break;
      }
      break;
    case 0x04:
      if (a < 0x04800000) st<T>(&sys.io7[a & 0xFFF], v);
      else st<T>(&sys.wifi[a & 0x1FFF], v);
      break;
    case 0x06: st<T>(&sys.vram7[a & 0x3FFFF], v); break;
    default: break;
  }
}

u8 Arm7Bus::read8(u32 a) { return rd<u8>(a); }
u16 Arm7Bus::read16(u32 a) { return rd<u16>(a); }
u32 Arm7Bus::read32(u32 a) { return rd<u32>(a); }
void Arm7Bus::write8(u32 a, u8 v) { wr<u8>(a, v); }
void Arm7Bus::write16(u32 a, u16 v) { wr<u16>(a, v); }
void Arm7Bus::write32(u32 a, u32 v) { wr<u32>(a, v); }

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

System::System() : bus9(*this), bus7(*this), cpu9(true, bus9), cpu7(false, bus7) {
  buildBios();
  cpu9.cp15Read = [this](u32 crn, u32 crm, u32 op1, u32 op2) { return readCp15(crn, crm, op1, op2); };
  cpu9.cp15Write = [this](u32 crn, u32 crm, u32 op1, u32 op2, u32 v) { writeCp15(crn, crm, op1, op2, v); };
}

// A tiny stand-in for the real BIOS: exception vectors that spin, plus the
// IRQ trampoline that calls the game's handler (as the real BIOS does).
void System::buildBios() {
  for (u32 i = 0; i < 8; i++) {
    put32(bios9, i * 4, 0xEAFFFFFE);  // b .
    put32(bios7, i * 4, 0xEAFFFFFE);
  }
  put32(bios9, 0x18, 0xEA000038);  // b 0x100
  put32(bios7, 0x18, 0xEA000038);

  static const u32 irq9[] = {
      0xE92D500F,  // stmfd sp!, {r0-r3, r12, lr}
      0xEE190F11,  // mrc   p15, 0, r0, c9, c1, 0     (DTCM base)
      0xE1A00620,  // mov   r0, r0, lsr #12
      0xE1A00600,  // mov   r0, r0, lsl #12
      0xE2800901,  // add   r0, r0, #0x4000
      0xE28FE000,  // add   lr, pc, #0
      0xE510F004,  // ldr   pc, [r0, #-4]
      0xE8BD500F,  // ldmfd sp!, {r0-r3, r12, lr}
      0xE25EF004,  // subs  pc, lr, #4
  };
  static const u32 irq7[] = {
      0xE92D500F,  // stmfd sp!, {r0-r3, r12, lr}
      0xE3A0C301,  // mov   r12, #0x04000000
      0xE28FE000,  // add   lr, pc, #0
      0xE51CF004,  // ldr   pc, [r12, #-4]
      0xE8BD500F,  // ldmfd sp!, {r0-r3, r12, lr}
      0xE25EF004,  // subs  pc, lr, #4
  };
  for (u32 i = 0; i < sizeof(irq9) / 4; i++) put32(bios9, 0x100 + i * 4, irq9[i]);
  for (u32 i = 0; i < sizeof(irq7) / 4; i++) put32(bios7, 0x100 + i * 4, irq7[i]);
}

u32 System::readCp15(u32 crn, u32 crm, u32 op1, u32 op2) {
  if (crn == 0 && crm == 0) {
    if (op2 == 0) return 0x41059461;  // ARM946E-S
    if (op2 == 1) return 0x0F0D2112;  // cache type
    return 0;
  }
  if (crn == 1 && crm == 0 && op2 == 0) return cp15Control;
  if (crn == 9 && crm == 1) return op2 == 0 ? cp15DtcmReg : cp15ItcmReg;
  return 0;
}

void System::writeCp15(u32 crn, u32 crm, u32 op1, u32 op2, u32 v) {
  if (crn == 1 && crm == 0 && op2 == 0) {
    cp15Control = v;
    dtcmEnabled = v & BIT(16);
    itcmEnabled = v & BIT(18);
    cpu9.vectorBase = (v & BIT(13)) ? 0xFFFF0000u : 0;
  } else if (crn == 7 && ((crm == 0 && op2 == 4) || (crm == 8 && op2 == 2))) {
    cpu9.halted = true;  // wait for interrupt
  } else if (crn == 9 && crm == 1) {
    if (op2 == 0) {
      cp15DtcmReg = v;
      dtcmBase = v & 0xFFFFF000u;
      dtcmSize = tcmSizeFromReg(v);
    } else if (op2 == 1) {
      cp15ItcmReg = v;
      itcmSize = tcmSizeFromReg(v);
    }
  }
  // Cache / write-buffer / protection-unit registers are accepted and ignored.
}

bool System::loadRom(std::vector<u8> data) {
  if (data.size() < 0x200) return false;
  RomHeader h;
  std::memcpy(h.title, &data[0], 12);
  std::memcpy(h.gameCode, &data[0x0C], 4);
  h.arm9Offset = ld<u32>(&data[0x20]);
  h.arm9Entry = ld<u32>(&data[0x24]);
  h.arm9Ram = ld<u32>(&data[0x28]);
  h.arm9Size = ld<u32>(&data[0x2C]);
  h.arm7Offset = ld<u32>(&data[0x30]);
  h.arm7Entry = ld<u32>(&data[0x34]);
  h.arm7Ram = ld<u32>(&data[0x38]);
  h.arm7Size = ld<u32>(&data[0x3C]);
  u64 sz = data.size();
  if ((u64)h.arm9Offset + h.arm9Size > sz || (u64)h.arm7Offset + h.arm7Size > sz) return false;
  header = h;
  rom = std::move(data);
  romLoaded = true;
  return true;
}

bool System::loadRomFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::vector<u8> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return loadRom(std::move(data));
}

void System::directBoot() {
  if (!romLoaded) return;

  // Header + a few well-known values the BIOS/firmware leaves in main RAM.
  std::copy(rom.begin(), rom.begin() + std::min<size_t>(0x170, rom.size()), mainRam.begin() + 0x3FFE00);
  put32(mainRam, 0x3FF800, 0x00001FC2);  // chip ID
  put32(mainRam, 0x3FF804, 0x00001FC2);
  put32(mainRam, 0x3FFC00, 0x00001FC2);
  put32(mainRam, 0x3FFC04, 0x00001FC2);

  // Boot-time hardware state
  wramcnt = 3;
  io9[0x247] = 3;
  io9[0x300] = 1;  // POSTFLG
  io7[0x300] = 1;

  cpu9.reset();
  cpu7.reset();

  // ARM9 CP15: control, DTCM @ 0x03000000 (16KB), ITCM mirrored over 32MB
  writeCp15(1, 0, 0, 0, 0x00056078);
  writeCp15(9, 1, 0, 0, 0x0300000A);
  writeCp15(9, 1, 0, 1, 0x00000020);

  for (u32 i = 0; i < header.arm9Size; i++) bus9.write8(header.arm9Ram + i, rom[header.arm9Offset + i]);
  for (u32 i = 0; i < header.arm7Size; i++) bus7.write8(header.arm7Ram + i, rom[header.arm7Offset + i]);

  auto setupStacks = [](Cpu& c, u32 sys, u32 irq, u32 svc) {
    c.setMode(MODE_SVC); c.r[13] = svc;
    c.setMode(MODE_IRQ); c.r[13] = irq;
    c.setMode(MODE_SYS); c.r[13] = sys;
    c.cpsr = 0xDF;  // SYS mode, IRQ+FIQ disabled, ARM state
  };
  setupStacks(cpu9, 0x03002F7C, 0x03003F80, 0x03003FC0);
  setupStacks(cpu7, 0x0380FD80, 0x0380FF80, 0x0380FFC0);

  cpu9.jump(header.arm9Entry, false);
  cpu7.jump(header.arm7Entry, false);
  arm9Clock = arm7Clock = 0;
}

void System::runCycles(u64 n) {
  u64 target = arm9Clock + n;
  while (arm9Clock < target) {
    arm9Clock += (u64)cpu9.step();
    while (arm7Clock * 2 < arm9Clock) arm7Clock += (u64)cpu7.step();
  }
}

// ---------------------------------------------------------------------------
// Frontend API
// ---------------------------------------------------------------------------

void System::setTouch(bool down, int x, int y) {
  touchDown = down;
  touchX = std::clamp(x, 0, kScreenW - 1);
  touchY = std::clamp(y, 0, kScreenH - 1);
}

// KEYINPUT (0x04000130, both CPUs) and EXTKEYIN (0x04000136, ARM7): 0 = pressed.
// The touch coordinates will be served through the ARM7 SPI touch controller once
// that exists (milestone 3); here we only expose the pen-down flag.
void System::updateInputRegs() {
  u16 key = (u16)(~keys & 0x3FF);
  u16 ext = 0x7F;
  if (keys & BIT(10)) ext &= ~1u;
  if (keys & BIT(11)) ext &= ~2u;
  if (touchDown) ext &= ~0x40u;
  st<u16>(&io9[0x130], key);
  st<u16>(&io7[0x130], key);
  st<u16>(&io7[0x136], ext);
}

// PLACEHOLDER: the 2D/3D engines are not implemented yet (milestones 4 and 7).
// Until then both screens show a dark background with a border so the
// frontend's layout and touch mapping can be checked.
void System::renderScreens() {
  const u32 bg = 0xFF2A1F18, border = 0xFF6E5A46;
  for (int s = 0; s < 2; s++) {
    for (int y = 0; y < kScreenH; y++) {
      for (int x = 0; x < kScreenW; x++) {
        bool edge = x == 0 || y == 0 || x == kScreenW - 1 || y == kScreenH - 1;
        framebuffer[(size_t)(s * kScreenH + y) * kScreenW + x] = edge ? border : bg;
      }
    }
  }
  if (touchDown) {  // draw a small marker under the stylus on the bottom screen
    for (int dy = -2; dy <= 2; dy++)
      for (int dx = -2; dx <= 2; dx++) {
        int x = touchX + dx, y = touchY + dy;
        if (x >= 0 && x < kScreenW && y >= 0 && y < kScreenH)
          framebuffer[(size_t)(kScreenH + y) * kScreenW + x] = 0xFF40C0FF;
      }
  }
}

// PLACEHOLDER: silence at the right rate so the frontend's audio path and pacing
// work. Replaced by the 16-channel SPU mixer in milestone 6.
void System::generateAudio() {
  audioAccum += kAudioRate / 59.8261;
  size_t frames = (size_t)audioAccum;
  audioAccum -= (double)frames;
  audioBuf.insert(audioBuf.end(), frames * 2, (s16)0);
  if (audioBuf.size() > (size_t)kAudioRate * 2) audioBuf.erase(audioBuf.begin(), audioBuf.end() - kAudioRate * 2);
}

void System::runFrame() {
  updateInputRegs();
  if (romLoaded) runCycles(1120380);
  renderScreens();
  generateAudio();
  frameCount++;
}

size_t System::takeAudio(s16* dst, size_t maxSamples) {
  size_t n = std::min(maxSamples, audioBuf.size());
  std::memcpy(dst, audioBuf.data(), n * sizeof(s16));
  audioBuf.erase(audioBuf.begin(), audioBuf.begin() + n);
  return n;
}
