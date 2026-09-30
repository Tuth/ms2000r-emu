#include "opcodecycles.h"

#include <iterator>
#include <cstdlib>
#include <cstring>

#include "opcodes.h"
#include "registers.h"

namespace dsp56k
{
	static_assert(std::size(g_cycles) == std::size(g_opcodes));

	namespace
	{
		constexpr bool validateCycles()
		{
			for(size_t i=0; i<std::size(g_cycles); ++i)
			{
				if(static_cast<size_t>(g_cycles[i].inst) != i)
					return false;
			}
			return true;
		}
	}

	static_assert(validateCycles());

	uint32_t calcCycles(const Instruction _inst, const TWord _pc, const TWord _op, const TWord _extMemAddress, const TWord _extMemWaitStates)
	{
		if(_inst == Invalid)
			return 0;

		const auto& cycles = g_cycles[static_cast<uint32_t>(_inst)];

		/*	Table A-1 has two rows for an absolute short MOVE: "MOVE [x or y]:aa,D" with one clock cycle, and a row that
			is garbled to "MOVE [x or y]aa" with two. Every other MOVE has a row per direction, so the second one is the
			write. Both directions used to count two, which made firmware whose audio loop is timed to a number of ESAI
			frames overrun them.
		*/
		if((_inst == Movex_aa || _inst == Movey_aa) && getFieldValue(_inst, Field_W, _op))
			return 1;

		auto c = std::max(cycles.cycles, 1u);

		if(cycles.pru || cycles.lab || cycles.lim)
		{
			// pru: Pre-update specifies clock cycles added for using the pre-update addressing modes (pre-decrement and offset by N addressing modes)
			// lab: Long absolute specifies clock cycles added for using the Long Absolute Address

			TWord mmm = MMM_Rn;
			if(hasField(_inst, Field_MM))
				mmm = getFieldValue(_inst, Field_MM, _op);
			else if(hasField(_inst, Field_MMM))
				mmm = getFieldValue(_inst, Field_MMM, _op);
			else
				assert(false && "pru != 0 but no MMM field found");

			const auto eaMode = static_cast<EffectiveAddressingMode>(mmm);

			switch (eaMode)
			{
			/*	DSP56300FM rev.5 RENDERED p.425 (printed A-1): T is for "the Post-Update mode (post-increment,
				post-decrement and post offset by N) or the No-Update mode"; "+ pru" is for "the pre-update
				addressing modes (pre-decrement and offset by N addressing modes)". RENDERED p.75 (printed 4-7):
				(Rn)+Nn and (Rn)-Nn are Post-Increment/Post-Decrement By Offset Nn, (Rn+Nn) is Indexed By Offset
				Nn and -(Rn) is Pre-Decrement By One. So only 101 and 111 pay pru; charging it for 000/001 too
				counted every (Rn)+Nn / (Rn)-Nn move one cycle too many (MS2000 VOCODER-1).
			*/
			case MMM_RnMinusNn:			// 000 (Rn)-Nn: post-update, pru only in the legacy A/B model
			case MMM_RnPlusNn:			// 001 (Rn)+Nn: post-update, pru only in the legacy A/B model
				{ static const bool legacy = [] { const char* e = std::getenv("MS2K_PRUPOST"); return e && std::strcmp(e, "legacy") == 0; }();
				  if (legacy) c += cycles.pru; }
				break;
			case MMM_RnPlusNnNoUpdate:	// 101 (Rn+Nn)	
			case MMM_MinusRn:			// 111 -(Rn)
				c += cycles.pru;
				break;
			case MMM_AbsAddr:
				{
					/*	MMM = 110 is two modes told apart by RRR: 000 Absolute Address, 100 Immediate Data
						(DSP56300FM rev.5 section 4.4.4, printed p.4-8). Table A-1's "+ lab" is added for the Long
						Absolute Address mode and "+ lim" for the long immediate data mode (printed p.A-1/A-2),
						so an instruction pays one of them, never both. Adding both counted every long absolute
						or long immediate move one cycle too many.
					*/
					TWord rrr = 0;
					if(hasField(_inst, Field_RRR))
						rrr = getFieldValue(_inst, Field_RRR, _op);
					c += (rrr == (MMMRRR_ImmediateData & 7)) ? cycles.lim : cycles.lab;
				}
				break;
			default:
				break;
			}
		}

		return c;
	}

	uint32_t calcCycles(const Instruction _instA, const Instruction _instB, const TWord _pc, const TWord _op, const TWord _extMemAddress, const TWord _extMemWaitStates)
	{
		const auto cyclesA = calcCycles(_instA, _pc, _op, _extMemAddress, _extMemWaitStates);
		const auto cyclesB = calcCycles(_instB, _pc, _op, _extMemAddress, _extMemWaitStates);

		return std::max(cyclesA, cyclesB);
	}
}
