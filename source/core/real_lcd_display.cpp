#include "real_lcd_display.h"

#include <cassert>
#include <iostream>

#define LOG(x) std::cout << "[LCD] " << x << std::endl

namespace MS2000
{
	RealLCDDisplay::RealLCDDisplay() = default;

	std::optional<uint8_t> RealLCDDisplay::exec(bool registerSelect, bool read, uint8_t g)
	{
		bool changed = false;
		bool cgRamChanged = false;

		std::optional<uint8_t> result;

		if(!read)
		{
			if(!registerSelect)
			{
				if(g == 0x01)
				{
					LOG("LCD Clear Display");
					m_dramData.fill(' ');
					changed = true;
					m_changed = true;
				}
				else if(g == 0x02)
				{
					LOG("LCD Return Home");
					m_dramAddr = 0;
					m_cursorPos = 0;
				}
				else if((g & 0xfc) == 0x04)
				{
					const int increment = (g >> 1) & 1;
					const int shift = g & 1;
					LOG("LCD Entry Mode Set, inc=" << increment << ", shift=" << shift);

					m_addrIncrement = increment ? 1 : -1;
				}
				else if((g & 0xf8) == 0x08)
				{
					const int displayOnOff = (g >> 2) & 1;
					const int cursorOnOff = (g >> 1) & 1;
					const int cursorBlinking = g & 1;

					LOG("LCD Display ON/OFF, display=" << displayOnOff << ", cursor=" << cursorOnOff << ", blinking=" << cursorBlinking);

					m_displayOn = displayOnOff != 0;
					m_cursorOn = cursorOnOff != 0;
					m_cursorBlinking = cursorBlinking != 0;
				}
				else if((g & 0xf3) == 0x10)
				{
					const int scrl = (g >> 2) & 3;

					LOG("LCD Cursor/Display Shift, scrl=" << scrl);
					m_cursorShift = static_cast<CursorShiftMode>(scrl);
				}
				else if((g & 0xec) == 0x28)
				{
					const int dl = (g >> 4) & 1;
					const int ft = g & 3;

					LOG("LCD Function Set, dl=" << dl << ", ft=" << ft);
					m_dataLength = static_cast<DataLength>(dl);
					m_fontTable = static_cast<FontTable>(ft);
				}
				else if((g & (1<<7)) != 0)
				{
				    const int addr = g & 0x7f;
				    // Mask to valid DDRAM range: 0x00-0x13 (line 1) or 0x40-0x53 (line 2)
				    // Wrap/ignore invalid addresses
				    if (addr < 20) {
				        LOG("LCD Set DDRAM address line 1, addr=" << addr);
				        m_dramAddr = addr;
				    } else if (addr >= 0x40 && addr < 0x54) {
				        LOG("LCD Set DDRAM address line 2, addr=" << addr);
				        m_dramAddr = addr;
				    } else if (addr >= 20 && addr < 40) {
				        LOG("LCD Set DDRAM address line 2 wrap, addr=" << addr);
				        m_dramAddr = 0x40 + (addr - 20);
				    } else {
				        LOG("LCD Set DDRAM address INVALID/ignored, addr=" << addr);
				        // Ignore invalid - don't change address
				    }
				    m_addressMode = AddressMode::DDRam;
				}
				else if(g & (1<<6))
				{
					const int acg = g & 0x3f;

//					LOG("LCD Set CGRAM address, acg=" << acg);
					m_cgramAddr = acg;
					m_addressMode = AddressMode::CGRam;
				}
				else
				{
					LOG("LCD unknown command");
					assert(false);
				}
			}
			else
			{
				if(m_addressMode == AddressMode::CGRam)
				{
//					changed = true;
//					LOG("LCD write data to CGRAM addr " << m_cgramAddr << ", data=" << static_cast<int>(g));

					if (m_cgramData[m_cgramAddr] != g)
					{
						m_cgramData[m_cgramAddr] = g;
						cgRamChanged = true;
					}

					m_cgramAddr += m_addrIncrement;

					/*
					if((m_cgramAddr & 0x7) == 0)
					{
						std::stringstream ss;
						ss << "CG RAM character " << (m_cgramAddr/8 - 1) << ':' << '\n';
						ss << "##################" << '\n';
						for(auto i = m_cgramAddr - 8; i < m_cgramAddr - 1; ++i)
						{
							ss << '#';
							for(int x=7; x >= 0; --x)
							{
								if(m_cgramData[i] & (1<<x))
									ss << "[]";
								else
									ss << "  ";
							}
							ss << '#' << '\n';
						}
						ss << "##################" << '\n';
						const auto s(ss.str());
						LOG(s);
					}
					*/
				}
				else
				{
//					LOG("LCD write data to DDRAM addr " << m_dramAddr << ", data=" << static_cast<int>(g) << ", char=" << static_cast<char>(g));

					const auto old = m_dramData;

					if (m_dramAddr < 20) {
						// Line 1: 0-19
						m_dramData[m_dramAddr] = static_cast<char>(g);
					} else if (m_dramAddr >= 0x40 && m_dramAddr < 0x54) {
						// Line 2: 0x40-0x53 (map to 20-39)
						m_dramData[m_dramAddr - 0x40 + 20] = static_cast<char>(g);
					} else if (m_dramAddr >= 20 && m_dramAddr < 40) {
						// Wrap/write aliased line 2
						m_dramData[m_dramAddr] = static_cast<char>(g);
					} else {
						// Invalid address - ignore
						LOG("LCD write data to INVALID DDRAM addr " << m_dramAddr);
					}

					    int nextAddr = m_dramAddr + m_addrIncrement;
					    // HD44780 wrap: line1 [0-19] -> line2 [0x40-0x53], line2 [0x40-0x53] -> line1 [0-19]
					    if (nextAddr < 0) {
					        nextAddr = 0x53;  // underflow -> last line2 addr
					    } else if (nextAddr >= 0x54) {
					        nextAddr = 0;     // overflow past line2 -> back to line1 start
					    } else if (nextAddr >= 0x14 && nextAddr < 0x40) {
					        nextAddr = 0x40;  // wrap between lines
					    }
					    m_dramAddr = nextAddr;

					if(m_dramData != old) {
						changed = true;
						m_changed = true;
						
						// fw26.txt: Trigger DDRAM write callback for event-driven LCD validation
							if (m_ddramWriteCallback) {
							m_ddramWriteCallback(static_cast<uint8_t>(m_dramAddr), g);
						}
					}
				}
			}
		}
		else
		{
			if(registerSelect)
			{
				LOG("LCD read data from CGRAM or DDRAM");
				if(m_addressMode == AddressMode::CGRam)
					result = m_cgramData[m_cgramAddr];
				else
					result = m_dramData[m_dramAddr];
			}
			else
			{
				LOG("LCD read busy flag & address");
				if(m_addressMode == AddressMode::CGRam)
				{
					result = static_cast<uint8_t>(m_cgramAddr);
				}
				else
				{
					auto a = m_dramAddr;
					if(a > 0x53)
						a = 0x53;
					if(a == 20)
						a = 19;
					result = static_cast<uint8_t>(m_dramAddr);
				}
			}
		}

		if(changed && m_changeCallback)
			m_changeCallback();

		if(cgRamChanged && m_cgRamChangeCallback)
			m_cgRamChangeCallback();

		return result;
	}

	bool RealLCDDisplay::getCgData(std::array<uint8_t, 8>& _data, uint32_t _charIndex) const
	{
		const auto idx = _charIndex * 8;
		if(idx + 8 >= getCgRam().size())
			return false;

		uint32_t j = 0;

		for(auto i = idx; i<idx+8; ++i)
			_data[j++] = getCgRam()[i];

		return true;
	}

	// Compatibility functions
	std::string RealLCDDisplay::getText(int line) const
	{
		if (line == 0) {
			return std::string(m_dramData.data(), 20);
		} else if (line == 1) {
			return std::string(m_dramData.data() + 20, 20);
		}
		return "";
	}

	void RealLCDDisplay::setText(int line, const std::string& text)
	{
		// This is for testing only - real LCD should be controlled by firmware
		if (line == 0) {
			for (int i = 0; i < 20 && i < text.length(); i++) {
				m_dramData[i] = text[i];
			}
		} else if (line == 1) {
			for (int i = 0; i < 20 && i < text.length(); i++) {
				m_dramData[i + 20] = text[i];
			}
		}
		m_changed = true;
	}

	void RealLCDDisplay::clear()
	{
		m_dramData.fill(' ');
		m_changed = true;
	}
}
