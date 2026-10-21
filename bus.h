#pragma once
#include "types.h"

// Memory interface seen by a CPU core. Addresses are passed through unaligned;
// implementations align them to the access size.
class Bus {
public:
  virtual ~Bus() = default;
  virtual u8 read8(u32 addr) = 0;
  virtual u16 read16(u32 addr) = 0;
  virtual u32 read32(u32 addr) = 0;
  virtual void write8(u32 addr, u8 value) = 0;
  virtual void write16(u32 addr, u16 value) = 0;
  virtual void write32(u32 addr, u32 value) = 0;
};
