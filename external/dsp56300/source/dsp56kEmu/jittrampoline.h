#pragma once

#include <cstdint>
#include <vector>

#include "jitasmjithelpers.h"
#include "jittypes.h"

#include "asmjit/core/jitruntime.h"

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
