#pragma once

#include <cstdint>
#include <vector>
#include <cstdio>
#ifdef _MSC_VER
#include <intrin.h>
#else
#define _ReturnAddress() __builtin_return_address(0)
#endif

#include "jitasmjithelpers.h"
#include "jittypes.h"

#include "asmjit/core/jitruntime.h"

namespace dsp56k
{
	// 2026-10-12 recreate-pingpong hunt: defined in jit.cpp
	void funcRecreate(JitDspPtr* _jit, TWord _pc) noexcept;
}

namespace dsp56k
{
	class DSP;

	class JitTrampoline
	{
	public:
		typedef void (*TExecLoopFunc)(DSP*, uint32_t) noexcept;				// DSP, iteration count
		typedef void (*TExecOneFunc)(JitDspPtr*, TWord, TJitFunc) noexcept;	// DspRegs, PC, function to be called
		typedef void (*TExecUntilCyclesFunc)(DSP*, uint64_t) noexcept;		// DSP, exclusive cycle target (MS2000 PERF-DSP-2)

		static constexpr uint32_t UnrollShift = 3;
		static constexpr uint32_t UnrollSize = 1<<UnrollShift;

		JitTrampoline(DSP& _dsp);

		void generateCode()
		{
			generateExecLoopFunc();
			generateExecOneFunc();
			generateExecUntilCyclesFunc();

			// 2026-10-13 hunt: the ASM's baked offsets are relative to THIS &m_dsp. If the DSP
			// object moves after generation (move/copy in the host wrapper), every [dsp+off] read
			// hits a wrong cell - measured: the dispatch's "entries base" reads a module .data
			// address instead of the table base, and the indexed read then finds &funcRecreate in
			// a .data cell - the entire recreate storm.
			std::fprintf(stderr, "[TRAMP] genDspAddr=%p execLoop=%p execOne=%p execUntilCycles=%p funcRecreate=%p\n",
				reinterpret_cast<void*>(&m_dsp),
				reinterpret_cast<void*>(m_funcExecLoop),
				reinterpret_cast<void*>(m_funcExecOne),
				reinterpret_cast<void*>(m_funcExecUntilCycles),
				reinterpret_cast<void*>(&funcRecreate));

			// 2026-10-13 hunt: hexdump the generated execUntilCycles dispatch - the ASM reads
			// &funcRecreate from a cell the C++ reads as the block func; decode the real encoding.
			{
				const auto* p = reinterpret_cast<const unsigned char*>(m_funcExecUntilCycles);
				if (p != nullptr)
				{
					std::fprintf(stderr, "[TRAMP] execUntilCycles bytes:");
					for (size_t i = 0; i < 192; ++i)
						std::fprintf(stderr, "%02X", p[i]);
					std::fprintf(stderr, "\n");
				}
			}
		}

		void exec(DSP* _dsp, uint32_t _count) noexcept
		{
			assert(((_count >> UnrollShift) << UnrollShift) == _count);
			++m_running;
			m_funcExecLoop(_dsp, _count >> UnrollShift);
			stopped();
		}

		void execOne(JitDspPtr* _jit, const TWord _pc, const TJitFunc _func) noexcept
		{
			// 2026-10-12 recreate-pingpong hunt: the recreate storm is dispatched through this
			// trampoline entry while every table-reading dispatcher is instrumented and silent.
			// The C++ caller's return address (a module address) names the dispatch path.
#ifdef _WIN32
			if (_func == &funcRecreate)
			{
				static thread_local int s_p = 0;
				if (s_p < 10)
				{
					++s_p;
					std::fprintf(stderr, "[TR] execOne(funcRecreate) pc=0x%06X caller=%p\n", _pc, _ReturnAddress());
				}
			}
#endif
			++m_running;
			m_funcExecOne(_jit, _pc, _func);
			stopped();
		}

		/*	MS2000 PERF-DSP-2: blocks under ONE trampoline entry until the DSP cycle counter reaches _targetCycles - the
			same sequence as "while(cycles < target) dsp.exec()": before every block the interrupt/peripheral function
			(with the exec loop's inlined "no peripheral due" test, which is execPeriph()'s own first test), then the
			block at the current PC. Idea from joelanders/dsp56300-md-mm ("Add cycle-bounded JIT trampoline execution").
			x64 only; elsewhere the function is not generated and DSP::execUntilCycles loops over exec(). */
		bool hasExecUntilCycles() const noexcept { return m_funcExecUntilCycles != nullptr; }
		void execUntilCycles(DSP* _dsp, const uint64_t _targetCycles) noexcept
		{
			++m_running;
			m_funcExecUntilCycles(_dsp, _targetCycles);
			stopped();
		}

		/*	Generated code calls peripherals and external bus devices, and those can destroy blocks by writing P memory. The
			block that made the call can be one of them, or it jumps to one of them when it ends, so the code of a destroyed
			block is released once no generated code runs anymore.
		*/
		void releaseCode(TJitFunc _func);

	private:
		void stopped() noexcept
		{
			if(!--m_running && !m_retiredCode.empty())
				releaseRetiredCode();
		}

		void releaseRetiredCode() noexcept;

		void initCodeHolder(asmjit::CodeHolder& _codeHolder);
		void generateExecLoopFunc();
		void generateExecOneFunc();
		void generateExecUntilCyclesFunc();

		DSP& m_dsp;
		asmjit::JitRuntime m_runtime;
		AsmJitLogger m_logger;
		AsmJitErrorHandler m_errorHandler;
		TExecLoopFunc m_funcExecLoop = nullptr;
		TExecOneFunc m_funcExecOne = nullptr;
		TExecUntilCyclesFunc m_funcExecUntilCycles = nullptr;

		uint32_t m_running = 0;					// exec and execOne nest, generated code runs while this is not zero
		std::vector<TJitFunc> m_retiredCode;
	};
}
