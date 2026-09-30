#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace MS2000 {

class H8S2350Emulator;
class DSP56362Emulator;

struct FirmwareHeader {
    uint32_t reset_vector;
    uint32_t code_size;
    uint32_t data_size;
    uint32_t entry_point;
    uint16_t version;
    uint16_t checksum;
};

class MS2000FirmwareLoader {
public:
    MS2000FirmwareLoader();
    ~MS2000FirmwareLoader();
    
    // Load firmware from file
    bool loadFirmwareFile(const std::string& filename);
    
    // Install firmware into emulators
    bool installH8SFirmware(H8S2350Emulator* h8s);
    bool installDSPFirmware(DSP56362Emulator* dsp);
    
    // Firmware analysis
    bool analyzeFirmware();
    void printFirmwareInfo() const;
    
    // Get firmware information
    const FirmwareHeader& getHeader() const { return m_header; }
    const std::vector<uint8_t>& getFirmwareData() const { return m_firmware_data; }
    
    // Validate firmware
    bool validateChecksum() const;
    bool isValidMS2000Firmware() const;
    
private:
    std::vector<uint8_t> m_firmware_data;
    FirmwareHeader m_header;
    bool m_firmware_loaded;
    std::string m_firmware_path;
    
    // Firmware parsing
    bool parseHeader();
    bool extractCodeSection(std::vector<uint8_t>& code_data) const;
    bool extractDataSection(std::vector<uint8_t>& data_data) const;
    
    // Memory mapping
    uint32_t mapH8SAddress(uint32_t address) const;
    bool isValidH8SAddress(uint32_t address) const;
    
    // Checksum calculation
    uint16_t calculateChecksum() const;
};

// H8S Memory Map (MS2000 specific)
namespace H8SMemoryMap {
    // Flash ROM (Program memory)
    constexpr uint32_t FLASH_BASE = 0x000000;
    constexpr uint32_t FLASH_SIZE = 0x100000;  // 1MB
    constexpr uint32_t FLASH_END = FLASH_BASE + FLASH_SIZE - 1;
    
    // RAM (Data memory)
    constexpr uint32_t RAM_BASE = 0x100000;
    constexpr uint32_t RAM_SIZE = 0x80000;     // 512KB
    constexpr uint32_t RAM_END = RAM_BASE + RAM_SIZE - 1;
    
    // I/O Registers
    constexpr uint32_t IO_BASE = 0x200000;
    constexpr uint32_t IO_SIZE = 0x10000;      // 64KB
    constexpr uint32_t IO_END = IO_BASE + IO_SIZE - 1;
    
    // HPI Interface
    constexpr uint32_t HPI_BASE = 0x210000;
    constexpr uint32_t HPI_SIZE = 0x1000;      // 4KB
    constexpr uint32_t HPI_END = HPI_BASE + HPI_SIZE - 1;
    
    // Reset vector location
    constexpr uint32_t RESET_VECTOR_ADDR = 0x000000;
    
    // Interrupt vectors
    constexpr uint32_t IRQ_VECTOR_BASE = 0x000010;
    constexpr uint32_t IRQ_VECTOR_SIZE = 0x100;
}

// DSP56362 Memory Map (MS2000 specific)
namespace DSPMemoryMap {
    // Program memory (X:)
    constexpr uint32_t PROGRAM_BASE = 0x000000;
    constexpr uint32_t PROGRAM_SIZE = 0x100000;  // 1MB
    
    // Data memory (Y:)
    constexpr uint32_t DATA_BASE = 0x100000;
    constexpr uint32_t DATA_SIZE = 0x100000;     // 1MB
    
    // HPI registers
    constexpr uint32_t HPI_HPIC = 0x200000;      // HPI Control
    constexpr uint32_t HPI_HPIR = 0x200001;      // HPI Data/Address
    constexpr uint32_t HPI_HPIA = 0x200002;      // HPI Address
}

} // namespace MS2000