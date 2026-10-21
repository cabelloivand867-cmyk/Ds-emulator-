#include "cpu.h"

namespace {

inline u32 ror32(u32 v, u32 n) {
  n &= 31;
  return n ? (v >> n) | (v << (32 - n)) : v;
}

inline u32 popcnt(u32 v) {
  u32 c = 0;
  while (v) {
    v &= v - 1;
    c++;
  }
  return c;
}

inline u32 clz32(u32 v) {
  if (!v) return 32;
  u32 n = 0;
  while (!(v & 0x80000000u)) {
    v <<= 1;
    n++;
  }
  return n;
}

inline u32 addWithCarry(u32 a, u32 b, u32 cin, bool& C, bool& V) {
  u64 t = (u64)a + b + cin;
  u32 r = (u32)t;
  C = (t >> 32) & 1;
  V = ((~(a ^ b) & (a ^ r)) >> 31) & 1;
  return r;
}

inline s32 satAdd(s32 a, s32 b, bool& q) {
  s64 r = (s64)a + b;
  if (r > INT32_MAX) { q = true; return INT32_MAX; }
  if (r < INT32_MIN) { q = true; return INT32_MIN; }
  return (s32)r;
}

inline s32 satSub(s32 a, s32 b, bool& q) {
  s64 r = (s64)a - b;
  if (r > INT32_MAX) { q = true; return INT32_MAX; }
  if (r < INT32_MIN) { q = true; return INT32_MIN; }
  return (s32)r;
}

// Barrel shifter. `c` is carry in/out. `imm` selects the immediate-amount
// encoding (where LSR/ASR #0 mean #32 and ROR #0 means RRX).
u32 shiftOp(u32 type, u32 v, u32 amt, bool imm, bool& c) {
  switch (type) {
    case 0:  // LSL
      if (amt == 0) return v;
      if (amt < 32) { c = (v >> (32 - amt)) & 1; return v << amt; }
      if (amt == 32) { c = v & 1; return 0; }
      c = false;
      return 0;
    case 1:  // LSR
      if (imm && amt == 0) amt = 32;
      if (amt == 0) return v;
      if (amt < 32) { c = (v >> (amt - 1)) & 1; return v >> amt; }
      if (amt == 32) { c = v >> 31; return 0; }
      c = false;
      return 0;
    case 2:  // ASR
      if (imm && amt == 0) amt = 32;
      if (amt == 0) return v;
      if (amt < 32) { c = (v >> (amt - 1)) & 1; return (u32)((s32)v >> amt); }
      c = v >> 31;
      return (u32)((s32)v >> 31);
    default:  // ROR
      if (imm && amt == 0) {  // RRX
        bool nc = v & 1;
        u32 res = (v >> 1) | ((u32)c << 31);
        c = nc;
        return res;
      }
      if (amt == 0) return v;
      amt &= 31;
      if (amt == 0) { c = v >> 31; return v; }
      c = (v >> (amt - 1)) & 1;
      return ror32(v, amt);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------

Cpu::Cpu(bool arm9, Bus& b) : isArm9(arm9), bus(b) { reset(); }

void Cpu::reset() {
  for (auto& x : r) x = 0;
  for (int i = 0; i < 6; i++) bankR13[i] = bankR14[i] = spsr[i] = 0;
  for (int i = 0; i < 5; i++) fiqR[i] = usrR[i] = 0;
  cpsr = MODE_SVC | BIT(7) | BIT(6);
  vectorBase = isArm9 ? 0xFFFF0000u : 0;
  r[15] = vectorBase;
  halted = false;
  irqLine = false;
  cycles = 0;
}

int Cpu::bankIndex(u32 mode) {
  switch (mode & 0x1F) {
    case MODE_FIQ: return 1;
    case MODE_IRQ: return 2;
    case MODE_SVC: return 3;
    case MODE_ABT: return 4;
    case MODE_UND: return 5;
    default: return 0;  // USR / SYS
  }
}

void Cpu::setMode(u32 newMode) {
  newMode &= 0x1F;
  u32 cur = cpsr & 0x1F;
  if (cur == newMode) return;
  int ci = bankIndex(cur), ni = bankIndex(newMode);
  if (ci != ni) {
    bankR13[ci] = r[13];
    bankR14[ci] = r[14];
    if (ci == 1) {
      for (int i = 0; i < 5; i++) { fiqR[i] = r[8 + i]; r[8 + i] = usrR[i]; }
    } else if (ni == 1) {
      for (int i = 0; i < 5; i++) { usrR[i] = r[8 + i]; r[8 + i] = fiqR[i]; }
    }
    r[13] = bankR13[ni];
    r[14] = bankR14[ni];
  }
  cpsr = (cpsr & ~0x1Fu) | newMode;
}

u32 Cpu::getSpsr() const {
  int i = bankIndex(cpsr & 0x1F);
  return i ? spsr[i] : cpsr;
}

void Cpu::writeCpsr(u32 value, u32 mask) {
  if ((cpsr & 0x1F) == MODE_USR) mask &= 0xFF000000u;
  mask &= ~BIT(5);  // T bit is only changed via BX / exceptions
  u32 nv = (cpsr & ~mask) | (value & mask);
  u32 oldMode = cpsr & 0x1F;
  cpsr = (nv & ~0x1Fu) | oldMode;
  setMode(nv & 0x1F);
}

void Cpu::restoreSpsr() {
  u32 s = getSpsr();
  u32 oldMode = cpsr & 0x1F;
  cpsr = (s & ~0x1Fu) | oldMode;
  setMode(s & 0x1F);
}

bool Cpu::cond(u32 c) const {
  u32 f = cpsr >> 28;
  bool n = f & 8, z = f & 4, cc = f & 2, v = f & 1;
  switch (c) {
    case 0: return z;
    case 1: return !z;
    case 2: return cc;
    case 3: return !cc;
    case 4: return n;
    case 5: return !n;
    case 6: return v;
    case 7: return !v;
    case 8: return cc && !z;
    case 9: return !cc || z;
    case 10: return n == v;
    case 11: return n != v;
    case 12: return !z && n == v;
    case 13: return z || n != v;
    case 14: return true;
    default: return false;
  }
}

void Cpu::setNZ(u32 v) {
  cpsr = (cpsr & 0x3FFFFFFFu) | (v & 0x80000000u) | (v == 0 ? BIT(30) : 0u);
}
void Cpu::setC(bool c) { cpsr = (cpsr & ~BIT(29)) | ((u32)c << 29); }
void Cpu::setV(bool v) { cpsr = (cpsr & ~BIT(28)) | ((u32)v << 28); }

void Cpu::jump(u32 addr, bool thumb) {
  if (thumb) cpsr |= BIT(5); else cpsr &= ~BIT(5);
  r[15] = addr & (thumb ? ~1u : ~3u);
  branched = true;
}

void Cpu::branch(u32 addr) {
  r[15] = addr & (inThumb() ? ~1u : ~3u);
  branched = true;
}

void Cpu::branchEx(u32 addr) {
  if (addr & 1) { cpsr |= BIT(5); r[15] = addr & ~1u; }
  else { cpsr &= ~BIT(5); r[15] = addr & ~3u; }
  branched = true;
}

void Cpu::exception(u32 vector, u32 mode, u32 returnAddr, bool disableFiq) {
  u32 old = cpsr;
  setMode(mode);
  spsr[bankIndex(mode)] = old;
  r[14] = returnAddr;
  cpsr &= ~BIT(5);
  cpsr |= BIT(7);
  if (disableFiq) cpsr |= BIT(6);
  r[15] = vectorBase + vector;
  branched = true;
}

void Cpu::undefinedInstr() {
  exception(0x04, MODE_UND, r[15] + (inThumb() ? 2 : 4), false);
}

void Cpu::swi(u32 num) {
  if (swiHook && swiHook(num)) return;
  exception(0x08, MODE_SVC, r[15] + (inThumb() ? 2 : 4), false);
}

u32 Cpu::rdWord(u32 a) { return ror32(bus.read32(a & ~3u), (a & 3) * 8); }

// ---------------------------------------------------------------------------
// Main step
// ---------------------------------------------------------------------------

int Cpu::step() {
  if (halted) {
    if (irqLine) halted = false;  // TODO: wake on IE&IF regardless of IME
    else { cycles += 1; return 1; }
  }
  if (irqLine && !(cpsr & BIT(7))) {
    // lr = next instruction + 4 so that "subs pc, lr, #4" resumes correctly
    exception(0x18, MODE_IRQ, r[15] + 4, false);
    cycles += 3;
    return 3;
  }
  branched = false;
  cyc = 1;
  if (inThumb()) {
    u16 op = bus.read16(r[15]);
    execThumb(op);
    if (!branched) r[15] += 2;
  } else {
    u32 op = bus.read32(r[15]);
    execArm(op);
    if (!branched) r[15] += 4;
  }
  cycles += cyc;
  return cyc;
}

// ---------------------------------------------------------------------------
// ALU
// ---------------------------------------------------------------------------

u32 Cpu::alu(u32 opc, u32 a, u32 b, bool S, bool sc) {
  bool C = (cpsr >> 29) & 1, V = (cpsr >> 28) & 1;
  u32 cin = (cpsr >> 29) & 1;
  u32 res = 0;
  bool arith = false;
  switch (opc) {
    case 0: case 8: res = a & b; break;
    case 1: case 9: res = a ^ b; break;
    case 2: case 10: res = addWithCarry(a, ~b, 1, C, V); arith = true; break;
    case 3: res = addWithCarry(b, ~a, 1, C, V); arith = true; break;
    case 4: case 11: res = addWithCarry(a, b, 0, C, V); arith = true; break;
    case 5: res = addWithCarry(a, b, cin, C, V); arith = true; break;
    case 6: res = addWithCarry(a, ~b, cin, C, V); arith = true; break;
    case 7: res = addWithCarry(b, ~a, cin, C, V); arith = true; break;
    case 12: res = a | b; break;
    case 13: res = b; break;
    case 14: res = a & ~b; break;
    default: res = ~b; break;
  }
  if (S) {
    setNZ(res);
    if (arith) { setC(C); setV(V); }
    else setC(sc);
  }
  return res;
}

// ---------------------------------------------------------------------------
// ARM
// ---------------------------------------------------------------------------

void Cpu::execArm(u32 op) {
  u32 c = op >> 28;
  if (c == 0xF) {
    if (isArm9 && (op & 0x0E000000) == 0x0A000000) {  // BLX <imm>
      s32 off = ((s32)(op << 8)) >> 6;
      off += (op >> 23) & 2;
      r[14] = r[15] + 4;
      cpsr |= BIT(5);
      r[15] = (r[15] + 8 + off) & ~1u;
      branched = true;
      return;
    }
    if (isArm9) return;  // PLD and friends: treated as NOP
    undefinedInstr();
    return;
  }
  if (!cond(c)) return;

  switch ((op >> 25) & 7) {
    case 0:
      if ((op & 0x90) == 0x90) {
        if ((op & 0x60) == 0) {
          if ((op & 0x0FC000F0) == 0x00000090 || (op & 0x0F8000F0) == 0x00800090) armMultiply(op);
          else if ((op & 0x0FB00FF0) == 0x01000090) armSwap(op);
          else undefinedInstr();
        } else {
          armHalfTransfer(op);
        }
        return;
      }
      if ((op & 0x0F900000) == 0x01000000) { armMisc(op); return; }
      armDataProc(op);
      return;
    case 1:
      if ((op & 0x0FB00000) == 0x03200000) {  // MSR <imm>
        doMsr(op, ror32(op & 0xFF, ((op >> 8) & 15) * 2));
        return;
      }
      if ((op & 0x0F900000) == 0x03000000) { undefinedInstr(); return; }
      armDataProc(op);
      return;
    case 2:
      armSingleTransfer(op);
      return;
    case 3:
      if (op & 0x10) { undefinedInstr(); return; }
      armSingleTransfer(op);
      return;
    case 4: armBlockTransfer(op); return;
    case 5: armBranch(op); return;
    case 6: undefinedInstr(); return;
    default:
      if (op & BIT(24)) swi((op >> 16) & 0xFF);
      else armCoproc(op);
      return;
  }
}

void Cpu::armDataProc(u32 op) {
  u32 opc = (op >> 21) & 15;
  bool S = op & BIT(20);
  u32 rn = (op >> 16) & 15, rd = (op >> 12) & 15;
  bool c = (cpsr >> 29) & 1;
  u32 a, b;
  if (op & BIT(25)) {
    u32 rot = ((op >> 8) & 15) * 2;
    b = ror32(op & 0xFF, rot);
    if (rot) c = b >> 31;
    a = R(rn);
  } else {
    u32 rm = op & 15, type = (op >> 5) & 3;
    if (op & BIT(4)) {  // shift by register: PC reads as +12
      u32 amt = R((op >> 8) & 15) & 0xFF;
      u32 v = rm == 15 ? r[15] + 12 : R(rm);
      b = shiftOp(type, v, amt, false, c);
      a = rn == 15 ? r[15] + 12 : R(rn);
      cyc += 1;
    } else {
      b = shiftOp(type, R(rm), (op >> 7) & 31, true, c);
      a = R(rn);
    }
  }
  u32 res = alu(opc, a, b, S, c);
  if (opc >= 8 && opc <= 11) return;  // TST/TEQ/CMP/CMN
  if (rd == 15) {
    if (S) {
      restoreSpsr();
      r[15] = res & (inThumb() ? ~1u : ~3u);
      branched = true;
    } else {
      branch(res);
    }
  } else {
    r[rd] = res;
  }
}

void Cpu::doMsr(u32 op, u32 val) {
  u32 mask = 0;
  if (op & BIT(16)) mask |= 0x000000FF;
  if (op & BIT(17)) mask |= 0x0000FF00;
  if (op & BIT(18)) mask |= 0x00FF0000;
  if (op & BIT(19)) mask |= 0xFF000000;
  if (op & BIT(22)) {
    int i = bankIndex(cpsr & 0x1F);
    if (i) spsr[i] = (spsr[i] & ~mask) | (val & mask);
  } else {
    writeCpsr(val, mask);
  }
}

void Cpu::armMisc(u32 op) {
  u32 rd = (op >> 12) & 15, rm = op & 15;
  if ((op & 0x0FBF0FFF) == 0x010F0000) {  // MRS
    r[rd] = (op & BIT(22)) ? getSpsr() : cpsr;
    return;
  }
  if ((op & 0x0FB0FFF0) == 0x0120F000) {  // MSR <reg>
    doMsr(op, R(rm));
    return;
  }
  if ((op & 0x0FFFFFF0) == 0x012FFF10) {  // BX
    branchEx(R(rm));
    return;
  }
  if (!isArm9) { undefinedInstr(); return; }
  if ((op & 0x0FFFFFF0) == 0x012FFF30) {  // BLX <reg>
    u32 a = R(rm);
    r[14] = r[15] + 4;
    branchEx(a);
    return;
  }
  if ((op & 0x0FFF0FF0) == 0x016F0F10) {  // CLZ
    r[rd] = clz32(R(rm));
    return;
  }
  if ((op & 0x0F900FF0) == 0x01000050) {  // QADD / QSUB / QDADD / QDSUB
    u32 rn = (op >> 16) & 15;
    bool q = false;
    s32 m = (s32)R(rm), n = (s32)R(rn), res;
    switch ((op >> 21) & 3) {
      case 0: res = satAdd(m, n, q); break;
      case 1: res = satSub(m, n, q); break;
      case 2: { s32 d = satAdd(n, n, q); res = satAdd(m, d, q); break; }
      default: { s32 d = satAdd(n, n, q); res = satSub(m, d, q); break; }
    }
    if (q) cpsr |= BIT(27);
    r[rd] = (u32)res;
    return;
  }
  if ((op & 0x0F900090) == 0x01000080) {  // SMLAxy / SMLAWy / SMULWy / SMLALxy / SMULxy
    u32 d = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15;
    bool x = op & BIT(5), y = op & BIT(6);
    auto half = [](u32 v, bool top) { return (s32)(s16)(top ? v >> 16 : v & 0xFFFF); };
    switch ((op >> 21) & 3) {
      case 0: {  // SMLAxy
        s32 p = half(R(rm), x) * half(R(rs), y);
        bool C, V;
        r[d] = addWithCarry((u32)p, R(rn), 0, C, V);
        if (V) cpsr |= BIT(27);
        break;
      }
      case 1: {  // SMLAWy / SMULWy
        s32 p = (s32)(((s64)(s32)R(rm) * half(R(rs), y)) >> 16);
        if (x) {
          r[d] = (u32)p;
        } else {
          bool C, V;
          r[d] = addWithCarry((u32)p, R(rn), 0, C, V);
          if (V) cpsr |= BIT(27);
        }
        break;
      }
      case 2: {  // SMLALxy
        s64 acc = (s64)(((u64)r[d] << 32) | r[rn]);
        acc += (s64)half(R(rm), x) * half(R(rs), y);
        r[rn] = (u32)acc;
        r[d] = (u32)((u64)acc >> 32);
        break;
      }
      default:  // SMULxy
        r[d] = (u32)(half(R(rm), x) * half(R(rs), y));
        break;
    }
    return;
  }
  undefinedInstr();  // BKPT and anything else
}

void Cpu::armMultiply(u32 op) {
  u32 rm = op & 15, rs = (op >> 8) & 15;
  bool S = op & BIT(20), A = op & BIT(21);
  if (!(op & BIT(23))) {  // MUL / MLA
    u32 rd = (op >> 16) & 15, rn = (op >> 12) & 15;
    u32 res = R(rm) * R(rs);
    if (A) res += R(rn);
    r[rd] = res;
    if (S) setNZ(res);
    cyc += 2;
    return;
  }
  u32 hi = (op >> 16) & 15, lo = (op >> 12) & 15;
  bool sign = op & BIT(22);
  u64 res;
  if (sign) res = (u64)((s64)(s32)R(rm) * (s64)(s32)R(rs));
  else res = (u64)R(rm) * (u64)R(rs);
  if (A) res += ((u64)r[hi] << 32) | r[lo];
  r[lo] = (u32)res;
  r[hi] = (u32)(res >> 32);
  if (S) cpsr = (cpsr & 0x3FFFFFFFu) | ((u32)(res >> 63) << 31) | (res == 0 ? BIT(30) : 0u);
  cyc += 3;
}

void Cpu::armSwap(u32 op) {
  u32 rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
  u32 a = r[rn];
  if (op & BIT(22)) {
    u32 t = bus.read8(a);
    bus.write8(a, (u8)r[rm]);
    r[rd] = t;
  } else {
    u32 t = rdWord(a);
    bus.write32(a & ~3u, r[rm]);
    r[rd] = t;
  }
  cyc += 2;
}

void Cpu::armSingleTransfer(u32 op) {
  bool I = op & BIT(25), P = op & BIT(24), U = op & BIT(23);
  bool B = op & BIT(22), W = op & BIT(21), L = op & BIT(20);
  u32 rn = (op >> 16) & 15, rd = (op >> 12) & 15;
  u32 base = R(rn), off;
  if (!I) {
    off = op & 0xFFF;
  } else {
    bool c = (cpsr >> 29) & 1;
    off = shiftOp((op >> 5) & 3, R(op & 15), (op >> 7) & 31, true, c);
  }
  u32 wb = U ? base + off : base - off;
  u32 addr = P ? wb : base;
  if (L) {
    u32 val = B ? bus.read8(addr) : rdWord(addr);
    if (!P || W) r[rn] = wb;
    if (rd == 15) {
      if (isArm9) branchEx(val); else branch(val);
    } else {
      r[rd] = val;
    }
    cyc += 2;
  } else {
    u32 val = R(rd);
    if (rd == 15) val += 4;
    if (B) bus.write8(addr, (u8)val);
    else bus.write32(addr & ~3u, val);
    if (!P || W) r[rn] = wb;
    cyc += 1;
  }
}

void Cpu::armHalfTransfer(u32 op) {
  bool P = op & BIT(24), U = op & BIT(23), Imm = op & BIT(22);
  bool W = op & BIT(21), L = op & BIT(20);
  u32 rn = (op >> 16) & 15, rd = (op >> 12) & 15, sh = (op >> 5) & 3;
  u32 off = Imm ? (((op >> 4) & 0xF0) | (op & 0xF)) : R(op & 15);
  u32 base = R(rn);
  u32 wb = U ? base + off : base - off;
  u32 addr = P ? wb : base;
  if (L) {
    u32 val;
    if (sh == 1) {
      val = bus.read16(addr & ~1u);
      if (addr & 1) val = ror32(val, 8);
    } else if (sh == 2) {
      val = (u32)(s32)(s8)bus.read8(addr);
    } else if (addr & 1) {
      val = (u32)(s32)(s8)bus.read8(addr);
    } else {
      val = (u32)(s32)(s16)bus.read16(addr);
    }
    if (!P || W) r[rn] = wb;
    if (rd == 15) branch(val); else r[rd] = val;
    cyc += 2;
  } else if (sh == 1) {  // STRH
    u32 v = R(rd);
    bus.write16(addr & ~1u, (u16)v);
    if (!P || W) r[rn] = wb;
    cyc += 1;
  } else {  // LDRD / STRD (ARMv5TE only)
    if (!isArm9) { undefinedInstr(); return; }
    u32 a = addr & ~3u;
    if (sh == 2) {
      u32 lo = bus.read32(a), hi = bus.read32(a + 4);
      if (!P || W) r[rn] = wb;
      r[rd] = lo;
      r[rd + 1] = hi;
    } else {
      bus.write32(a, R(rd));
      bus.write32(a + 4, R(rd + 1));
      if (!P || W) r[rn] = wb;
    }
    cyc += 2;
  }
}

void Cpu::armBlockTransfer(u32 op) {
  bool P = op & BIT(24), U = op & BIT(23), S = op & BIT(22);
  bool W = op & BIT(21), L = op & BIT(20);
  u32 rn = (op >> 16) & 15, list = op & 0xFFFF;
  u32 base = r[rn];
  u32 count = popcnt(list);
  u32 n = list ? count : 16;  // empty list moves the base by 0x40
  u32 start, wb;
  if (U) { start = P ? base + 4 : base; wb = base + 4 * n; }
  else { start = P ? base - 4 * n : base - 4 * n + 4; wb = base - 4 * n; }

  u32 savedMode = cpsr & 0x1F;
  bool userBank = S && !(L && (list & BIT(15))) && savedMode != MODE_USR && savedMode != MODE_SYS;
  if (userBank) setMode(MODE_USR);

  u32 a = start & ~3u;
  bool havePC = false;
  u32 loadedPC = 0;
  if (list == 0) {
    if (!isArm9) {  // ARM7 transfers R15
      if (L) { loadedPC = bus.read32(a); havePC = true; }
      else bus.write32(a, r[15] + 12);
    }
  } else {
    for (int i = 0; i < 16; i++) {
      if (!(list & BIT(i))) continue;
      if (L) {
        u32 v = bus.read32(a);
        if (i == 15) { loadedPC = v; havePC = true; }
        else r[i] = v;
      } else {
        bus.write32(a, i == 15 ? r[15] + 12 : r[i]);
      }
      a += 4;
    }
  }
  if (userBank) setMode(savedMode);

  if (W && !(L && (list & BIT(rn)))) r[rn] = wb;
  if (havePC) {
    if (S) {
      restoreSpsr();
      r[15] = loadedPC & (inThumb() ? ~1u : ~3u);
      branched = true;
    } else if (isArm9) {
      branchEx(loadedPC);
    } else {
      branch(loadedPC);
    }
  }
  cyc += (int)n;
}

void Cpu::armBranch(u32 op) {
  s32 off = ((s32)(op << 8)) >> 6;
  if (op & BIT(24)) r[14] = r[15] + 4;
  branch(r[15] + 8 + off);
}

void Cpu::armCoproc(u32 op) {
  if (!isArm9 || ((op >> 8) & 15) != 15 || !(op & BIT(4))) { undefinedInstr(); return; }
  u32 opc1 = (op >> 21) & 7, crn = (op >> 16) & 15, rd = (op >> 12) & 15;
  u32 opc2 = (op >> 5) & 7, crm = op & 15;
  if (op & BIT(20)) {  // MRC
    u32 v = cp15Read ? cp15Read(crn, crm, opc1, opc2) : 0;
    if (rd == 15) cpsr = (cpsr & 0x0FFFFFFFu) | (v & 0xF0000000u);
    else r[rd] = v;
  } else {  // MCR
    if (cp15Write) cp15Write(crn, crm, opc1, opc2, R(rd));
  }
}

// ---------------------------------------------------------------------------
// Thumb
// ---------------------------------------------------------------------------

void Cpu::execThumb(u16 op) {
  switch (op >> 13) {
    case 0: {  // 000: shifts, add/sub
      u32 type = (op >> 11) & 3;
      u32 rd = op & 7, rs = (op >> 3) & 7;
      if (type == 3) {
        u32 v = (op & BIT(10)) ? (u32)((op >> 6) & 7) : r[(op >> 6) & 7];
        r[rd] = alu((op & BIT(9)) ? 2 : 4, r[rs], v, true, false);
      } else {
        bool c = (cpsr >> 29) & 1;
        u32 v = shiftOp(type, r[rs], (op >> 6) & 31, true, c);
        r[rd] = v;
        setNZ(v);
        setC(c);
      }
      return;
    }
    case 1: {  // 001: MOV/CMP/ADD/SUB imm8
      u32 rd = (op >> 8) & 7, imm = op & 0xFF;
      switch ((op >> 11) & 3) {
        case 0: r[rd] = alu(13, 0, imm, true, (cpsr >> 29) & 1); break;
        case 1: alu(10, r[rd], imm, true, false); break;
        case 2: r[rd] = alu(4, r[rd], imm, true, false); break;
        default: r[rd] = alu(2, r[rd], imm, true, false); break;
      }
      return;
    }
    case 2: {  // 010
      if ((op & 0xFC00) == 0x4000) {  // ALU
        u32 opc = (op >> 6) & 15, rd = op & 7, rs = (op >> 3) & 7;
        bool c = (cpsr >> 29) & 1;
        u32 a = r[rd], b = r[rs], res;
        switch (opc) {
          case 0: r[rd] = alu(0, a, b, true, c); break;
          case 1: r[rd] = alu(1, a, b, true, c); break;
          case 2: case 3: case 4: case 7: {
            u32 type = opc == 2 ? 0 : opc == 3 ? 1 : opc == 4 ? 2 : 3;
            res = shiftOp(type, a, b & 0xFF, false, c);
            r[rd] = res;
            setNZ(res);
            setC(c);
            cyc += 1;
            break;
          }
          case 5: r[rd] = alu(5, a, b, true, c); break;
          case 6: r[rd] = alu(6, a, b, true, c); break;
          case 8: alu(8, a, b, true, c); break;
          case 9: r[rd] = alu(2, 0, b, true, c); break;
          case 10: alu(10, a, b, true, c); break;
          case 11: alu(11, a, b, true, c); break;
          case 12: r[rd] = alu(12, a, b, true, c); break;
          case 13: res = a * b; r[rd] = res; setNZ(res); cyc += 2; break;
          case 14: r[rd] = alu(14, a, b, true, c); break;
          default: r[rd] = alu(15, a, b, true, c); break;
        }
        return;
      }
      if ((op & 0xFC00) == 0x4400) {  // hi register ops / BX / BLX
        u32 rd = (op & 7) | ((op >> 4) & 8), rs = (op >> 3) & 15;
        u32 b = R(rs);
        switch ((op >> 8) & 3) {
          case 0: {
            u32 v = R(rd) + b;
            if (rd == 15) branch(v); else r[rd] = v;
            break;
          }
          case 1: alu(10, R(rd), b, true, false); break;
          case 2:
            if (rd == 15) branch(b); else r[rd] = b;
            break;
          default:
            if (op & 0x80) {  // BLX <reg>
              if (!isArm9) { undefinedInstr(); return; }
              r[14] = (r[15] + 2) | 1;
            }
            branchEx(b);
            break;
        }
        return;
      }
      if ((op & 0xF800) == 0x4800) {  // LDR PC-relative
        u32 rd = (op >> 8) & 7;
        r[rd] = bus.read32(((r[15] + 4) & ~3u) + ((op & 0xFF) << 2));
        cyc += 2;
        return;
      }
      // 0101: load/store register offset
      u32 rd = op & 7;
      u32 a = r[(op >> 3) & 7] + r[(op >> 6) & 7];
      switch ((op >> 9) & 7) {
        case 0: bus.write32(a & ~3u, r[rd]); break;
        case 1: bus.write16(a & ~1u, (u16)r[rd]); break;
        case 2: bus.write8(a, (u8)r[rd]); break;
        case 3: r[rd] = (u32)(s32)(s8)bus.read8(a); break;
        case 4: r[rd] = rdWord(a); break;
        case 5: {
          u32 v = bus.read16(a & ~1u);
          r[rd] = (a & 1) ? ror32(v, 8) : v;
          break;
        }
        case 6: r[rd] = bus.read8(a); break;
        default:
          r[rd] = (a & 1) ? (u32)(s32)(s8)bus.read8(a) : (u32)(s32)(s16)bus.read16(a);
          break;
      }
      cyc += 1;
      return;
    }
    case 3: {  // 011: load/store imm offset
      u32 rd = op & 7, rb = r[(op >> 3) & 7], imm = (op >> 6) & 31;
      bool B = op & BIT(12), L = op & BIT(11);
      if (B) {
        u32 a = rb + imm;
        if (L) r[rd] = bus.read8(a); else bus.write8(a, (u8)r[rd]);
      } else {
        u32 a = rb + imm * 4;
        if (L) r[rd] = rdWord(a); else bus.write32(a & ~3u, r[rd]);
      }
      cyc += 1;
      return;
    }
    case 4: {
      if ((op & 0xF000) == 0x8000) {  // halfword imm offset
        u32 rd = op & 7, a = r[(op >> 3) & 7] + ((op >> 6) & 31) * 2;
        if (op & BIT(11)) {
          u32 v = bus.read16(a & ~1u);
          r[rd] = (a & 1) ? ror32(v, 8) : v;
        } else {
          bus.write16(a & ~1u, (u16)r[rd]);
        }
      } else {  // SP-relative
        u32 rd = (op >> 8) & 7, a = r[13] + (op & 0xFF) * 4;
        if (op & BIT(11)) r[rd] = rdWord(a); else bus.write32(a & ~3u, r[rd]);
      }
      cyc += 1;
      return;
    }
    case 5: {
      if ((op & 0xF000) == 0xA000) {  // ADD rd, PC/SP, #imm
        u32 rd = (op >> 8) & 7, imm = (op & 0xFF) * 4;
        r[rd] = (op & BIT(11)) ? r[13] + imm : ((r[15] + 4) & ~3u) + imm;
        return;
      }
      if ((op & 0xFF00) == 0xB000) {  // ADD/SUB SP
        u32 imm = (op & 0x7F) * 4;
        if (op & 0x80) r[13] -= imm; else r[13] += imm;
        return;
      }
      if ((op & 0xF600) == 0xB400) {  // PUSH / POP
        u32 list = op & 0xFF;
        bool R_ = op & BIT(8);
        if (!(op & BIT(11))) {  // PUSH
          u32 n = popcnt(list) + (R_ ? 1 : 0);
          u32 a = r[13] - 4 * n;
          r[13] = a;
          for (int i = 0; i < 8; i++)
            if (list & BIT(i)) { bus.write32(a, r[i]); a += 4; }
          if (R_) bus.write32(a, r[14]);
          cyc += (int)n;
        } else {  // POP
          u32 a = r[13];
          for (int i = 0; i < 8; i++)
            if (list & BIT(i)) { r[i] = bus.read32(a); a += 4; }
          if (R_) {
            u32 v = bus.read32(a);
            a += 4;
            r[13] = a;
            if (isArm9) branchEx(v); else branch(v);
          } else {
            r[13] = a;
          }
          cyc += (int)popcnt(list) + 1;
        }
        return;
      }
      undefinedInstr();  // BKPT etc.
      return;
    }
    case 6: {
      if ((op & 0xF000) == 0xC000) {  // LDMIA / STMIA
        u32 rb = (op >> 8) & 7, list = op & 0xFF;
        u32 a = r[rb];
        bool L = op & BIT(11);
        if (list == 0) {
          r[rb] = a + 0x40;
          return;
        }
        for (int i = 0; i < 8; i++) {
          if (!(list & BIT(i))) continue;
          if (L) r[i] = bus.read32(a & ~3u);
          else bus.write32(a & ~3u, r[i]);
          a += 4;
        }
        if (!(L && (list & BIT(rb)))) r[rb] = a;
        cyc += (int)popcnt(list);
        return;
      }
      u32 c = (op >> 8) & 15;  // conditional branch / SWI
      if (c == 15) { swi(op & 0xFF); return; }
      if (c == 14) { undefinedInstr(); return; }
      if (cond(c)) branch(r[15] + 4 + (u32)((s32)(s8)(op & 0xFF) * 2));
      return;
    }
    default: {  // 111
      u32 sel = (op >> 11) & 3;
      if (sel == 0) {  // B
        s32 off = ((s32)((u32)(op & 0x7FF) << 21)) >> 20;
        branch(r[15] + 4 + (u32)off);
      } else if (sel == 1) {  // BLX suffix (ARMv5)
        if (!isArm9 || (op & 1)) { undefinedInstr(); return; }
        u32 target = (r[14] + ((u32)(op & 0x7FF) << 1)) & ~3u;
        r[14] = (r[15] + 2) | 1;
        cpsr &= ~BIT(5);
        r[15] = target;
        branched = true;
      } else if (sel == 2) {  // BL prefix
        s32 off = ((s32)((u32)(op & 0x7FF) << 21)) >> 9;
        r[14] = r[15] + 4 + (u32)off;
      } else {  // BL suffix
        u32 target = r[14] + ((u32)(op & 0x7FF) << 1);
        r[14] = (r[15] + 2) | 1;
        branch(target);
      }
      return;
    }
  }
}
