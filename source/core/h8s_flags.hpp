#pragma once
/**
 * @file h8s_flags.hpp
 * @brief H8S/2350 Compatible Flag Helper Functions
 * 
 * Consolidated CCR flag calculation helpers for H8S/2350 instruction emulation.
 * All flag operations follow official H8S/2350 architecture specifications.
 */

#include <cstdint>

namespace MS2000 {

// Forward declaration for CCR structure
struct H8S2350Flags;

/**
 * @brief H8S/2350 compatible flag helper functions
 * 
 * These inline functions provide consistent CCR flag calculation across
 * all H8S instruction implementations (ALU, BCD, etc.)
 */
namespace H8SFlags {

    /**
     * @brief Set Z and N flags from 8-bit result
     * @param result 8-bit operation result
     * @param flags CCR flags structure to update
     */
    inline void setZN_8(uint8_t result, H8S2350Flags& flags) {
        flags.zero = (result == 0);
        flags.negative = (result & 0x80) != 0;
    }

    /**
     * @brief Clear carry and overflow flags (for logical operations)
     * @param flags CCR flags structure to update
     */
    inline void clearCV(H8S2350Flags& flags) {
        flags.carry = false;
        flags.overflow = false;
    }

    /**
     * @brief Calculate all flags for 8-bit ADD operation (H8S convention)
     * @param a First operand
     * @param b Second operand  
     * @param result Addition result (a + b)
     * @param flags CCR flags structure to update
     */
    inline void flags_add8(uint8_t a, uint8_t b, uint8_t result, H8S2350Flags& flags) {
        // Carry (C): carry out from bit7
        flags.carry = ((uint16_t)a + (uint16_t)b) > 0xFF;
        // Half-carry (H): carry from bit3->bit4
        flags.half_carry = ((a & 0x0F) + (b & 0x0F)) > 0x0F;
        // Overflow (V): same sign operands → different sign result
        flags.overflow = ((~(a ^ b) & (a ^ result)) & 0x80) != 0;
        setZN_8(result, flags);
    }

    /**
     * @brief Calculate all flags for 8-bit SUB/CMP operation (H8S convention)
     * @param a First operand (minuend)
     * @param b Second operand (subtrahend)
     * @param result Subtraction result (a - b)
     * @param flags CCR flags structure to update
     */
    inline void flags_sub8(uint8_t a, uint8_t b, uint8_t result, H8S2350Flags& flags) {
        // Carry (C): H8S SUB/CMP "no borrow" = 1, borrow = 0
        flags.carry = a >= b;
        // Half-carry (H): "no borrow from bit3" = 1  
        flags.half_carry = ((a & 0x0F) >= (b & 0x0F));
        // Overflow (V): different sign subtraction, result sign differs from a
        flags.overflow = (((a ^ b) & (a ^ result)) & 0x80) != 0;
        setZN_8(result, flags);
    }

    /**
     * @brief Apply DAA (Decimal Adjust for Addition) BCD correction
     * @param a Input value after binary addition
     * @param flags Input/output CCR flags (H and C used as input, all flags updated)
     * @return BCD corrected result
     */
    inline uint8_t daa8_apply(uint8_t a, H8S2350Flags& flags) {
        uint8_t corr = 0;
        const bool C_in = flags.carry;
        const bool H_in = flags.half_carry;

        if (((a & 0x0F) > 0x09) || H_in) corr |= 0x06;  // Lower nibble correction
        if ((a > 0x99) || C_in)        corr |= 0x60;  // Upper nibble correction

        const uint16_t res = (uint16_t)a + corr;
        const uint8_t result = (uint8_t)res;

        // Update flags after DAA
        flags.carry       = (res > 0xFF);                                   // New C
        flags.half_carry  = (((a & 0x0F) + (corr & 0x0F)) > 0x0F);        // New H
        flags.overflow    = false;                                          // V=0 for BCD
        setZN_8(result, flags);

        return result;
    }

    /**
     * @brief Apply DAS (Decimal Adjust for Subtraction) BCD correction
     * @param a Input value after binary subtraction
     * @param flags Input/output CCR flags (H and C used as input, all flags updated)
     * @return BCD corrected result
     */
    inline uint8_t das8_apply(uint8_t a, H8S2350Flags& flags) {
        uint8_t corr = 0;
        const bool borrow_in     = !flags.carry;        // C=1 → no-borrow
        const bool h_borrow_in   = !flags.half_carry;   // H=1 → no-borrow (lower nibble)

        if (((a & 0x0F) > 0x09) || h_borrow_in) corr |= 0x06;  // Nibble correction
        if ((a > 0x99) || borrow_in)           corr |= 0x60;  // Byte correction

        // Corrected subtraction
        const int16_t res16 = (int16_t)a - (int16_t)corr;
        const uint8_t result = (uint8_t)res16;

        // Update flags after DAS (H8S: C=1 → no-borrow)
        flags.carry       = ((uint8_t)a >= corr);                               // No-borrow?
        flags.half_carry  = ((a & 0x0F) >= (corr & 0x0F));                      // Nibble no-borrow
        flags.overflow    = false;                                              // V=0 for BCD
        setZN_8(result, flags);

        return result;
    }

} // namespace H8SFlags
} // namespace MS2000