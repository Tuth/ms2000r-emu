// FlashROM - MBM29LV800BA command state machine. Sources and approximations: flash_rom.h.
#include "flash_rom.h"
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace MS2000 {

namespace {
// Every flash event is rare and real, so it is always reported - but capped, and the
// cap announces itself when it binds (BUG87: a silent cap reads exactly like absence).
bool flashReportAllowed() {
    static unsigned n = 0;
    const unsigned cap = 64;
    ++n;
    if (n <= cap) return true;
    if (n == cap + 1)
        printf("[FLASH] REPORT CAP REACHED after %u events - FURTHER FLASH EVENTS ARE NOT "
               "SHOWN AND THIS IS NOT AN ABSENCE.\n", cap);
    return false;
}
} // namespace

FlashROM::FlashROM() { data_.assign(FLASH_SIZE, 0xFF); }

bool FlashROM::loadFromFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size > static_cast<std::streamsize>(FLASH_SIZE)) size = FLASH_SIZE;
    data_.assign(FLASH_SIZE, 0xFF);
    if (size > 0) file.read(reinterpret_cast<char*>(data_.data()), size);
    resetToRead(); bypass_ = false;
    return true;
}

bool FlashROM::saveToFile(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(data_.data()), data_.size());
    return true;
}

// BUG126: the state file pair. The mask file is one line, the sector mask in hex (SA0 =
// bit 0). Only the named sectors are ever copied back, so a firmware code sector the
// firmware never erased cannot be replaced from the state image.
static uint32_t readMask(const std::string& maskPath) {
    std::ifstream m(maskPath);
    if (!m) return 0;
    std::string line; std::getline(m, line);
    return uint32_t(std::strtoul(line.c_str(), nullptr, 16));
}

bool FlashROM::saveState(const std::string& imagePath, const std::string& maskPath) const {
    const uint32_t mask = (readMask(maskPath) | loaded_ | dirty_) & ((1u << NUM_SECTORS) - 1u);
    if (!mask) return true;
    const std::string tmp = imagePath + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data_.data()), data_.size());
        if (!f) return false;
    }
    std::remove(imagePath.c_str());
    if (std::rename(tmp.c_str(), imagePath.c_str()) != 0) return false;
    std::ofstream m(maskPath);
    if (!m) return false;
    char buf[16]; std::snprintf(buf, sizeof buf, "%05X", unsigned(mask));
    m << buf << "\n";
    return true;
}

uint32_t FlashROM::loadState(const std::string& imagePath, const std::string& maskPath) {
    const uint32_t mask = readMask(maskPath) & ((1u << NUM_SECTORS) - 1u);
    if (!mask) return 0;
    std::ifstream f(imagePath, std::ios::binary);
    if (!f) return 0;
    std::vector<uint8_t> img(FLASH_SIZE, 0xFF);
    f.read(reinterpret_cast<char*>(img.data()), img.size());
    if (f.gcount() != std::streamsize(FLASH_SIZE)) return 0;     // a short file is not a flash image
    for (uint32_t sa = 0; sa < NUM_SECTORS; ++sa) {
        if (!(mask & (1u << sa))) continue;
        uint32_t s = 0, e = 0;
        if (!sectorRange(sa, s, e)) continue;
        std::copy(img.begin() + s, img.begin() + e + 1, data_.begin() + s);
    }
    loaded_ = mask;
    return mask;
}

uint32_t FlashROM::loadStateFrom(uint32_t mask, const uint8_t* image, size_t bytes) {
    mask &= (1u << NUM_SECTORS) - 1u;
    if (!mask || !image || bytes < FLASH_SIZE || data_.size() < FLASH_SIZE) return 0;
    for (uint32_t sa = 0; sa < NUM_SECTORS; ++sa) {
        if (!(mask & (1u << sa))) continue;
        uint32_t s = 0, e = 0;
        if (!sectorRange(sa, s, e)) continue;
        std::copy(image + s, image + e + 1, data_.begin() + s);
    }
    loaded_ = mask;
    return mask;
}

// Table 3 (rendered p.13), Am29LV800BB bottom boot block, x8 column = CPU byte address.
bool FlashROM::sectorRange(uint32_t s, uint32_t& start, uint32_t& end) {
    switch (s) {
        case 0: start = 0x00000; end = 0x03FFF; return true;   // 16 KB
        case 1: start = 0x04000; end = 0x05FFF; return true;   //  8 KB
        case 2: start = 0x06000; end = 0x07FFF; return true;   //  8 KB
        case 3: start = 0x08000; end = 0x0FFFF; return true;   // 32 KB
        default:
            if (s >= NUM_SECTORS) return false;
            start = (s - 3) * 0x10000u; end = start + 0xFFFFu; return true;   // 64 KB
    }
}

uint32_t FlashROM::sectorOf(uint32_t a) {
    a &= FLASH_SIZE - 1;
    if (a < 0x04000) return 0;
    if (a < 0x06000) return 1;
    if (a < 0x08000) return 2;
    if (a < 0x10000) return 3;
    return 3 + (a >> 16);
}

bool FlashROM::eraseSector(uint32_t sector) {
    uint32_t s, e;
    if (!sectorRange(sector, s, e) || e >= data_.size()) return false;
    std::fill(data_.begin() + s, data_.begin() + e + 1, 0xFF);
    return true;
}

bool FlashROM::eraseChip() {
    std::fill(data_.begin(), data_.end(), 0xFF);
    return true;
}

uint16_t FlashROM::arrayWord(uint32_t w) const {
    const uint32_t a = (w << 1) & (FLASH_SIZE - 1);
    if (a + 1 >= data_.size()) return 0xFFFF;
    return uint16_t((data_[a] << 8) | data_[a + 1]);
}

void FlashROM::programWord(uint32_t w, uint16_t d, uint32_t pc) {
    const uint32_t a   = (w << 1) & (FLASH_SIZE - 1);
    const uint16_t old = arrayWord(w);
    const uint16_t now = uint16_t(old & d);       // p.18: program only clears bits
    if (a + 1 < data_.size()) { data_[a] = uint8_t(now >> 8); data_[a + 1] = uint8_t(now); }
    if (now != old) { dirty_ |= 1u << sectorOf(a); ++gen_; }   // BUG126
    if (d & ~old) {
        // p.18: asking for a 0 -> 1 "may halt the operation and cause the DQ5 bit to be
        // set to 1"; p.23: then "the system must write the reset command".
        mode_ = Mode::PROGRAM_FAILED; failData_ = d;
        if (flashReportAllowed())
            printf("[FLASH-PROGRAM-FAIL] 0x%06X: 0x%04X over 0x%04X asks for a 0->1 - DQ5=1 "
                   "until reset (PC=0x%06X)\n", a, d, old, pc);
    } else if (flashReportAllowed()) {
        printf("[FLASH-PROGRAM] 0x%06X: 0x%04X -> 0x%04X (PC=0x%06X)\n", a, old, now, pc);
    }
}

void FlashROM::eraseSectorAt(uint32_t w, uint32_t pc) {
    const uint32_t sa = sectorOf(w << 1);
    uint32_t s = 0, e = 0;
    sectorRange(sa, s, e);
    eraseSector(sa);
    dirty_ |= 1u << sa; ++gen_;                           // BUG126
    if (flashReportAllowed())
        printf("[FLASH-ERASE] sector SA%u 0x%05X-0x%05X erased (PC=0x%06X)\n", sa, s, e, pc);
}

void FlashROM::badSequence(uint32_t w, uint16_t d, uint32_t pc) {
    // p.17: "Writing incorrect address and data values or writing them in the improper
    // sequence resets the device to reading array data."
    if (flashReportAllowed())
        printf("[FLASH-BAD-SEQ] cycle %d: word 0x%05X = 0x%04X is not in Table 1 - device back "
               "to read array (PC=0x%06X)\n", cycle_, w, d, pc);
    resetToRead();
}

void FlashROM::busWrite(uint32_t w, uint16_t d, uint32_t pc) {
    w &= (FLASH_SIZE >> 1) - 1;                   // A18:A0
    const uint8_t  lo = uint8_t(d);               // note 4: DQ15-8 don't care
    const uint32_t a  = w & 0x7FF;                // note 5: A18-A11 don't care

    if (mode_ == Mode::PROGRAM_FAILED || mode_ == Mode::AUTOSELECT) {
        if (lo == 0xF0) {
            if (flashReportAllowed())
                printf("[FLASH-RESET] %s -> read array (PC=0x%06X)\n",
                       mode_ == Mode::AUTOSELECT ? "autoselect" : "program-failed", pc);
            resetToRead();
        } else if (flashReportAllowed()) {
            printf("[FLASH-IGNORED] word 0x%05X = 0x%04X while in %s - only reset (F0) leaves "
                   "it (p.17) (PC=0x%06X)\n", w, d,
                   mode_ == Mode::AUTOSELECT ? "autoselect" : "program-failed", pc);
        }
        return;
    }

    if (bypass_) {                                // Table 1, unlock bypass
        if (cycle_ == 0) {
            if (lo == 0xA0 || lo == 0x90) { cmd_ = lo; cycle_ = 1; }
            else badSequence(w, d, pc);
        } else if (cmd_ == 0xA0) {
            programWord(w, d, pc); cycle_ = 0; cmd_ = 0;
        } else {                                  // 90 then 00 = unlock bypass reset
            if (lo == 0x00) { bypass_ = false; if (flashReportAllowed())
                printf("[FLASH-BYPASS] exit (PC=0x%06X)\n", pc); }
            cycle_ = 0; cmd_ = 0;
        }
        return;
    }

    // The data cycle of a program is PA/PD: its value is data, never a reset.
    if (cycle_ == 3 && cmd_ == 0xA0) { cycle_ = 0; cmd_ = 0; programWord(w, d, pc); return; }

    if (lo == 0xF0) { resetToRead(); return; }    // p.17: reset, address don't care

    switch (cycle_) {
        case 0:
            if (a == 0x555 && lo == 0xAA) { cycle_ = 1; return; }
            if (lo == 0xB0 || lo == 0x30) {
                if (flashReportAllowed())
                    printf("[FLASH-IGNORED] erase %s with no erase in progress (instant-erase "
                           "approximation) (PC=0x%06X)\n", lo == 0xB0 ? "suspend" : "resume", pc);
                return;
            }
            badSequence(w, d, pc); return;
        case 1:
            if (a == 0x2AA && lo == 0x55) { cycle_ = 2; return; }
            badSequence(w, d, pc); return;
        case 2:
            if (a != 0x555) { badSequence(w, d, pc); return; }
            if (lo == 0x90) {
                mode_ = Mode::AUTOSELECT; cycle_ = 0;
                if (flashReportAllowed()) printf("[FLASH-AUTOSELECT] enter (PC=0x%06X)\n", pc);
                return;
            }
            if (lo == 0xA0 || lo == 0x80) { cmd_ = lo; cycle_ = 3; return; }
            if (lo == 0x20) {
                bypass_ = true; cycle_ = 0;
                if (flashReportAllowed()) printf("[FLASH-BYPASS] enter (PC=0x%06X)\n", pc);
                return;
            }
            badSequence(w, d, pc); return;
        case 3:                                   // cmd_ == 0x80: second unlock
            if (a == 0x555 && lo == 0xAA) { cycle_ = 4; return; }
            badSequence(w, d, pc); return;
        case 4:
            if (a == 0x2AA && lo == 0x55) { cycle_ = 5; return; }
            badSequence(w, d, pc); return;
        case 5:
            if (lo == 0x30) { eraseSectorAt(w, pc); resetToRead(); return; }   // SA/30
            if (a == 0x555 && lo == 0x10) {
                eraseChip(); resetToRead();
                dirty_ = (1u << NUM_SECTORS) - 1u; ++gen_;       // BUG126
                if (flashReportAllowed()) printf("[FLASH-ERASE] CHIP erased (PC=0x%06X)\n", pc);
                return;
            }
            badSequence(w, d, pc); return;
        default:
            badSequence(w, d, pc); return;
    }
}

uint8_t FlashROM::busReadByte(uint32_t cpuAddr) {
    cpuAddr &= FLASH_SIZE - 1;
    const bool odd = (cpuAddr & 1) != 0;          // odd byte = DQ7-0, even = DQ15-8
    switch (mode_) {
        case Mode::READ_ARRAY:
            return cpuAddr < data_.size() ? data_[cpuAddr] : 0xFF;
        case Mode::AUTOSELECT: {
            // Table 4 (p.14): selected by A1:A0 of the WORD address. DQ15-8 is X for the
            // manufacturer and protect codes; 0x00 is returned there, and that choice is
            // ours, not the page's. All sectors read unprotected (p.14: "shipped with all
            // sectors unprotected"; nothing in flash.bin protects one).
            const uint32_t sel = (cpuAddr >> 1) & 3;
            uint16_t v = 0x0000;
            if (sel == 0) v = MANUFACTURER_ID;
            else if (sel == 1) v = DEVICE_ID;
            else if (sel == 2) v = 0x0000;
            else if (flashReportAllowed())
                printf("[FLASH-AUTOSELECT] A1:A0 = 11 is not in Table 4 - read as 0x0000\n");
            return odd ? uint8_t(v) : uint8_t(v >> 8);
        }
        case Mode::PROGRAM_FAILED: {
            // p.22/23: DQ7 = complement of the datum, DQ6 toggles each read, DQ5 = 1.
            if (!odd) return data_[cpuAddr];
            toggle_ = !toggle_;
            uint8_t v = data_[cpuAddr] & 0x1F;
            v |= uint8_t((~failData_) & 0x80) | (toggle_ ? 0x40 : 0x00) | 0x20;
            return v;
        }
    }
    return 0xFF;
}

} // namespace MS2000
