#pragma once

#include <string>
#include <vector>
#include <map>
#include <fstream>

namespace MS2000
{
    // SYX üzenet típusok
    enum class SyxMessageType
    {
        FIRMWARE_DUMP,      // Teljes firmware dump
        PRESET_DUMP,        // Preset dump
        SYSTEM_DUMP,        // Rendszer beállítások
        BULK_DUMP,          // Bulk data dump
        REQUEST_DUMP,       // Dump kérés
        ACKNOWLEDGE,        // Nyugtázás
        UNKNOWN             // Ismeretlen típus
    };
    
    // SYX üzenet struktúra
    struct SyxMessage
    {
        SyxMessageType type;
        uint8_t device_id;
        uint8_t manufacturer_id;
        std::vector<uint8_t> data;
        uint32_t checksum;
        bool is_valid;
    };
    
    // Korg MS-2000 specifikus SYX konstansok
    namespace MS2000Syx
    {
        static constexpr uint8_t MANUFACTURER_ID = 0x42;  // Korg
        static constexpr uint8_t DEVICE_ID = 0x00;        // MS-2000
        static constexpr uint8_t MODEL_ID = 0x30;         // MS-2000 model
        
        // SYX üzenet típusok
        static constexpr uint8_t MSG_FIRMWARE_DUMP = 0x01;
        static constexpr uint8_t MSG_PRESET_DUMP = 0x02;
        static constexpr uint8_t MSG_SYSTEM_DUMP = 0x03;
        static constexpr uint8_t MSG_BULK_DUMP = 0x04;
        static constexpr uint8_t MSG_REQUEST_DUMP = 0x05;
        static constexpr uint8_t MSG_ACKNOWLEDGE = 0x06;
        
        // Firmware dump header
        static constexpr uint8_t FIRMWARE_HEADER[] = {0x42, 0x30, 0x00, 0x01};
        static constexpr uint8_t PRESET_HEADER[] = {0x42, 0x30, 0x00, 0x02};
        static constexpr uint8_t SYSTEM_HEADER[] = {0x42, 0x30, 0x00, 0x03};
    }
    
    // SYX Parser osztály
    class SyxParser
    {
    public:
        SyxParser();
        ~SyxParser() = default;
        
        // SYX fájl betöltés
        bool loadSyxFile(const std::string& file_path);
        bool loadSyxData(const std::vector<uint8_t>& syx_data);
        
        // SYX üzenetek feldolgozása
        std::vector<SyxMessage> parseMessages() const;
        SyxMessage parseMessage(const std::vector<uint8_t>& message_data) const;
        
        // Firmware kinyerés
        std::vector<uint8_t> extractFirmware() const;
        std::vector<uint8_t> extractPreset(int preset_number) const;
        std::vector<uint8_t> extractSystemSettings() const;
        
        // Validáció
        bool validateSyxFile() const;
        bool validateMessage(const SyxMessage& message) const;
        bool validateChecksum(const std::vector<uint8_t>& data) const;
        
        // Információk
        size_t getMessageCount() const { return m_messages.size(); }
        size_t getFirmwareSize() const { return m_firmware_size; }
        size_t getPresetCount() const { return m_preset_count; }
        bool hasFirmware() const { return m_has_firmware; }
        bool hasPresets() const { return m_has_presets; }
        
        // Debug és diagnosztika
        void dumpSyxInfo() const;
        void dumpMessage(const SyxMessage& message) const;
        void exportFirmware(const std::string& output_path) const;
        void exportPresets(const std::string& output_path) const;
        
    protected:
        // SYX adatok
        std::vector<uint8_t> m_syx_data;
        std::vector<SyxMessage> m_messages;
        
        // Kinyert adatok
        std::vector<uint8_t> m_firmware_data;
        std::map<int, std::vector<uint8_t>> m_preset_data;
        std::vector<uint8_t> m_system_data;
        
        // Metaadatok
        size_t m_firmware_size;
        size_t m_preset_count;
        bool m_has_firmware;
        bool m_has_presets;
        bool m_is_valid;
        
        // Parsing függvények
        void parseSyxData();
        SyxMessageType detectMessageType(const std::vector<uint8_t>& data) const;
        uint32_t calculateChecksum(const std::vector<uint8_t>& data) const;
        
        // Korg MS-2000 specifikus parsing
        bool parseMS2000Firmware(const SyxMessage& message);
        bool parseMS2000Preset(const SyxMessage& message);
        bool parseMS2000System(const SyxMessage& message);
        
        // Segédfüggvények
        std::vector<uint8_t> extractDataBlock(const std::vector<uint8_t>& message, size_t offset, size_t length) const;
        bool isValidMS2000Message(const std::vector<uint8_t>& data) const;
        std::string formatHexData(const std::vector<uint8_t>& data) const;
    };
    
    // Korg MS-2000 specifikus SYX parser
    class MS2000SyxParser : public SyxParser
    {
    public:
        MS2000SyxParser();
        ~MS2000SyxParser() = default;
        
        // MS-2000 specifikus funkciók
        bool loadMS2000SyxFile(const std::string& file_path);
        bool validateMS2000Syx() const;
        
        // MS-2000 firmware információk
        struct MS2000FirmwareInfo
        {
            std::string version;
            std::string build_date;
            uint32_t firmware_size;
            uint32_t checksum;
            bool is_valid;
        };
        
        MS2000FirmwareInfo getFirmwareInfo() const { return m_firmware_info; }
        
        // MS-2000 preset információk
        struct MS2000PresetInfo
        {
            std::string name;
            uint8_t preset_number;
            uint32_t checksum;
            bool is_valid;
        };
        
        std::vector<MS2000PresetInfo> getPresetInfo() const { return m_preset_info; }
        
        // MS-2000 rendszer beállítások
        struct MS2000SystemInfo
        {
            uint8_t master_volume;
            uint8_t midi_channel;
            uint8_t transpose;
            uint8_t tuning;
            bool is_valid;
        };
        
        MS2000SystemInfo getSystemInfo() const { return m_system_info; }
        
        // MS-2000 specifikus export
        bool exportMS2000Firmware(const std::string& output_path) const;
        bool exportMS2000Presets(const std::string& output_path) const;
        bool exportMS2000System(const std::string& output_path) const;
        
    private:
        // MS-2000 specifikus adatok
        MS2000FirmwareInfo m_firmware_info;
        std::vector<MS2000PresetInfo> m_preset_info;
        MS2000SystemInfo m_system_info;
        
        // MS-2000 specifikus parsing
        void parseMS2000FirmwareInfo();
        void parseMS2000PresetInfo();
        void parseMS2000SystemInfo();
        
        // MS-2000 validáció
        bool validateMS2000Firmware() const;
        bool validateMS2000Presets() const;
        bool validateMS2000System() const;
    };
}
