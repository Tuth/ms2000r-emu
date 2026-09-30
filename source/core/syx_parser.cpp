#include "syx_parser.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace MS2000
{
    // SYX Parser implementáció
    SyxParser::SyxParser()
        : m_firmware_size(0)
        , m_preset_count(0)
        , m_has_firmware(false)
        , m_has_presets(false)
        , m_is_valid(false)
    {
    }
    
    bool SyxParser::loadSyxFile(const std::string& file_path)
    {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open SYX file: " << file_path << std::endl;
            return false;
        }
        
        // Fájl méret meghatározása
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Fájl betöltése
        m_syx_data.resize(file_size);
        file.read(reinterpret_cast<char*>(m_syx_data.data()), file_size);
        file.close();
        
        // SYX adatok feldolgozása
        parseSyxData();
        
        return m_is_valid;
    }
    
    bool SyxParser::loadSyxData(const std::vector<uint8_t>& syx_data)
    {
        m_syx_data = syx_data;
        parseSyxData();
        return m_is_valid;
    }
    
    void SyxParser::parseSyxData()
    {
        m_messages.clear();
        m_is_valid = false;
        
        if (m_syx_data.empty()) {
            return;
        }
        
        // SYX üzenetek keresése (F0 ... F7)
        size_t pos = 0;
        while (pos < m_syx_data.size()) {
            // SysEx start keresése
            while (pos < m_syx_data.size() && m_syx_data[pos] != 0xF0) {
                pos++;
            }
            
            if (pos >= m_syx_data.size()) {
                break;
            }
            
            size_t start_pos = pos;
            
            // SysEx end keresése
            while (pos < m_syx_data.size() && m_syx_data[pos] != 0xF7) {
                pos++;
            }
            
            if (pos >= m_syx_data.size()) {
                break;
            }
            
            size_t end_pos = pos;
            
            // Üzenet kinyerése
            std::vector<uint8_t> message_data(
                m_syx_data.begin() + start_pos,
                m_syx_data.begin() + end_pos + 1
            );
            
            // Üzenet feldolgozása
            SyxMessage message = parseMessage(message_data);
            if (message.is_valid) {
                m_messages.push_back(message);
            }
            
            pos++;
        }
        
        // Kinyert adatok feldolgozása
        for (const auto& message : m_messages) {
            switch (message.type) {
                case SyxMessageType::FIRMWARE_DUMP:
                    parseMS2000Firmware(message);
                    break;
                case SyxMessageType::PRESET_DUMP:
                    parseMS2000Preset(message);
                    break;
                case SyxMessageType::SYSTEM_DUMP:
                    parseMS2000System(message);
                    break;
                default:
                    break;
            }
        }
        
        m_is_valid = !m_messages.empty();
    }
    
    SyxMessage SyxParser::parseMessage(const std::vector<uint8_t>& message_data) const
    {
        SyxMessage message;
        message.is_valid = false;
        
        if (message_data.size() < 8) {
            return message;
        }
        
        // SysEx start ellenőrzése
        if (message_data[0] != 0xF0) {
            return message;
        }
        
        // SysEx end ellenőrzése
        if (message_data.back() != 0xF7) {
            return message;
        }
        
        // Manufacturer ID ellenőrzése (Korg = 0x42)
        if (message_data[1] != 0x42) {
            return message;
        }
        
        // Device ID ellenőrzése (MS-2000 = 0x30)
        if (message_data[2] != 0x30) {
            return message;
        }
        
        // Üzenet típus meghatározása
        message.type = detectMessageType(message_data);
        message.manufacturer_id = message_data[1];
        message.device_id = message_data[2];
        
        // Adatok kinyerése (SysEx start/end nélkül)
        message.data.assign(
            message_data.begin() + 3,
            message_data.end() - 1
        );
        
        // Checksum kiszámítása
        message.checksum = calculateChecksum(message.data);
        message.is_valid = true;
        
        return message;
    }
    
    SyxMessageType SyxParser::detectMessageType(const std::vector<uint8_t>& data) const
    {
        if (data.size() < 4) {
            return SyxMessageType::UNKNOWN;
        }
        
        // MS-2000 specifikus üzenet típusok
        uint8_t message_type = data[3];
        
        switch (message_type) {
            case 0x01:
                return SyxMessageType::FIRMWARE_DUMP;
            case 0x02:
                return SyxMessageType::PRESET_DUMP;
            case 0x03:
                return SyxMessageType::SYSTEM_DUMP;
            case 0x04:
                return SyxMessageType::BULK_DUMP;
            case 0x05:
                return SyxMessageType::REQUEST_DUMP;
            case 0x06:
                return SyxMessageType::ACKNOWLEDGE;
            default:
                return SyxMessageType::UNKNOWN;
        }
    }
    
    uint32_t SyxParser::calculateChecksum(const std::vector<uint8_t>& data) const
    {
        uint32_t checksum = 0;
        for (uint8_t byte : data) {
            checksum += byte;
        }
        return checksum & 0x7F; // 7-bit checksum
    }
    
    bool SyxParser::parseMS2000Firmware(const SyxMessage& message)
    {
        if (message.type != SyxMessageType::FIRMWARE_DUMP) {
            return false;
        }
        
        // Firmware adatok kinyerése
        m_firmware_data = message.data;
        m_firmware_size = m_firmware_data.size();
        m_has_firmware = true;
        
        return true;
    }
    
    bool SyxParser::parseMS2000Preset(const SyxMessage& message)
    {
        if (message.type != SyxMessageType::PRESET_DUMP) {
            return false;
        }
        
        if (message.data.size() < 2) {
            return false;
        }
        
        // Preset szám kinyerése
        uint8_t preset_number = message.data[0];
        
        // Preset adatok kinyerése
        std::vector<uint8_t> preset_data(
            message.data.begin() + 1,
            message.data.end()
        );
        
        m_preset_data[preset_number] = preset_data;
        m_preset_count = m_preset_data.size();
        m_has_presets = true;
        
        return true;
    }
    
    bool SyxParser::parseMS2000System(const SyxMessage& message)
    {
        if (message.type != SyxMessageType::SYSTEM_DUMP) {
            return false;
        }
        
        // Rendszer beállítások kinyerése
        m_system_data = message.data;
        
        return true;
    }
    
    std::vector<uint8_t> SyxParser::extractFirmware() const
    {
        return m_firmware_data;
    }
    
    std::vector<uint8_t> SyxParser::extractPreset(int preset_number) const
    {
        auto it = m_preset_data.find(preset_number);
        if (it != m_preset_data.end()) {
            return it->second;
        }
        return std::vector<uint8_t>();
    }
    
    std::vector<uint8_t> SyxParser::extractSystemSettings() const
    {
        return m_system_data;
    }
    
    bool SyxParser::validateSyxFile() const
    {
        return m_is_valid;
    }
    
    bool SyxParser::validateMessage(const SyxMessage& message) const
    {
        if (!message.is_valid) {
            return false;
        }
        
        // Checksum validáció
        uint32_t calculated_checksum = calculateChecksum(message.data);
        return calculated_checksum == message.checksum;
    }
    
    bool SyxParser::validateChecksum(const std::vector<uint8_t>& data) const
    {
        if (data.size() < 1) {
            return false;
        }
        
        // Utolsó byte a checksum
        uint8_t stored_checksum = data.back();
        std::vector<uint8_t> data_without_checksum(
            data.begin(),
            data.end() - 1
        );
        
        uint32_t calculated_checksum = calculateChecksum(data_without_checksum);
        return (calculated_checksum & 0x7F) == stored_checksum;
    }
    
    void SyxParser::dumpSyxInfo() const
    {
        std::cout << "=== SYX File Information ===" << std::endl;
        std::cout << "File size: " << m_syx_data.size() << " bytes" << std::endl;
        std::cout << "Message count: " << m_messages.size() << std::endl;
        std::cout << "Has firmware: " << (m_has_firmware ? "Yes" : "No") << std::endl;
        std::cout << "Has presets: " << (m_has_presets ? "Yes" : "No") << std::endl;
        std::cout << "Firmware size: " << m_firmware_size << " bytes" << std::endl;
        std::cout << "Preset count: " << m_preset_count << std::endl;
        std::cout << "Valid: " << (m_is_valid ? "Yes" : "No") << std::endl;
        std::cout << std::endl;
        
        // Üzenetek listázása
        for (size_t i = 0; i < m_messages.size(); i++) {
            std::cout << "Message " << i << ":" << std::endl;
            dumpMessage(m_messages[i]);
            std::cout << std::endl;
        }
    }
    
    void SyxParser::dumpMessage(const SyxMessage& message) const
    {
        std::cout << "  Type: ";
        switch (message.type) {
            case SyxMessageType::FIRMWARE_DUMP:
                std::cout << "Firmware Dump";
                break;
            case SyxMessageType::PRESET_DUMP:
                std::cout << "Preset Dump";
                break;
            case SyxMessageType::SYSTEM_DUMP:
                std::cout << "System Dump";
                break;
            case SyxMessageType::BULK_DUMP:
                std::cout << "Bulk Dump";
                break;
            case SyxMessageType::REQUEST_DUMP:
                std::cout << "Request Dump";
                break;
            case SyxMessageType::ACKNOWLEDGE:
                std::cout << "Acknowledge";
                break;
            default:
                std::cout << "Unknown";
                break;
        }
        std::cout << std::endl;
        
        std::cout << "  Manufacturer ID: 0x" << std::hex << (int)message.manufacturer_id << std::endl;
        std::cout << "  Device ID: 0x" << std::hex << (int)message.device_id << std::endl;
        std::cout << "  Data size: " << std::dec << message.data.size() << " bytes" << std::endl;
        std::cout << "  Checksum: 0x" << std::hex << message.checksum << std::endl;
        std::cout << "  Valid: " << (message.is_valid ? "Yes" : "No") << std::endl;
        
        // Első 16 byte hex dump
        if (!message.data.empty()) {
            std::cout << "  Data (first 16 bytes): " << formatHexData(message.data) << std::endl;
        }
    }
    
    void SyxParser::exportFirmware(const std::string& output_path) const
    {
        if (!m_has_firmware) {
            std::cerr << "Error: No firmware data to export" << std::endl;
            return;
        }
        
        std::ofstream file(output_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot create output file: " << output_path << std::endl;
            return;
        }
        
        file.write(reinterpret_cast<const char*>(m_firmware_data.data()), m_firmware_data.size());
        file.close();
        
        std::cout << "Firmware exported to: " << output_path << std::endl;
    }
    
    void SyxParser::exportPresets(const std::string& output_path) const
    {
        if (!m_has_presets) {
            std::cerr << "Error: No preset data to export" << std::endl;
            return;
        }
        
        std::ofstream file(output_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot create output file: " << output_path << std::endl;
            return;
        }
        
        for (const auto& preset : m_preset_data) {
            // Preset szám
            file.write(reinterpret_cast<const char*>(&preset.first), sizeof(preset.first));
            
            // Preset adatok
            file.write(reinterpret_cast<const char*>(preset.second.data()), preset.second.size());
        }
        
        file.close();
        
        std::cout << "Presets exported to: " << output_path << std::endl;
    }
    
    std::string SyxParser::formatHexData(const std::vector<uint8_t>& data) const
    {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        
        size_t count = std::min(data.size(), size_t(16));
        for (size_t i = 0; i < count; i++) {
            ss << std::setw(2) << (int)data[i];
            if (i < count - 1) {
                ss << " ";
            }
        }
        
        if (data.size() > 16) {
            ss << " ...";
        }
        
        return ss.str();
    }
    
    // MS2000SyxParser implementáció
    MS2000SyxParser::MS2000SyxParser()
    {
        // MS2000 specifikus inicializálás
    }
    
    bool MS2000SyxParser::loadMS2000SyxFile(const std::string& file_path)
    {
        if (!loadSyxFile(file_path)) {
            return false;
        }
        
        // MS-2000 specifikus validáció
        if (!validateMS2000Syx()) {
            return false;
        }
        
        // MS-2000 specifikus információk kinyerése
        parseMS2000FirmwareInfo();
        parseMS2000PresetInfo();
        parseMS2000SystemInfo();
        
        return true;
    }
    
    bool MS2000SyxParser::validateMS2000Syx() const
    {
        // MS-2000 specifikus validáció
        for (const auto& message : m_messages) {
            if (message.manufacturer_id != 0x42 || message.device_id != 0x30) {
                return false;
            }
        }
        
        return true;
    }
    
    void MS2000SyxParser::parseMS2000FirmwareInfo()
    {
        if (!m_has_firmware) {
            return;
        }
        
        // Firmware információk kinyerése
        m_firmware_info.firmware_size = m_firmware_size;
        m_firmware_info.checksum = calculateChecksum(m_firmware_data);
        m_firmware_info.is_valid = true;
        
        // Verzió és build dátum kinyerése (ha elérhető)
        // Ez függ a konkrét firmware formátumtól
    }
    
    void MS2000SyxParser::parseMS2000PresetInfo()
    {
        if (!m_has_presets) {
            return;
        }
        
        m_preset_info.clear();
        
        for (const auto& preset : m_preset_data) {
            MS2000PresetInfo info;
            info.preset_number = preset.first;
            info.checksum = calculateChecksum(preset.second);
            info.is_valid = true;
            
            // Preset név kinyerése (ha elérhető)
            // Ez függ a konkrét preset formátumtól
            
            m_preset_info.push_back(info);
        }
    }
    
    void MS2000SyxParser::parseMS2000SystemInfo()
    {
        if (m_system_data.empty()) {
            return;
        }
        
        // Rendszer beállítások kinyerése
        if (m_system_data.size() >= 4) {
            m_system_info.master_volume = m_system_data[0];
            m_system_info.midi_channel = m_system_data[1];
            m_system_info.transpose = m_system_data[2];
            m_system_info.tuning = m_system_data[3];
            m_system_info.is_valid = true;
        }
    }
    
    bool MS2000SyxParser::exportMS2000Firmware(const std::string& output_path) const
    {
        exportFirmware(output_path);
        return m_has_firmware;
    }
    
    bool MS2000SyxParser::exportMS2000Presets(const std::string& output_path) const
    {
        exportPresets(output_path);
        return m_has_presets;
    }
    
    bool MS2000SyxParser::exportMS2000System(const std::string& output_path) const
    {
        if (m_system_data.empty()) {
            std::cerr << "Error: No system data to export" << std::endl;
            return false;
        }
        
        std::ofstream file(output_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot create output file: " << output_path << std::endl;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(m_system_data.data()), m_system_data.size());
        file.close();
        
        std::cout << "System settings exported to: " << output_path << std::endl;
        return true;
    }
}
