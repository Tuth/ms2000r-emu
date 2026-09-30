#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>

namespace MS2000
{
    // Firmware fájl típusok
    enum class FirmwareType
    {
        BINARY,     // Raw binary firmware
        OMF,        // Object Module Format
        HEX,        // Intel HEX format
        S19,        // Motorola S-Record
        UNKNOWN
    };
    
    // Firmware header információ
    struct FirmwareInfo
    {
        FirmwareType type;
        uint32_t start_address;
        uint32_t end_address;
        uint32_t entry_point;
        uint32_t checksum;
        std::string version;
        std::string build_date;
        bool is_valid;
    };
    
    // Firmware loader osztály
    class FirmwareLoader
    {
    public:
        FirmwareLoader();
        ~FirmwareLoader() = default;
        
        // Firmware betöltés
        bool loadFirmware(const std::string& file_path);
        bool loadFirmwareFromMemory(const std::vector<uint8_t>& firmware_data);
        
        // Firmware validáció
        bool validateFirmware() const;
        bool verifyChecksum() const;
        
        // Firmware információk
        FirmwareInfo getFirmwareInfo() const { return m_firmware_info; }
        std::vector<uint8_t> getFirmwareData() const { return m_firmware_data; }
        bool isFirmwareLoaded() const { return m_firmware_loaded; }
        
        // Firmware mentés
        bool saveFirmware(const std::string& file_path) const;
        bool exportFirmware(const std::string& file_path, FirmwareType type) const;
        
        // Debug és diagnosztika
        void dumpFirmwareInfo() const;
        void dumpFirmwareHex(uint32_t start_addr, uint32_t length) const;
        void disassembleFirmware(uint32_t start_addr, uint32_t length) const;
        
    private:
        // Firmware adatok
        std::vector<uint8_t> m_firmware_data;
        FirmwareInfo m_firmware_info;
        bool m_firmware_loaded;
        
        // Fájl típus detektálás
        FirmwareType detectFirmwareType(const std::string& file_path) const;
        FirmwareType detectFirmwareTypeFromData(const std::vector<uint8_t>& data) const;
        
        // Fájl típus specifikus betöltők
        bool loadBinaryFirmware(const std::string& file_path);
        bool loadOMFFirmware(const std::string& file_path);
        bool loadHexFirmware(const std::string& file_path);
        bool loadS19Firmware(const std::string& file_path);
        
        // Fájl típus specifikus mentők
        bool saveBinaryFirmware(const std::string& file_path) const;
        bool saveOMFFirmware(const std::string& file_path) const;
        bool saveHexFirmware(const std::string& file_path) const;
        bool saveS19Firmware(const std::string& file_path) const;
        
        // Firmware analízis
        void analyzeFirmware();
        void extractFirmwareInfo();
        uint32_t calculateChecksum() const;
        
        // Segédfüggvények
        bool isValidAddress(uint32_t address) const;
        bool isValidFirmwareSize(size_t size) const;
        std::string formatHexAddress(uint32_t address) const;
        std::string formatHexData(const std::vector<uint8_t>& data) const;
    };
    
    // Korg MS-2000 specifikus firmware loader
    class MS2000FirmwareLoader : public FirmwareLoader
    {
    public:
        MS2000FirmwareLoader();
        ~MS2000FirmwareLoader() = default;
        
        // MS-2000 specifikus firmware betöltés
        bool loadMS2000Firmware(const std::string& file_path);
        
        // MS-2000 firmware validáció
        bool validateMS2000Firmware() const;
        
        // MS-2000 firmware információk
        struct MS2000FirmwareInfo
        {
            std::string model;           // "MS-2000"
            std::string version;         // Firmware verzió
            std::string build_date;      // Build dátum
            uint32_t firmware_size;      // Firmware méret
            uint32_t checksum;           // Checksum
            bool has_arpeggiator;        // Arpeggiátor támogatás
            bool has_effects;            // Effektek támogatás
            bool has_midi;               // MIDI támogatás
            uint8_t max_polyphony;       // Maximum polyfónia
            bool is_valid;               // Érvényes firmware
        };
        
        MS2000FirmwareInfo getMS2000FirmwareInfo() const { return m_ms2000_info; }
        
        // MS-2000 firmware mentés
        bool saveMS2000Firmware(const std::string& file_path) const;
        
        // MS-2000 firmware backup/restore
        bool createFirmwareBackup(const std::string& backup_path) const;
        bool restoreFirmwareFromBackup(const std::string& backup_path);
        
    private:
        // MS-2000 specifikus információk
        MS2000FirmwareInfo m_ms2000_info;
        
        // MS-2000 firmware analízis
        void analyzeMS2000Firmware();
        void extractMS2000FirmwareInfo();
        
        // MS-2000 firmware validáció
        bool validateMS2000Checksum() const;
        bool validateMS2000Structure() const;
        bool validateMS2000EntryPoints() const;
        
        // MS-2000 specifikus konstansok
        static constexpr uint32_t MS2000_FIRMWARE_MIN_SIZE = 0x1000;  // 4KB minimum
        static constexpr uint32_t MS2000_FIRMWARE_MAX_SIZE = 0x10000; // 64KB maximum
        static constexpr uint32_t MS2000_FIRMWARE_START = 0x0000;
        static constexpr uint32_t MS2000_FIRMWARE_END = 0x3FFF;
        
        // MS-2000 firmware signature-ek
        static constexpr uint8_t MS2000_SIGNATURE[] = {0x4B, 0x4F, 0x52, 0x47}; // "KORG"
        static constexpr uint8_t MS2000_MODEL_SIGNATURE[] = {0x4D, 0x53, 0x32, 0x30, 0x30, 0x30}; // "MS2000"
    };
}
