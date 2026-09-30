// Manual-derived ALU oracle for DIFFREF. The reference core never computes H and has an
// inverted ADD V, so for the arithmetic group the judge is THIS, written from the RENDERED
// pages of REJ09B0139 (H8S/2600 software manual):
//   ADD.B p.48 / ADD.W p.49 / ADD.L p.50 (H = carry at bit 3/11/27, C = carry at 7/15/31)
//   ADDX p.52, SUBX p.238 (Z: previous value when the result is zero, else 0 - also HM
//   REJ09B0330 Table A.1 note [5]; the ADDX page of REJ09B0139 omits it, the chip manual rules)
//   CMP.B/W/L p.91-93, SUB.B/W/L p.233/235/236 (H/C = borrow at bit 3/11/27, 7/15/31)
//   DEC.B/W/L p.98-100, INC.B/W/L p.116-118 (H and C unchanged; V per the Notes:
//   H'7FFF+1, H'7FFF+2, H'7FFE+2 overflow; H'8000-1, H'8000-2, H'8001-2 overflow)
//   NEG.B/W/L p.155-157 (0 - Rd: H/C = borrow at bit 3/11/27, 7/15/31)
// Register fields: HM REJ09B0330 Appendix A.2 legend - byte 0-7 RnH / 8-15 RnL,
// word 0-7 Rn / 8-15 En, long 0-7 ERn (bit 3 of a long field must be 0).
#pragma once
#include <cstdint>

struct OracleOut { uint32_t er[8]; uint8_t ccr; uint32_t pc; };

namespace oracle_detail {
inline uint8_t  rb(const uint32_t* er, int f) { return f < 8 ? uint8_t(er[f & 7] >> 8) : uint8_t(er[f & 7]); }
inline void     wb(uint32_t* er, int f, uint8_t v) { if (f < 8) er[f & 7] = (er[f & 7] & 0xFFFF00FFu) | (uint32_t(v) << 8); else er[f & 7] = (er[f & 7] & 0xFFFFFF00u) | v; }
inline uint16_t rw(const uint32_t* er, int f) { return f < 8 ? uint16_t(er[f & 7]) : uint16_t(er[f & 7] >> 16); }
inline void     ww(uint32_t* er, int f, uint16_t v) { if (f < 8) er[f & 7] = (er[f & 7] & 0xFFFF0000u) | v; else er[f & 7] = (er[f & 7] & 0x0000FFFFu) | (uint32_t(v) << 16); }
// generic add/sub of width bits (8/16/32) with carry-in; H at bit (bits-5)
struct R { uint32_t res; bool h, n, z, v, c; };
inline R add(uint32_t a, uint32_t b, int bits, bool cin)
{
    const uint64_t m = (bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1);
    const uint64_t s = uint64_t(a & m) + uint64_t(b & m) + (cin ? 1 : 0);
    const uint32_t res = uint32_t(s & m), sign = 1u << (bits - 1);
    const uint64_t hm = (1ull << (bits - 4)) - 1;
    R r; r.res = res; r.c = (s >> bits) & 1; r.h = ((a & hm) + (b & hm) + (cin ? 1 : 0)) > hm;
    r.n = (res & sign) != 0; r.z = res == 0; r.v = ((~(a ^ b) & (a ^ res)) & sign) != 0; return r;
}
inline R sub(uint32_t a, uint32_t b, int bits, bool cin)
{
    const uint64_t m = (bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1);
    const uint32_t res = uint32_t((uint64_t(a & m) - uint64_t(b & m) - (cin ? 1 : 0)) & m), sign = 1u << (bits - 1);
    const uint64_t hm = (1ull << (bits - 4)) - 1;
    R r; r.res = res; r.c = uint64_t(a & m) < uint64_t(b & m) + (cin ? 1 : 0); r.h = (a & hm) < (b & hm) + (cin ? 1 : 0);
    r.n = (res & sign) != 0; r.z = res == 0; r.v = (((a ^ b) & (a ^ res)) & sign) != 0; return r;
}
inline uint8_t put(uint8_t ccr, const R& r, bool setH, bool setC, bool zSticky)
{
    uint8_t c = uint8_t(ccr & ~0x0Eu);
    if (setH) c = uint8_t((c & ~0x20u) | (r.h ? 0x20 : 0));
    if (setC) c = uint8_t((c & ~0x01u) | (r.c ? 0x01 : 0));
    const bool z = zSticky ? ((ccr & 0x04) && r.z) : r.z;
    return uint8_t(c | (r.n ? 8 : 0) | (z ? 4 : 0) | (r.v ? 2 : 0));
}
} // namespace

// returns false when the instruction is not in the oracle's scope
inline bool aluOracle(const uint8_t* code, const uint32_t* erIn, uint8_t ccrIn, uint32_t pc, OracleOut& o)
{
    using namespace oracle_detail;
    for (int i = 0; i < 8; ++i) o.er[i] = erIn[i];
    o.ccr = ccrIn; o.pc = pc + 2;
    const uint8_t b0 = code[0], b1 = code[1], hi = b1 >> 4, lo = b1 & 15;
    const bool C = ccrIn & 1;
    R r;
    switch (b0 >> 4) {
        case 0x8: r = add(rb(o.er, b0 & 15), b1, 8, false); wb(o.er, b0 & 15, uint8_t(r.res)); o.ccr = put(ccrIn, r, true, true, false); return true;
        case 0x9: r = add(rb(o.er, b0 & 15), b1, 8, C);     wb(o.er, b0 & 15, uint8_t(r.res)); o.ccr = put(ccrIn, r, true, true, true);  return true;
        case 0xA: r = sub(rb(o.er, b0 & 15), b1, 8, false);                                     o.ccr = put(ccrIn, r, true, true, false); return true;
        case 0xB: r = sub(rb(o.er, b0 & 15), b1, 8, C);     wb(o.er, b0 & 15, uint8_t(r.res)); o.ccr = put(ccrIn, r, true, true, true);  return true;
    }
    switch (b0) {
        case 0x08: r = add(rb(o.er, lo), rb(o.er, hi), 8, false); wb(o.er, lo, uint8_t(r.res)); o.ccr = put(ccrIn, r, true, true, false); return true;
        case 0x0E: r = add(rb(o.er, lo), rb(o.er, hi), 8, C);     wb(o.er, lo, uint8_t(r.res)); o.ccr = put(ccrIn, r, true, true, true);  return true;
        case 0x18: r = sub(rb(o.er, lo), rb(o.er, hi), 8, false); wb(o.er, lo, uint8_t(r.res)); o.ccr = put(ccrIn, r, true, true, false); return true;
        case 0x1C: r = sub(rb(o.er, lo), rb(o.er, hi), 8, false);                               o.ccr = put(ccrIn, r, true, true, false); return true;
        case 0x1E: r = sub(rb(o.er, lo), rb(o.er, hi), 8, C);     wb(o.er, lo, uint8_t(r.res)); o.ccr = put(ccrIn, r, true, true, true);  return true;
        case 0x09: r = add(rw(o.er, lo), rw(o.er, hi), 16, false); ww(o.er, lo, uint16_t(r.res)); o.ccr = put(ccrIn, r, true, true, false); return true;
        case 0x19: r = sub(rw(o.er, lo), rw(o.er, hi), 16, false); ww(o.er, lo, uint16_t(r.res)); o.ccr = put(ccrIn, r, true, true, false); return true;
        case 0x1D: r = sub(rw(o.er, lo), rw(o.er, hi), 16, false);                                o.ccr = put(ccrIn, r, true, true, false); return true;
        case 0x0A:
            if (hi == 0) { r = add(rb(o.er, lo), 1, 8, false); wb(o.er, lo, uint8_t(r.res)); o.ccr = put(ccrIn, r, false, false, false); return true; }
            if ((hi & 8) && !(lo & 8)) { r = add(o.er[lo], o.er[hi & 7], 32, false); o.er[lo] = r.res; o.ccr = put(ccrIn, r, true, true, false); return true; }
            return false;
        case 0x1A:
            if (hi == 0) { r = sub(rb(o.er, lo), 1, 8, false); wb(o.er, lo, uint8_t(r.res)); o.ccr = put(ccrIn, r, false, false, false); return true; }
            if ((hi & 8) && !(lo & 8)) { r = sub(o.er[lo], o.er[hi & 7], 32, false); o.er[lo] = r.res; o.ccr = put(ccrIn, r, true, true, false); return true; }
            return false;
        case 0x1F:
            if ((hi & 8) && !(lo & 8)) { r = sub(o.er[lo], o.er[hi & 7], 32, false); o.ccr = put(ccrIn, r, true, true, false); return true; }
            return false;
        case 0x0B: case 0x1B: {
            const bool inc = b0 == 0x0B;
            if (hi == 0x5 || hi == 0xD) { r = inc ? add(rw(o.er, lo), hi == 5 ? 1 : 2, 16, false) : sub(rw(o.er, lo), hi == 5 ? 1 : 2, 16, false);
                ww(o.er, lo, uint16_t(r.res)); o.ccr = put(ccrIn, r, false, false, false); return true; }
            if ((hi == 0x7 || hi == 0xF) && !(lo & 8)) { r = inc ? add(o.er[lo], hi == 7 ? 1 : 2, 32, false) : sub(o.er[lo], hi == 7 ? 1 : 2, 32, false);
                o.er[lo] = r.res; o.ccr = put(ccrIn, r, false, false, false); return true; }
            return false;
        }
        case 0x17:
            if (hi == 0x8) { r = sub(0, rb(o.er, lo), 8, false);  wb(o.er, lo, uint8_t(r.res));  o.ccr = put(ccrIn, r, true, true, false); return true; }
            if (hi == 0x9) { r = sub(0, rw(o.er, lo), 16, false); ww(o.er, lo, uint16_t(r.res)); o.ccr = put(ccrIn, r, true, true, false); return true; }
            if (hi == 0xB && !(lo & 8)) { r = sub(0, o.er[lo], 32, false); o.er[lo] = r.res; o.ccr = put(ccrIn, r, true, true, false); return true; }
            return false;
        case 0x79: {
            const uint16_t imm = uint16_t((code[2] << 8) | code[3]); o.pc = pc + 4;
            if (hi == 1) { r = add(rw(o.er, lo), imm, 16, false); ww(o.er, lo, uint16_t(r.res)); o.ccr = put(ccrIn, r, true, true, false); return true; }
            if (hi == 2) { r = sub(rw(o.er, lo), imm, 16, false);                                o.ccr = put(ccrIn, r, true, true, false); return true; }
            if (hi == 3) { r = sub(rw(o.er, lo), imm, 16, false); ww(o.er, lo, uint16_t(r.res)); o.ccr = put(ccrIn, r, true, true, false); return true; }
            return false;
        }
        case 0x7A: {
            const uint32_t imm = (uint32_t(code[2]) << 24) | (uint32_t(code[3]) << 16) | (uint32_t(code[4]) << 8) | code[5]; o.pc = pc + 6;
            if (lo & 8) return false;
            if (hi == 1) { r = add(o.er[lo], imm, 32, false); o.er[lo] = r.res; o.ccr = put(ccrIn, r, true, true, false); return true; }
            if (hi == 2) { r = sub(o.er[lo], imm, 32, false);                  o.ccr = put(ccrIn, r, true, true, false); return true; }
            if (hi == 3) { r = sub(o.er[lo], imm, 32, false); o.er[lo] = r.res; o.ccr = put(ccrIn, r, true, true, false); return true; }
            return false;
        }
    }
    return false;
}
