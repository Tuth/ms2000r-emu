// dsp_bootrom_probe - BUG104 measurement, not part of the emulator.
//
// Runs the VINTAGE DSP56362 boot ROM (a local dump, never in the repo) in the upstream dsp56kEmu
// (d6e4514), in mode 5 = MODD:MODC:MODB:MODA 0101 as KOD-A30412 straps it, and feeds it the
// MS2000 firmware's OWN stage-1 image (flash.bin 0x03BCDE, 30 bytes) over the SHI, one 24-bit word
// per SPI exchange. Prints what the ROM does. Nothing here is modelled by us: the ROM is Motorola's,
// the words are KORG's, the core and the SHI are the library's.
//
// usage: dsp_bootrom_probe <boot-362.bin> <flash.bin>
#include "dsp.h"
#include "memory.h"
#include "peripherals.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace dsp56k;

static std::vector<uint8_t> load(const char* _p)
{
	std::ifstream f(_p, std::ios::binary);
	return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv)
{
	if (argc < 3) { printf("usage: dsp_bootrom_probe <boot-362.bin> <flash.bin>\n"); return 2; }
	const auto rom = load(argv[1]);
	const auto fw  = load(argv[2]);
	if (rom.size() != 768 || fw.size() < 0x03BCDE + 30) { printf("bad input sizes %zu %zu\n", rom.size(), fw.size()); return 2; }

	DefaultMemoryValidator validator;
	Memory mem(validator, 0x1000000);            // full 24-bit space so P:$FF0000 exists
	Peripherals56362 periphX;
	PeripheralsNop periphY;
	DSP dsp(mem, &periphX, &periphY);

	// The stage-1 image lives at P:0..7 - the interrupt vector slots - and is reached by a plain
	// jmp. JitConfig says of dynamicFastInterrupts: "needs to be true if there is code that
	// executes code in interrupt regions as regular jumps". Argument 4 "static" leaves it off
	// so both behaviours can be measured.
	const bool dynFast = !(argc > 4 && std::string(argv[4]) == "static");
	{
		auto cfg = dsp.getJit().getConfig();
		cfg.dynamicFastInterrupts = dynFast;
		dsp.getJit().setConfig(cfg);
	}
	printf("JitConfig.dynamicFastInterrupts = %d\n", dynFast ? 1 : 0);

	for (TWord i = 0; i < 192; ++i)          // 192 words x 4 bytes little-endian (measured)
	{
		const TWord w = rom[4*i] | (rom[4*i+1] << 8) | (rom[4*i+2] << 16);
		mem.set(MemArea_P, 0xFF0000 + i, w);
	}

	// mode 5, latched from the MODA-MODD pins into OMR bits 3..0 when RESET deasserts
	// (DSP56362/D Table 1-8). DSP::com() is private, so the latch is written the way the pins do.
	dsp.regs().omr.var = (dsp.regs().omr.var & ~0xF) | 0x5;
	dsp.setPC(0xFF0000);                           // Table 4-1: reset vector $FF0000

	auto& shi = periphX.getSHI();
	auto state = [&](const char* _what)
	{
		const TWord hcsr = shi.read(SHI::HCSR);
		printf("%-34s PC=%06X OMR=%06X HCSR=%06X (HEN=%u HI2C=%u HM=%u HFIFO=%u HMST=%u HRQE=%u HRNE=%u) instr=%llu\n",
			_what, dsp.getPC().toWord(), dsp.regs().omr.toWord(), hcsr,
			hcsr & 1, (hcsr >> 1) & 1, (hcsr >> 2) & 3, (hcsr >> 5) & 1, (hcsr >> 6) & 1, (hcsr >> 7) & 3,
			(hcsr >> 17) & 1, static_cast<unsigned long long>(dsp.getInstructionCounter()));
	};
	auto run = [&](int _n) { for (int i = 0; i < _n; ++i) dsp.exec(); };

	run(200);
	state("after 200 instr, nothing sent:");

	// the firmware's stage-1 image: 10 words, big-endian 24-bit, MSB first
	for (int k = 0; k < 10; ++k)
	{
		const TWord w = (fw[0x03BCDE + 3*k] << 16) | (fw[0x03BCDE + 3*k + 1] << 8) | fw[0x03BCDE + 3*k + 2];
		shi.exchange(w);
		if (k == 9)
		{
			// single-step the hand-over and print every boundary - the raw evidence, not a summary
			for (int s = 0; s < 40; ++s)
			{
				printf("  step %2d PC=%06X OMR=%06X SR=%06X LC=%06X instr=%llu\n", s, dsp.getPC().toWord(),
					dsp.regs().omr.toWord(), dsp.regs().sr.toWord(), dsp.regs().lc.toWord(),
					static_cast<unsigned long long>(dsp.getInstructionCounter()));
				dsp.exec();
			}
		}
		run(40);
		char tag[64];
		snprintf(tag, sizeof(tag), "sent word %d = %06X:", k, w);
		state(tag);
	}
	run(400);
	state("400 instr after the last word:");

	// EXPERIMENT (probe only): is the stall a stale decode of P:0..7? Re-write the same words
	// through DSP::memWriteP(), the path that notifies the JIT/opcode cache, and step again.
	if (argc > 3)
	{
		for (TWord a = 0; a < 8; ++a) dsp.memWriteP(a, mem.get(MemArea_P, a));
		for (int s = 0; s < 16; ++s)
		{
			printf("  re-step %2d PC=%06X OMR=%06X SR=%06X instr=%llu\n", s, dsp.getPC().toWord(),
				dsp.regs().omr.toWord(), dsp.regs().sr.toWord(), static_cast<unsigned long long>(dsp.getInstructionCounter()));
			dsp.exec();
		}
		state("after re-write + 16 steps:");

		// EXPERIMENT 2: start at P:2 (the bset) directly - is it the block at P:0 or the bset?
		dsp.setPC(2);
		for (int s = 0; s < 12; ++s)
		{
			printf("  from-2 %2d PC=%06X OMR=%06X SR=%06X instr=%llu\n", s, dsp.getPC().toWord(),
				dsp.regs().omr.toWord(), dsp.regs().sr.toWord(), static_cast<unsigned long long>(dsp.getInstructionCounter()));
			dsp.exec();
		}
		// EXPERIMENT 3: start at P:6 (the jmp $FF0000)
		dsp.setPC(6);
		for (int s = 0; s < 6; ++s)
		{
			printf("  from-6 %2d PC=%06X OMR=%06X instr=%llu\n", s, dsp.getPC().toWord(),
				dsp.regs().omr.toWord(), static_cast<unsigned long long>(dsp.getInstructionCounter()));
			dsp.exec();
		}
	}
	printf("P:0..7 =");
	for (TWord a = 0; a < 8; ++a) printf(" %06X", mem.get(MemArea_P, a));
	printf("\nOMR bit 7 (MS) = %u\n", (dsp.regs().omr.toWord() >> 7) & 1);
	return 0;
}
