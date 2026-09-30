#pragma once
#include <cstdint>

enum class LcdPath : uint8_t { GPIO, PANEL_MP, DIRECT };
void lcd_trace(bool isData, uint8_t val, LcdPath path);