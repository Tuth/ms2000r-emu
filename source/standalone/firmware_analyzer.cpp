#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <numeric>
#include <cstdint>

class FirmwareAnalyzer
{
public:
    FirmwareAnalyzer() = default;
    
    bool loadFirmwareFile(const std::string& file_path)
    {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open firmware file: " << file_path << std::endl;
            return false;
        }
        
        // Fájl méret meghatározása
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Fájl betöltése
        m_firmware_data.resize(file_size);
        file.read(reinterpret_cast<char*>(m_firmware_data.data()), file_size);
        file.close();
        
        std::cout << "✅ Firmware file loaded: " << file_size << " bytes" << std::endl;
        return true;
    }
    
    void analyzeFirmware()
    {
        if (m_firmware_data.empty()) {
            std::cerr << "Error: No firmware data loaded" << std::endl;
            return;
        }
        
        std::cout << "\n=== Firmware Analysis ===" << std::endl;
        std::cout << "File size: " << m_firmware_data.size() << " bytes" << std::endl;
        
        // Header elemzés
        analyzeHeader();
        
        // String keresés
        searchStrings();
        
        // Pattern keresés
        searchPatterns();
        
        // I/O Register elemzés
        analyzeIORegisters();
        
        // Checksum számítás
        calculateChecksum();
        
        // DSP56000 specifikus elemzés
        analyzeDSP56000();
    }
    
    void exportToBinary(const std::string& output_path)
    {
        if (m_firmware_data.empty()) {
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
        
        std::cout << "✅ Firmware exported to: " << output_path << std::endl;
    }
    
    void exportToHex(const std::string& output_path)
    {
        if (m_firmware_data.empty()) {
            std::cerr << "Error: No firmware data to export" << std::endl;
            return;
        }
        
        std::ofstream file(output_path);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot create output file: " << output_path << std::endl;
            return;
        }
        
        file << std::hex << std::setfill('0');
        for (size_t i = 0; i < m_firmware_data.size(); i++) {
            file << std::setw(2) << (int)m_firmware_data[i];
            if ((i + 1) % 16 == 0) {
                file << std::endl;
            } else {
                file << " ";
            }
        }
        file.close();
        
        std::cout << "✅ Firmware exported to hex: " << output_path << std::endl;
    }
    
private:
    std::vector<uint8_t> m_firmware_data;
    
    void analyzeHeader()
    {
        std::cout << "\n--- Header Analysis ---" << std::endl;
        
        if (m_firmware_data.size() < 32) {
            std::cout << "Warning: File too small for header analysis" << std::endl;
            return;
        }
        
        // Első 32 byte hex dump
        std::cout << "First 32 bytes:" << std::endl;
        for (size_t i = 0; i < 32; i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)m_firmware_data[i] << " ";
            if ((i + 1) % 16 == 0) {
                std::cout << std::endl;
            }
        }
        std::cout << std::dec << std::endl;
        
        // Korg string keresés
        std::string korg_signature = "KORG";
        auto it = std::search(m_firmware_data.begin(), m_firmware_data.end(),
                             korg_signature.begin(), korg_signature.end());
        if (it != m_firmware_data.end()) {
            size_t offset = std::distance(m_firmware_data.begin(), it);
            std::cout << "Found KORG signature at offset: 0x" << std::hex << offset << std::endl;
        }
        
        // MS2000 string keresés
        std::string ms2000_signature = "MS2000";
        it = std::search(m_firmware_data.begin(), m_firmware_data.end(),
                        ms2000_signature.begin(), ms2000_signature.end());
        if (it != m_firmware_data.end()) {
            size_t offset = std::distance(m_firmware_data.begin(), it);
            std::cout << "Found MS2000 signature at offset: 0x" << std::hex << offset << std::endl;
        }
    }
    
    void searchStrings()
    {
        std::cout << "\n--- String Search ---" << std::endl;
        
        std::vector<std::string> search_strings = {
            "KORG", "MS2000", "MS-2000", "DSP", "Firmware", "Version", "Build",
            "Copyright", "Motorola", "DSP56000", "DSP56001", "ROM", "RAM"
        };
        
        for (const auto& search_str : search_strings) {
            auto it = std::search(m_firmware_data.begin(), m_firmware_data.end(),
                                 search_str.begin(), search_str.end());
            if (it != m_firmware_data.end()) {
                size_t offset = std::distance(m_firmware_data.begin(), it);
                std::cout << "Found '" << search_str << "' at offset: 0x" << std::hex << offset << std::endl;
            }
        }
    }
    
    void searchPatterns()
    {
        std::cout << "\n--- Pattern Search ---" << std::endl;
        
        // DSP56000 entry point pattern (gyakori: 0x0000 vagy 0x1000)
        uint32_t entry_point = 0;
        if (m_firmware_data.size() >= 4) {
            entry_point = (m_firmware_data[0] << 24) | (m_firmware_data[1] << 16) | 
                         (m_firmware_data[2] << 8) | m_firmware_data[3];
            std::cout << "Possible entry point: 0x" << std::hex << entry_point << std::endl;
        }
        
        // Zero pattern keresés (ROM padding)
        size_t zero_count = 0;
        for (size_t i = m_firmware_data.size() - 1; i >= 0 && i < m_firmware_data.size(); i--) {
            if (m_firmware_data[i] == 0) {
                zero_count++;
            } else {
                break;
            }
        }
        if (zero_count > 0) {
            std::cout << "Zero padding at end: " << zero_count << " bytes" << std::endl;
        }
    }
    
    void calculateChecksum()
    {
        std::cout << "\n--- Checksum Analysis ---" << std::endl;
        
        uint32_t checksum = 0;
        for (uint8_t byte : m_firmware_data) {
            checksum += byte;
        }
        
        std::cout << "Simple checksum: 0x" << std::hex << checksum << std::endl;
        std::cout << "Simple checksum (32-bit): 0x" << std::hex << (checksum & 0xFFFFFFFF) << std::endl;
        
        // XOR checksum
        uint32_t xor_checksum = 0;
        for (uint8_t byte : m_firmware_data) {
            xor_checksum ^= byte;
        }
        std::cout << "XOR checksum: 0x" << std::hex << xor_checksum << std::endl;
    }
    
    void analyzeIORegisters()
    {
        std::cout << "\n--- I/O Register Analysis (0xFE00-0xFFFF) ---" << std::endl;
        
        // Known I/O register categories
        std::map<uint16_t, std::string> known_registers = {
            // PORT registers
            {0xFE00, "PORT"},
            {0xFE01, "PORT"},
            {0xFE02, "PORT"},
            {0xFE03, "PORT"},
            {0xFE04, "PORT"},
            {0xFE05, "PORT"},
            {0xFE06, "PORT"},
            {0xFE07, "PORT"},
            
            // ADC registers
            {0xFE10, "ADC"},
            {0xFE11, "ADC"},
            {0xFE12, "ADC"},
            {0xFE13, "ADC"},
            {0xFE14, "ADC"},
            {0xFE15, "ADC"},
            {0xFE16, "ADC"},
            {0xFE17, "ADC"},
            
            // SCI registers
            {0xFE20, "SCI"},
            {0xFE21, "SCI"},
            {0xFE22, "SCI"},
            {0xFE23, "SCI"},
            {0xFE24, "SCI"},
            {0xFE25, "SCI"},
            {0xFE26, "SCI"},
            {0xFE27, "SCI"},
            
            // TPU registers
            {0xFE30, "TPU"},
            {0xFE31, "TPU"},
            {0xFE32, "TPU"},
            {0xFE33, "TPU"},
            {0xFE34, "TPU"},
            {0xFE35, "TPU"},
            {0xFE36, "TPU"},
            {0xFE37, "TPU"},
            
            // Timer registers
            {0xFE40, "TIMER"},
            {0xFE41, "TIMER"},
            {0xFE42, "TIMER"},
            {0xFE43, "TIMER"},
            {0xFE44, "TIMER"},
            {0xFE45, "TIMER"},
            {0xFE46, "TIMER"},
            {0xFE47, "TIMER"},
            
            // Interrupt registers
            {0xFE50, "INT"},
            {0xFE51, "INT"},
            {0xFE52, "INT"},
            {0xFE53, "INT"},
            {0xFE54, "INT"},
            {0xFE55, "INT"},
            {0xFE56, "INT"},
            {0xFE57, "INT"},
            
            // System control registers
            {0xFF00, "SYS"},
            {0xFF01, "SYS"},
            {0xFF02, "SYS"},
            {0xFF03, "SYS"},
            {0xFF04, "SYS"},
            {0xFF05, "SYS"},
            {0xFF06, "SYS"},
            {0xFF07, "SYS"},
            
            // Memory control registers
            {0xFF10, "MEM"},
            {0xFF11, "MEM"},
            {0xFF12, "MEM"},
            {0xFF13, "MEM"},
            {0xFF14, "MEM"},
            {0xFF15, "MEM"},
            {0xFF16, "MEM"},
            {0xFF17, "MEM"},
            
            // DMA registers
            {0xFF20, "DMA"},
            {0xFF21, "DMA"},
            {0xFF22, "DMA"},
            {0xFF23, "DMA"},
            {0xFF24, "DMA"},
            {0xFF25, "DMA"},
            {0xFF26, "DMA"},
            {0xFF27, "DMA"},
            
            // Debug registers
            {0xFF30, "DEBUG"},
            {0xFF31, "DEBUG"},
            {0xFF32, "DEBUG"},
            {0xFF33, "DEBUG"},
            {0xFF34, "DEBUG"},
            {0xFF35, "DEBUG"},
            {0xFF36, "DEBUG"},
            {0xFF37, "DEBUG"},
            
            // Reserved/Unknown registers
            {0xFFF0, "RESERVED"},
            {0xFFF1, "RESERVED"},
            {0xFFF2, "RESERVED"},
            {0xFFF3, "RESERVED"},
            {0xFFF4, "RESERVED"},
            {0xFFF5, "RESERVED"},
            {0xFFF6, "RESERVED"},
            {0xFFF7, "RESERVED"},
            {0xFFF8, "RESERVED"},
            {0xFFF9, "RESERVED"},
            {0xFFFA, "RESERVED"},
            {0xFFFB, "RESERVED"},
            {0xFFFC, "RESERVED"},
            {0xFFFD, "RESERVED"},
            {0xFFFE, "RESERVED"},
            {0xFFFF, "RESERVED"}
        };
        
        // Counters for different address types
        std::unordered_map<uint16_t, int> address_counts_16bit;
        std::unordered_map<uint32_t, int> address_counts_24bit;
        std::map<std::string, int> category_counts;
        
        // Scan firmware for immediate addresses
        for (size_t i = 0; i < m_firmware_data.size() - 1; i++) {
            // Check for 16-bit immediate addresses in I/O range
            if (i + 1 < m_firmware_data.size()) {
                uint16_t addr_16bit = (m_firmware_data[i] << 8) | m_firmware_data[i + 1];
                if (addr_16bit >= 0xFE00 && addr_16bit <= 0xFFFF) {
                    address_counts_16bit[addr_16bit]++;
                    
                    // Categorize by known register type
                    auto it = known_registers.find(addr_16bit);
                    if (it != known_registers.end()) {
                        category_counts[it->second]++;
                    } else {
                        category_counts["UNKNOWN"]++;
                    }
                }
            }
            
            // Check for 24-bit immediate addresses in I/O range
            if (i + 2 < m_firmware_data.size()) {
                uint32_t addr_24bit = (m_firmware_data[i] << 16) | 
                                    (m_firmware_data[i + 1] << 8) | 
                                    m_firmware_data[i + 2];
                if (addr_24bit >= 0xFE0000 && addr_24bit <= 0xFFFFFF) {
                    address_counts_24bit[addr_24bit]++;
                }
            }
        }
        
        // 1. FW I/O address candidates (targets) with hit counts
        std::cout << "\n1. FW I/O Address Candidates (Targets):" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::left << std::setw(8) << "Address" 
                  << std::setw(10) << "Category" 
                  << std::setw(8) << "16-bit" 
                  << std::setw(8) << "24-bit" 
                  << "Description" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        
        for (const auto& reg : known_registers) {
            uint16_t addr = reg.first;
            std::string category = reg.second;
            int count_16bit = address_counts_16bit[addr];
            int count_24bit = address_counts_24bit[addr | 0xFE0000];
            
            if (count_16bit > 0 || count_24bit > 0) {
                std::cout << std::left << std::setw(8) << std::hex << "0x" << std::setw(4) << std::setfill('0') << addr << std::setfill(' ')
                          << std::setw(10) << category
                          << std::setw(8) << std::dec << count_16bit
                          << std::setw(8) << count_24bit
                          << "I/O Register" << std::endl;
            }
        }
        
        // 2. Top 40 16-bit immediates from I/O range
        std::cout << "\n2. Top 40 16-bit Immediates (0xFE00-0xFFFF):" << std::endl;
        std::cout << "=============================================" << std::endl;
        std::cout << std::left << std::setw(8) << "Address" 
                  << std::setw(10) << "Category" 
                  << std::setw(8) << "Count" 
                  << "Description" << std::endl;
        std::cout << std::string(50, '-') << std::endl;
        
        std::vector<std::pair<uint16_t, int>> sorted_16bit;
        for (const auto& pair : address_counts_16bit) {
            sorted_16bit.push_back(pair);
        }
        std::sort(sorted_16bit.begin(), sorted_16bit.end(), 
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        int count = 0;
        for (const auto& pair : sorted_16bit) {
            if (count >= 40) break;
            
            uint16_t addr = pair.first;
            int hits = pair.second;
            auto it = known_registers.find(addr);
            std::string category = (it != known_registers.end()) ? it->second : "UNKNOWN";
            
            std::cout << std::left << std::setw(8) << std::hex << "0x" << std::setw(4) << std::setfill('0') << addr << std::setfill(' ')
                      << std::setw(10) << category
                      << std::setw(8) << std::dec << hits
                      << "16-bit immediate" << std::endl;
            count++;
        }
        
        // 3. Top 40 24-bit immediates from I/O range
        std::cout << "\n3. Top 40 24-bit Immediates (0xFE0000-0xFFFFFF):" << std::endl;
        std::cout << "=================================================" << std::endl;
        std::cout << std::left << std::setw(10) << "Address" 
                  << std::setw(10) << "Category" 
                  << std::setw(8) << "Count" 
                  << "Description" << std::endl;
        std::cout << std::string(52, '-') << std::endl;
        
        std::vector<std::pair<uint32_t, int>> sorted_24bit;
        for (const auto& pair : address_counts_24bit) {
            sorted_24bit.push_back(pair);
        }
        std::sort(sorted_24bit.begin(), sorted_24bit.end(), 
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        count = 0;
        for (const auto& pair : sorted_24bit) {
            if (count >= 40) break;
            
            uint32_t addr = pair.first;
            int hits = pair.second;
            uint16_t addr_16bit = addr & 0xFFFF;
            auto it = known_registers.find(addr_16bit);
            std::string category = (it != known_registers.end()) ? it->second : "UNKNOWN";
            
            std::cout << std::left << std::setw(10) << std::hex << "0x" << std::setw(6) << std::setfill('0') << addr << std::setfill(' ')
                      << std::setw(10) << category
                      << std::setw(8) << std::dec << hits
                      << "24-bit immediate" << std::endl;
            count++;
        }
        
        // Summary statistics
        std::cout << "\nI/O Register Analysis Summary:" << std::endl;
        std::cout << "==============================" << std::endl;
        std::cout << "Total 16-bit I/O addresses found: " << address_counts_16bit.size() << std::endl;
        std::cout << "Total 24-bit I/O addresses found: " << address_counts_24bit.size() << std::endl;
        int total_16bit = 0;
        for (const auto& pair : address_counts_16bit) {
            total_16bit += pair.second;
        }
        int total_24bit = 0;
        for (const auto& pair : address_counts_24bit) {
            total_24bit += pair.second;
        }
        std::cout << "Total I/O references: " << (total_16bit + total_24bit) << std::endl;
        
        std::cout << "\nCategory breakdown:" << std::endl;
        for (const auto& pair : category_counts) {
            if (pair.second > 0) {
                std::cout << "  " << std::left << std::setw(10) << pair.first 
                          << ": " << pair.second << " references" << std::endl;
            }
        }
    }
    
    void analyzeDSP56000()
    {
        std::cout << "\n--- DSP56000 Analysis ---" << std::endl;
        
        // DSP56000 memória méretek
        size_t firmware_size = m_firmware_data.size();
        
        if (firmware_size <= 0x4000) { // 16KB
            std::cout << "Firmware size: " << firmware_size << " bytes (<= 16KB)" << std::endl;
            std::cout << "Likely: DSP56000 program memory" << std::endl;
        } else if (firmware_size <= 0x8000) { // 32KB
            std::cout << "Firmware size: " << firmware_size << " bytes (<= 32KB)" << std::endl;
            std::cout << "Likely: DSP56000 program memory" << std::endl;
        } else if (firmware_size <= 0x10000) { // 64KB
            std::cout << "Firmware size: " << firmware_size << " bytes (<= 64KB)" << std::endl;
            std::cout << "Likely: DSP56000 program memory" << std::endl;
        } else {
            std::cout << "Firmware size: " << firmware_size << " bytes (> 64KB)" << std::endl;
            std::cout << "Likely: Multiple memory regions or data included" << std::endl;
        }
        
        // DSP56000 opcode pattern keresés
        // Gyakori DSP56000 opcode-ok: 0x0000 (NOP), 0x2000 (MOVE), stb.
        size_t nop_count = 0;
        for (size_t i = 0; i < m_firmware_data.size() - 1; i += 2) {
            uint16_t opcode = (m_firmware_data[i] << 8) | m_firmware_data[i + 1];
            if (opcode == 0x0000) { // NOP
                nop_count++;
            }
        }
        std::cout << "NOP instructions found: " << nop_count << std::endl;
    }
};

int main(int argc, char* argv[])
{
    std::cout << "🔍 Korg MS-2000 Firmware Analyzer" << std::endl;
    std::cout << "=================================" << std::endl;
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <firmware_file>" << std::endl;
        std::cout << std::endl;
        std::cout << "This tool can:" << std::endl;
        std::cout << "  - Analyze Korg MS-2000 firmware files" << std::endl;
        std::cout << "  - Search for strings and patterns" << std::endl;
        std::cout << "  - Calculate checksums" << std::endl;
        std::cout << "  - Export to binary/hex format" << std::endl;
        std::cout << std::endl;
        std::cout << "Example:" << std::endl;
        std::cout << "  " << argv[0] << " x811v107.sys" << std::endl;
        return 1;
    }
    
    std::string firmware_file = argv[1];
    
    std::cout << "Loading firmware file: " << firmware_file << std::endl;
    
    FirmwareAnalyzer analyzer;
    
    if (!analyzer.loadFirmwareFile(firmware_file)) {
        std::cerr << "Error: Failed to load firmware file!" << std::endl;
        return 1;
    }
    
    // Firmware elemzés
    analyzer.analyzeFirmware();
    
    // Exportálás
    std::string base_name = firmware_file.substr(0, firmware_file.find_last_of('.'));
    analyzer.exportToBinary(base_name + "_extracted.bin");
    analyzer.exportToHex(base_name + "_hex.txt");
    
    std::cout << std::endl;
    std::cout << "✅ Firmware analysis completed!" << std::endl;
    
    return 0;
}
