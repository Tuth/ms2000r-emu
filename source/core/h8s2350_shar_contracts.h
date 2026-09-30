#pragma once
#include <cstdint>

namespace H8S { namespace SHAR {
    // Arithmetic right shift by 1 with sign extend
    // Encoding primaries in this tree (no collision with SUBX 0x1E):
    constexpr uint8_t PRIMARY_B = 0x1B; // SHAR.B
    constexpr uint8_t PRIMARY_W = 0x1C; // SHAR.W
    constexpr uint8_t PRIMARY_L = 0x1D; // SHAR.L

    // Operand field guard: both nibbles must be 0..7 (3-bit reg indices)
    constexpr inline bool valid_rr(uint8_t op2) { return (op2 & 0x88) == 0; }

    struct ResultFlags {
        bool C, Z, N, V, H;
    };

    template<typename T>
    inline ResultFlags flags_after(T before, T after, uint8_t width_bits) {
        ResultFlags f{};
        f.C = (before & static_cast<T>(1)) != 0;                                      // LSB shifted out
        f.Z = (after == static_cast<T>(0));                                            // zero result
        f.N = (after & (static_cast<T>(1) << (width_bits - 1))) != 0;                  // MSB
        f.V = false;                                                                   // shifts clear V (confirm in manual)
        f.H = false;                                                                   // H unaffected
        return f;
    }

    // Helpers to perform arithmetic right shift by 1 with sign extend
    inline uint8_t  arith_shr_b(uint8_t  x) { return static_cast<uint8_t>((x >> 1) | (x & 0x80)); }
    inline uint16_t arith_shr_w(uint16_t x) { return static_cast<uint16_t>((x >> 1) | (x & 0x8000)); }
    inline uint32_t arith_shr_l(uint32_t x) { return static_cast<uint32_t>((x >> 1) | (x & 0x80000000u)); }
}}

// --- Compile-time hygiene with SUBX (enabled by project option) ---
#if defined(H8S_HAS_SHAR_SSOT)
  #include "h8s2350_contracts.h" // for SUBX SSoT
  static_assert(H8S::SHAR::PRIMARY_B != H8S::SUBX::RR_PRIMARY, "Opcode clash: SHAR.B vs SUBX RR_PRIMARY");
  static_assert(H8S::SHAR::PRIMARY_W != H8S::SUBX::RR_PRIMARY, "Opcode clash: SHAR.W vs SUBX RR_PRIMARY");
  static_assert(H8S::SHAR::PRIMARY_L != H8S::SUBX::RR_PRIMARY, "Opcode clash: SHAR.L vs SUBX RR_PRIMARY");
#endif

