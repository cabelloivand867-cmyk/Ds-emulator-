#pragma once
#include <functional>
#include "bus.h"
#include "types.h"

enum : u32 {
  MODE_USR = 0x10,
  MODE_FIQ = 0x11,
  MODE_IRQ = 0x12,
  MODE_SVC = 0x13,
  MODE_ABT = 0x17,
  MODE_UND = 0x1B,
  MODE_SYS = 0x1F
};

// ARM9 = ARMv5TE (ARM946E-S), ARM7 = ARMv4T (ARM7TDMI).
//
// r[15] holds the address of the instruction currently being executed
// (or about to be). Reads of R15 inside instructions add the pipeline offset.
class Cpu {
public:
  Cpu(bool arm9, Bus& bus);

  void reset();
  // Executes one instruction (or takes a pending IRQ / idles when halted).
  // Returns the number of CPU cycles consumed.
  int step();
  // Sets PC and Thumb state directly (used by direct boot).
  void jump(u32 addr, bool thumb);
  void setMode(u32 mode);
  bool inThumb() const { return (cpsr & BIT(5)) != 0; }

  u32 r[16] = {};
  u32 cpsr = 0;
  bool halted = false;
  bool irqLine = false;  // asserted by the interrupt controller (IME && IE & IF)
  const bool isArm9;
  u32 vectorBase = 0;
  u64 cycles = 0;

  // Optional HLE hook: return true if the SWI was handled.
  std::function<bool(u32 swiNumber)> swiHook;
  // CP15 (ARM9 only): crn, crm, op1, op2 [, value]
  std::function<u32(u32, u32, u32, u32)> cp15Read;
  std::function<void(u32, u32, u32, u32, u32)> cp15Write;

private:
  Bus& bus;
  u32 bankR13[6] = {}, bankR14[6] = {}, spsr[6] = {};
  u32 fiqR[5] = {}, usrR[5] = {};
  bool branched = false;
  int cyc = 1;

  static int bankIndex(u32 mode);
  u32 getSpsr() const;
  void writeCpsr(u32 value, u32 mask);
  void restoreSpsr();
  bool cond(u32 c) const;
  u32 R(int n) const { return n == 15 ? r[15] + (inThumb() ? 4 : 8) : r[n]; }

  void setNZ(u32 v);
  void setC(bool c);
  void setV(bool v);
  u32 alu(u32 opc, u32 a, u32 b, bool S, bool shifterCarry);

  void branch(u32 addr);    // same-state jump
  void branchEx(u32 addr);  // interworking jump (bit0 selects Thumb)
  void exception(u32 vector, u32 mode, u32 returnAddr, bool disableFiq);
  void undefinedInstr();
  void swi(u32 num);

  u32 rdWord(u32 a);

  void execArm(u32 op);
  void armDataProc(u32 op);
  void armMisc(u32 op);
  void armMultiply(u32 op);
  void armSwap(u32 op);
  void armSingleTransfer(u32 op);
  void armHalfTransfer(u32 op);
  void armBlockTransfer(u32 op);
  void armBranch(u32 op);
  void armCoproc(u32 op);
  void doMsr(u32 op, u32 val);

  void execThumb(u16 op);
};
