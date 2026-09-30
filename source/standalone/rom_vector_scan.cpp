#include <iostream>
#include <iomanip>
#include "../core/h8s2350_emulator.h"

using namespace MS2000;

int main() {
    std::cout << "=== ROM Vector Table Scanner ===" << std::endl;
    
    H8S2350Emulator emulator;
    emulator.setDebugMode(true);
    
    // Load firmware
    if (!emulator.loadFirmwareFromFile("flash.bin")) {
        std::cout << "❌ Failed to load firmware" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Firmware loaded successfully" << std::endl;
    
    // Scan ROM for potential vector tables in the 0x000000..0x0003FF range
    std::cout << "\n🔍 Scanning ROM for vector tables (0x000000..0x0003FF):" << std::endl;
    
    bool found_table = false;
    
    for (uint32_t base = 0x000000; base <= 0x0003C0; base += 0x40) {
        std::cout << "\n--- Base 0x" << std::hex << std::setfill('0') << std::setw(6) << base << " ---" << std::dec << std::endl;
        
        int valid_count = 0;
        int non_ff_count = 0;
        
        for (int i = 0; i < 16; i++) {
            uint32_t addr = base + (i * 4);
            uint32_t vector = emulator.readLong(addr);
            
            bool is_valid = (vector != 0x00000000 && vector != 0xFFFFFFFF);
            bool points_to_rom = (vector >= 0x000000 && vector <= 0x0FFFFF);
            bool even_aligned = ((vector & 1) == 0);
            
            if (is_valid) non_ff_count++;
            if (is_valid && points_to_rom && even_aligned) valid_count++;
            
            std::cout << "V[" << std::setfill('0') << std::setw(2) << i << "]="
                      << "0x" << std::hex << std::setw(8) << vector << std::dec;
            
            if (!is_valid) {
                std::cout << " (invalid)";
            } else if (!points_to_rom) {
                std::cout << " (out-of-rom)";
            } else if (!even_aligned) {
                std::cout << " (odd-addr)";
            } else {
                std::cout << " ✓";
            }
            std::cout << std::endl;
        }
        
        std::cout << "Score: " << valid_count << " valid vectors, " << non_ff_count << " non-0xFF" << std::endl;
        
        if (valid_count >= 4) {
            std::cout << "🎯 POTENTIAL VECTOR TABLE FOUND!" << std::endl;
            found_table = true;
        }
    }
    
    if (!found_table) {
        std::cout << "\n⚠️  No clear vector table found in ROM header" << std::endl;
        std::cout << "This suggests the firmware may:" << std::endl;
        std::cout << "1. Copy vectors to RAM at runtime" << std::endl;
        std::cout << "2. Use a different vector table layout" << std::endl;
        std::cout << "3. Have the vector table elsewhere in ROM" << std::endl;
    }
    
    // Also check common H8S vector locations
    std::cout << "\n🔍 Checking common H8S vector locations:" << std::endl;
    
    struct VectorInfo {
        const char* name;
        uint32_t address;
    };
    
    VectorInfo vectors[] = {
        {"Reset", 0x000000},
        {"Manual Reset", 0x000004},
        {"Reserved", 0x000008},
        {"General Illegal Instruction", 0x00000C},
        {"Slot Illegal Instruction", 0x000010},
        {"CPU Address Error", 0x000014},
        {"DMA Address Error", 0x000018},
        {"NMI", 0x00001C},
        {"User Break", 0x000020},
        {"H-UDI", 0x000024},
        {"IRQ0", 0x000040},
        {"IRQ1", 0x000044},
        {"IRQ2", 0x000048},
        {"IRQ3", 0x00004C}
    };
    
    for (auto& vec : vectors) {
        uint32_t value = emulator.readLong(vec.address);
        std::cout << vec.name << " (0x" << std::hex << std::setfill('0') << std::setw(6) << vec.address
                  << "): 0x" << std::setw(8) << value << std::dec;
        
        if (value == 0xFFFFFFFF) {
            std::cout << " (uninitialized)";
        } else if (value >= 0x000000 && value <= 0x0FFFFF && (value & 1) == 0) {
            std::cout << " ✓ (valid ROM address)";
        } else {
            std::cout << " (suspicious)";
        }
        std::cout << std::endl;
    }
    
    std::cout << "\n=== ROM Vector Scan Complete ===" << std::endl;
    return 0;
}