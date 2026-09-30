// SUBX instruction tests (P1.6): reg-reg and imm8 forms
// Validates CCR rules including Z-retention on zero result

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../core/h8s2350_emulator.h"

using namespace MS2000;

static void write_bytes(H8S2350Emulator& emu, uint32_t addr, const std::vector<uint8_t>& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) emu.writeByte(addr + (uint32_t)i, bytes[i]);
}

static void test_subx_rr_basic() {
    H8S2350Emulator emu;
    emu.reset();
    emu.getRegisters().pc = 0x010000;

    // R1 = 0x10, R2 = 0x01, C=0
    emu.getRegisters().r[1] = (emu.getRegisters().r[1] & 0xFFFFFF00u) | 0x10u;
    emu.getRegisters().r[2] = (emu.getRegisters().r[2] & 0xFFFFFF00u) | 0x01u;
    auto& f = emu.getFlags();
    f.carry = false;
    f.zero = false;
    f.negative = false;
    f.overflow = false;
    f.half_carry = false;

    // SUBX R2,R1 => 0x1E, (rs<<4)|rd = 0x1E, 0x21
    write_bytes(emu, 0x010000, {0x1E, 0x21});
    emu.step();

    // Result: 0x10 - 0x01 - 0 = 0x0F
    assert((emu.getRegisters().r[1] & 0xFFu) == 0x0Fu);
    // Flags: C=0 (no borrow), H= true (0x0 < 0x1), N=0, Z=0, V depends -> should be 0 here
    assert(f.carry == false);
    assert(f.half_carry == true);
    assert(f.negative == false);
    assert(f.zero == false);
    assert(f.overflow == false);
    // PC advanced by 2
    assert(emu.getRegisters().pc == 0x010002);
}

static void test_subx_rr_with_borrow_and_z_keep() {
    H8S2350Emulator emu;
    emu.reset();
    emu.getRegisters().pc = 0x020000;

    // R0 = 0x00, R3 = 0x00; C_in = 1
    emu.getRegisters().r[0] = (emu.getRegisters().r[0] & 0xFFFFFF00u);
    emu.getRegisters().r[3] = (emu.getRegisters().r[3] & 0xFFFFFF00u);
    auto& f = emu.getFlags();
    f.carry = true;  // C_in = 1
    f.zero = true;   // Z_before = 1 (should be retained if result==0)
    f.negative = false;
    f.overflow = false;
    f.half_carry = false;

    // SUBX R3,R0 => 0x1E, 0x30
    write_bytes(emu, 0x020000, {0x1E, 0x30});
    emu.step();

    // Result: 0x00 - 0x00 - 1 = 0xFF
    assert((emu.getRegisters().r[0] & 0xFFu) == 0xFFu);
    // Flags: borrow true, half-borrow true, N=1, V=0, Z=0 (since res != 0)
    assert(f.carry == true);
    assert(f.half_carry == true);
    assert(f.negative == true);
    assert(f.overflow == false);
    assert(f.zero == false);
}

static void test_subx_rr_zero_result_z_retained() {
    H8S2350Emulator emu;
    emu.reset();
    emu.getRegisters().pc = 0x030000;

    // R4 = 0x12, R5 = 0x12; C_in = 0; Z_before = 1
    emu.getRegisters().r[4] = (emu.getRegisters().r[4] & 0xFFFFFF00u) | 0x12u;
    emu.getRegisters().r[5] = (emu.getRegisters().r[5] & 0xFFFFFF00u) | 0x12u;
    auto& f = emu.getFlags();
    f.carry = false;
    f.zero = true;   // Z_before should be kept since res==0
    f.negative = false;
    f.overflow = false;
    f.half_carry = false;

    // SUBX R5,R4 => 0x1E, 0x54
    write_bytes(emu, 0x030000, {0x1E, 0x54});
    emu.step();

    // Result: 0x12 - 0x12 - 0 = 0x00
    assert((emu.getRegisters().r[4] & 0xFFu) == 0x00u);
    // Z stays 1 (retained), other flags cleared reasonably
    assert(f.zero == true);
    // Borrow false, half-borrow false, N=0, V=0
    assert(f.carry == false);
    assert(f.half_carry == false);
    assert(f.negative == false);
    assert(f.overflow == false);
}

static void test_subx_imm8_basic() {
    H8S2350Emulator emu;
    emu.reset();
    emu.getRegisters().pc = 0x040000;

    // Rd = R1 = 0x05; SUBX #0x03, R1 with C_in=0 => 0x02
    emu.getRegisters().r[1] = (emu.getRegisters().r[1] & 0xFFFFFF00u) | 0x05u;
    auto& f = emu.getFlags();
    f.carry = false;
    f.zero = false;
    f.negative = false;
    f.overflow = false;
    f.half_carry = false;

    // (0xB0 | rd)=0xB1, imm8=0x03
    write_bytes(emu, 0x040000, {0xB1, 0x03});
    emu.step();

    assert((emu.getRegisters().r[1] & 0xFFu) == 0x02u);
    assert(f.zero == false);
    assert(f.carry == false);
    assert(f.half_carry == false);
    assert(f.negative == false);
}

static void test_subx_half_borrow_edges() {
    // Case 1: dst=0x0F, src=0x00, Cin=0 -> res=0x0E, H=0, C=0, Z=0, N=0 (manual half-borrow)
    {
        H8S2350Emulator emu; emu.reset(); emu.getRegisters().pc = 0x050000;
        emu.getRegisters().r[0] = (emu.getRegisters().r[0] & 0xFFFFFF00u) | 0x0Fu;
        auto& f = emu.getFlags(); f.carry=false; f.zero=false; f.negative=false; f.overflow=false; f.half_carry=false;
        // SUBX #1,R0
        write_bytes(emu, 0x050000, { (uint8_t)(0xB0 | 0x00), 0x01 });
        emu.step();
        assert((emu.getRegisters().r[0] & 0xFFu) == 0x0E);
        assert(f.half_carry == false);
        assert(f.carry == false);
        assert(f.zero == false);
        assert(f.negative == false);
    }

    // Case 2: dst=0x10, src=0x0F, Cin=1 -> res=0x00, H=1, C=0, Z retained (manual half-borrow)
    {
        H8S2350Emulator emu; emu.reset(); emu.getRegisters().pc = 0x050100;
        emu.getRegisters().r[1] = (emu.getRegisters().r[1] & 0xFFFFFF00u) | 0x10u;
        auto& f = emu.getFlags(); f.carry=true; f.zero=true; // Cin=1, Z_before=1
        // Place src (0x0F) in R2 and use reg-reg SUBX R2,R1
        emu.getRegisters().r[2] = (emu.getRegisters().r[2] & 0xFFFFFF00u) | 0x0Fu;
        write_bytes(emu, 0x050100, { 0x1E, (uint8_t)((2<<4)|1) });
        emu.step();
        assert((emu.getRegisters().r[1] & 0xFFu) == 0x00u);
        assert(f.half_carry == true);
        assert(f.carry == false);
        assert(f.zero == true); // retained
        assert(f.negative == false);
    }

    // Case 3: dst=0x00, src=0x00, Cin=1 -> res=0xFF, H=1, C=1, Z=0, N=1
    {
        H8S2350Emulator emu; emu.reset(); emu.getRegisters().pc = 0x050200;
        emu.getRegisters().r[3] = (emu.getRegisters().r[3] & 0xFFFFFF00u);
        auto& f = emu.getFlags(); f.carry=true; f.zero=true; // Cin=1, Z_before=1
        write_bytes(emu, 0x050200, { (uint8_t)(0xB0 | 0x03), 0x00 });
        emu.step();
        assert((emu.getRegisters().r[3] & 0xFFu) == 0xFFu);
        assert(f.half_carry == true);
        assert(f.carry == true);
        assert(f.zero == false);
        assert(f.negative == true);
    }
}

static void test_subx_multiprecision_z_property() {
    // Chain 3 bytes: ensure Z stays 1 only if all byte results are zero
    // Scenario A: all zeros -> Z remains 1
    {
        H8S2350Emulator emu; emu.reset(); emu.getRegisters().pc = 0x060000;
        auto& f = emu.getFlags(); f.carry = false; f.zero = true;

        // Rd=R4,R5,R6 all 0; subtract 0 with Cin=0 three times
        emu.getRegisters().r[4] &= 0xFFFFFF00u;
        emu.getRegisters().r[5] &= 0xFFFFFF00u;
        emu.getRegisters().r[6] &= 0xFFFFFF00u;
        write_bytes(emu, 0x060000, { (uint8_t)(0xB0|4), 0x00, (uint8_t)(0xB0|5), 0x00, (uint8_t)(0xB0|6), 0x00 });
        emu.step(); emu.step(); emu.step();
        assert(f.zero == true);
    }

    // Scenario B: middle byte non-zero -> Z becomes 0 and stays 0
    {
        H8S2350Emulator emu; emu.reset(); emu.getRegisters().pc = 0x060100;
        auto& f = emu.getFlags(); f.carry = false; f.zero = true;

        emu.getRegisters().r[4] &= 0xFFFFFF00u;
        emu.getRegisters().r[5] = (emu.getRegisters().r[5] & 0xFFFFFF00u) | 0x01u; // middle becomes non-zero
        emu.getRegisters().r[6] &= 0xFFFFFF00u;
        write_bytes(emu, 0x060100, { (uint8_t)(0xB0|4), 0x00, (uint8_t)(0xB0|5), 0x00, (uint8_t)(0xB0|6), 0x00 });
        emu.step(); emu.step(); emu.step();
        assert(f.zero == false);
    }
}

static void test_subx_parity_imm_vs_reg() {
    // Compare SUBX Rr,Rd vs SUBX #imm8,Rd for same inputs
    H8S2350Emulator emu1; emu1.reset(); emu1.getRegisters().pc = 0x070000;
    H8S2350Emulator emu2; emu2.reset(); emu2.getRegisters().pc = 0x070100;

    // Setup: Rd=R2=0x37, Rr=R5=0x2A, Cin=1, Z_before=1
    emu1.getRegisters().r[2] = (emu1.getRegisters().r[2] & 0xFFFFFF00u) | 0x37u;
    emu1.getRegisters().r[5] = (emu1.getRegisters().r[5] & 0xFFFFFF00u) | 0x2Au;
    emu2.getRegisters().r[2] = (emu2.getRegisters().r[2] & 0xFFFFFF00u) | 0x37u;
    auto& f1 = emu1.getFlags(); f1.carry=true; f1.zero=true;
    auto& f2 = emu2.getFlags(); f2.carry=true; f2.zero=true;

    // Reg-reg
    write_bytes(emu1, 0x070000, { 0x1E, (uint8_t)((5<<4)|2) });
    emu1.step();
    // Imm8
    write_bytes(emu2, 0x070100, { (uint8_t)(0xB0|2), 0x2A });
    emu2.step();

    assert((emu1.getRegisters().r[2] & 0xFFu) == (emu2.getRegisters().r[2] & 0xFFu));
    auto& f1a = emu1.getFlags(); auto& f2a = emu2.getFlags();
    assert(f1a.carry == f2a.carry);
    assert(f1a.half_carry == f2a.half_carry);
    assert(f1a.zero == f2a.zero);
    assert(f1a.negative == f2a.negative);
    assert(f1a.overflow == f2a.overflow);
}

void run_subx_tests() {
    test_subx_rr_basic();
    test_subx_rr_with_borrow_and_z_keep();
    test_subx_rr_zero_result_z_retained();
    test_subx_imm8_basic();
    test_subx_half_borrow_edges();
    test_subx_multiprecision_z_property();
    test_subx_parity_imm_vs_reg();
    std::cout << "[SUBX] All tests passed\n";
}

int main() {
    try {
        run_subx_tests();
        std::cout << "[SUBX] Test program completed successfully" << std::endl;
        return 0;
    } catch (...) {
        std::cerr << "[SUBX] Test program failed" << std::endl;
        return 1;
    }
}
