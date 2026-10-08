// FlashROM - the MBM29LV800BA on KOD-A30411 (IC10), CS0, CPU 0x000000-0x0FFFFF.
//
// REWRITTEN 2026-09-23 (BUG102). The previous class was an Intel-style fiction
// (READ_STATUS 0x70, CLEAR_STATUS 0x50, "page program 0x40", "unified 64 KB sectors",
// ID 04/D5) and the CPU never reached it anyway: h8s2350_emulator.cpp wrote straight
// into the array "to allow firmware to proceed". Every rule below is from a RENDERED
// page of AM29LV800DB-90EC-AMD.pdf (the Am29LV800B, the AMD-compatible part) or from
// flash.bin itself, which is Tier 1:
//
//   p.11  the command register occupies no addressable location; power-up = read array
//   p.13  Table 3, Am29LV800BB BOTTOM boot sector addresses (x8 column = CPU address)
//   p.14  Table 4, autoselect codes: A1:A0 = 00 manufacturer, 01 device, 10 protect;
//         DQ15-8 of the manufacturer and protect codes are X (don't care)
//   p.17  wrong address/data or a wrong sequence resets to read array; F0 = reset,
//         address don't care; autoselect is left only by reset
//   p.18  program can only clear bits: "Only erase operations can convert a 0 to a 1"
//   p.21  Table 1 command definitions; note 4 DQ15-8 don't care in command cycles,
//         note 5 A18-A11 don't care for unlock and command cycles
//   p.22  DQ7 Data# polling (complement of the datum while programming, 0 while erasing)
//   p.23  DQ6 toggles while busy; DQ5 = 1 on failure; reset returns to read array
//
// FROM THE FIRMWARE (flash.bin, Tier 1), and these settle what the datasheet leaves open:
//   * WORD MODE. The command routines copied from 0x000200 to DRAM 0x400000 by the loop
//     at 0x00048A write MOV.W R0,@0x0000AAAA and MOV.W R0,@0x5554 - CPU byte addresses
//     that are word addresses 0x5555 / 0x2AAA, i.e. 0x555 / 0x2AA under note 5. So the
//     chip is x16 and CPU A1 drives flash A0.
//   * THE PART. 0x021176 reads the IDs and tests CMP.B #0x04 (manufacturer = FUJITSU,
//     not AMD's 01h) and CMP.B #0x5B (device low byte = BOTTOM boot). So autoselect
//     returns manufacturer 0x04 - the firmware's own expectation - and device 0x225B
//     (Table 4; the firmware checks only the low byte, 22h is the datasheet's value).
//
// STATED APPROXIMATION: embedded program/erase complete INSTANTLY. Real times are
// microseconds (program) to about a second (erase); this changes WHEN the firmware's
// DQ7 poll sees completion, not WHAT it sees. Erase suspend/resume and the sector-erase
// timeout window for queueing extra sectors are therefore not modelled; B0/30 written
// with nothing in progress are reported and ignored.
#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace MS2000 {

class FlashROM {
public:
    static constexpr uint32_t FLASH_SIZE  = 0x100000;   // 1 MB = 512 K x 16
    static constexpr uint32_t NUM_SECTORS = 19;         // Table 3, SA0-SA18
    static constexpr uint8_t  MANUFACTURER_ID = 0x04;   // what 0x021176 compares against
    static constexpr uint16_t DEVICE_ID       = 0x225B; // Table 4, bottom boot, word mode

    enum class Mode { READ_ARRAY, AUTOSELECT, PROGRAM_FAILED };

    FlashROM();

    // ---- host side (loader, --flash-dump, harness) - never a bus cycle ----
    bool loadFromFile(const std::string& filename);
    bool saveToFile(const std::string& filename) const;
    void resize(uint32_t newSize, uint8_t fillValue = 0xFF) {
        if (newSize > FLASH_SIZE) newSize = FLASH_SIZE;
        data_.resize(newSize, fillValue);
    }
    uint32_t size() const { return static_cast<uint32_t>(data_.size()); }
    uint8_t& operator[](uint32_t idx) { return data_[idx]; }            // raw array
    const uint8_t& operator[](uint32_t idx) const { return data_[idx]; }
    uint8_t* data() { return data_.data(); }
    const uint8_t* data() const { return data_.data(); }
    uint8_t* begin() { return data_.data(); }
    uint8_t* end() { return data_.data() + data_.size(); }
    const uint8_t* begin() const { return data_.data(); }
    const uint8_t* end() const { return data_.data() + data_.size(); }
    bool eraseSector(uint32_t sector);       // host tool: SA index per Table 3
    bool eraseChip();
    static bool sectorRange(uint32_t sector, uint32_t& start, uint32_t& end);  // CPU bytes
    static uint32_t sectorOf(uint32_t cpuAddr);

    // ---- CPU bus side ----
    // One 16-bit bus cycle. wordAddr = CPU address >> 1 (A18:A0), data = D15:D0.
    void    busWrite(uint32_t wordAddr, uint16_t data, uint32_t pc);
    // What the CPU sees at a byte address (big-endian: even = DQ15-8, odd = DQ7-0).
    uint8_t busReadByte(uint32_t cpuAddr);
    // PERF-129: busReadByte()'s READ_ARRAY case, inline for the instruction-fetch path (the
    // out-of-line call was 2.6-3 % of the emulation thread). Any other mode takes the full path.
    uint8_t busReadByteFast(uint32_t cpuAddr) {
        if (mode_ == Mode::READ_ARRAY) { cpuAddr &= FLASH_SIZE - 1; return cpuAddr < data_.size() ? data_[cpuAddr] : 0xFF; }
        return busReadByte(cpuAddr);
    }
    Mode    mode() const { return mode_; }

    // BUG126 (2026-09-26): the flash is NON-VOLATILE. Sectors the firmware erased or
    // programmed through the bus (Global WRITE, program WRITE, ...) are remembered in
    // dirtySectors(); the runner keeps them across runs in a STATE file beside the ROM
    // (never flash.bin itself - that image stays the sacred dump).
    uint32_t dirtySectors() const { return dirty_; }
    uint32_t writeGeneration() const { return gen_; }   // DEV-FLASH-1: +1 on every erase / program that changed the array
    // Save the current array + the cumulative sector mask (mask | whatever the file had).
    bool     saveState(const std::string& imagePath, const std::string& maskPath) const;
    // Copy the sectors named in the mask file from the state image into the array.
    // Returns the mask applied (0 = no state).
    uint32_t loadState(const std::string& imagePath, const std::string& maskPath);
    // VST3-2: the same state held by a host project instead of files. stateMask() = the sectors that are
    // not factory any more (taken from a state at start | written this run); loadStateFrom() copies the
    // sectors named in mask from a full-size image (FLASH_SIZE bytes). Returns the mask applied.
    uint32_t stateMask() const { return (loaded_ | dirty_) & ((1u << NUM_SECTORS) - 1u); }
    uint32_t loadStateFrom(uint32_t mask, const uint8_t* image, size_t bytes);

private:
    std::vector<uint8_t> data_;
    Mode     mode_   = Mode::READ_ARRAY;
    int      cycle_  = 0;        // position inside an unlock/command sequence
    uint8_t  cmd_    = 0;        // the command byte of the sequence in progress (A0/80)
    bool     bypass_ = false;    // unlock bypass (Table 1, "20")
    bool     bypassResetArmed_ = false;
    uint16_t failData_ = 0;      // the datum whose program failed (DQ7 = its complement)
    bool     toggle_ = false;    // DQ6 while in PROGRAM_FAILED
    uint32_t gen_    = 0;        // DEV-FLASH-1
    uint32_t dirty_  = 0;        // BUG126: sectors changed by bus erase/program this run
    uint32_t loaded_ = 0;        // BUG126: sectors taken from the state file at start

    uint16_t arrayWord(uint32_t wordAddr) const;
    void     programWord(uint32_t wordAddr, uint16_t data, uint32_t pc);
    void     eraseSectorAt(uint32_t wordAddr, uint32_t pc);
    void     resetToRead() { mode_ = Mode::READ_ARRAY; cycle_ = 0; cmd_ = 0; }
    void     badSequence(uint32_t wordAddr, uint16_t data, uint32_t pc);
};

} // namespace MS2000
