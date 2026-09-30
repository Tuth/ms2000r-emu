#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace MS2000
{
	// 2*20 characters display simulation (20*2)
	// EW20290GLW / NHD-0220DZW-AB5 and compatibles

	class RealLCDDisplay
	{
	public:
		using ChangeCallback = std::function<void()>;
		using DDRAMWriteCallback = std::function<void(uint8_t addr, uint8_t data)>; // fw26.txt: DDRAM write hook

		RealLCDDisplay();
		std::optional<uint8_t> exec(bool registerSelect, bool read, uint8_t g);

		const std::array<char, 40>& getDdRam() const { return m_dramData; }
		const auto& getCgRam() const { return m_cgramData; }
		bool getCgData(std::array<uint8_t, 8>& _data, uint32_t _charIndex) const;
		
		// Compatibility with our existing LCD interface
		std::string getText(int line) const;
		void setText(int line, const std::string& text);
		void clear();
		bool hasChanged() const { return m_changed; }
		void clearChanged() { m_changed = false; }
		bool isDisplayOn() const { return m_displayOn; }

		void setChangeCallback(const ChangeCallback& _callback)
		{
			m_changeCallback = _callback;
		}

		void setCgRamChangeCallback(const ChangeCallback& _callback)
		{
			m_cgRamChangeCallback = _callback;
		}

		// fw26.txt: DDRAM write callback for event-driven LCD validation
		void setDDRAMWriteCallback(const DDRAMWriteCallback& callback)
		{
			m_ddramWriteCallback = callback;
		}
	private:
		enum class CursorShiftMode
		{
			CursorLeft,
			CursorRight,
			DisplayLeft,
			DisplayRight
		};
		enum class DisplayShiftMode
		{
			Right,
			Left
		};
		enum class FontTable
		{
			EnglishJapanese,
			WesternEuropean1,
			EnglishRussian,
			WesternEuropean2,
		};
		enum class DataLength
		{
			Bit8,
			Bit4
		};

		enum class AddressMode
		{
			DDRam,
			CGRam,
		};

		uint32_t m_lastWriteCounter = 0xffffffff;

		uint32_t m_cursorPos = 0;
		uint32_t m_dramAddr = 0;
		uint32_t m_cgramAddr = 0;

		CursorShiftMode m_cursorShift = CursorShiftMode::CursorLeft;
		DisplayShiftMode m_displayShift = DisplayShiftMode::Left;
		FontTable m_fontTable = FontTable::EnglishJapanese;
		DataLength m_dataLength = DataLength::Bit8;
		AddressMode m_addressMode = AddressMode::DDRam;

		bool m_displayOn = true;
		bool m_cursorOn = false;
		bool m_cursorBlinking = false;

		int m_addrIncrement = 1;

		std::array<uint8_t, 0x40> m_cgramData{};
		std::array<char, 40> m_dramData{};
		uint32_t m_lastOpState = 0;
		bool m_changed = false;

		ChangeCallback m_changeCallback;
		ChangeCallback m_cgRamChangeCallback;
		DDRAMWriteCallback m_ddramWriteCallback; // fw26.txt: DDRAM write notification
	};
}
