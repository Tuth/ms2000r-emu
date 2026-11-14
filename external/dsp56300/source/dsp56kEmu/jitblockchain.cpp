#include "jitblockchain.h"

#include <atomic>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include <thread>
#include <functional>

#include "dsp.h"
#include "jitasmjithelpers.h"
#include "jitblockruntimedata.h"
#include "jitblock.h"
#include "jitemitter.h"
#include "jitoptimizer.h"
#include "jitprofilingsupport.h"
#include "dsp56kBase/profiler.h"
#include "asmjit/core/jitruntime.h"
#include "jitblockemitter.h"

namespace dsp56k
{
	void funcRun(JitDspPtr* _jit, TWord _pc) noexcept;
	void funcCreate(JitDspPtr* _jit, TWord _pc) noexcept;
	void funcRecreate(JitDspPtr* _jit, TWord _pc) noexcept;

	// 2026-10-12 MS2000 H-bank vocoder crash hunt: nesting depth of JitBlockChain::create (each first-time
	// block execs from inside create(), so a first-time linear run nests one C++ frame per block).
	std::atomic<int> g_jitCreateDepth{ 0 };
	// 2026-10-12 MS2000 memory-appetite hunt: total funcRecreate calls (destroy+re-emit churn,
	// suspected ping-pong around the interrupt-vector area at P:0xFF0000 feeding the asmjit pool)
	std::atomic<uint64_t> g_jitRecreateCount{ 0 };
	// defined in jit.cpp: true while funcCreate/funcRecreate (called from JIT'd code) is on the stack
	extern thread_local bool g_inFuncCreate;

	JitBlockChain::JitBlockChain(Jit& _jit, const JitDspMode& _mode, const size_t _usedFuncSize) : m_jit(_jit), m_mode(_mode)
	{
		m_logger.reset(new AsmJitLogger());
		m_errorHandler.reset(new AsmJitErrorHandler());

		// Initialize MmuArrays with the full P memory size for lazy allocation
		const auto& mem = _jit.dsp().memory();
		const auto pSize = mem.sizeP();

		m_jitFuncs.init(pSize, &funcCreate);
		m_jitFuncs.setResizedCallback([this]() { onFuncsResized(); });

		m_jitCache.init(pSize, [](JitCacheEntry* _ptr, size_t _count)
		{
			for (size_t i = 0; i < _count; ++i)
				new (&_ptr[i]) JitCacheEntry();
		});

		// 2026-10-12 MS2000 memory-appetite hunt: name the per-chain table cost once per chain.
		// With a full 24-bit P space (pSize=0x1000000) each MmuArray reserves the full range
		// virtually and - on Windows - COMMITS the whole backing store at init
		// ((numBlocks+1) * blockBytes, a pagefile-backed section). This print makes the
		// per-chain table anatomy visible: entry size, MMU active or fallback, and the
		// theoretical table bytes. Measured appetite: 4+ GB commit during boot, warden-killed.
		std::fprintf(stderr, "[JIT-CHAIN] created. pSize=0x%zX sizeof(cacheEntry)=%zu B mmu(cache)=%d mmu(funcs)=%d tableBytes(cache)=%llu tableBytes(funcs)=%llu\n",
			pSize, sizeof(JitCacheEntry),
			m_jitCache.usesMMU() ? 1 : 0, m_jitFuncs.usesMMU() ? 1 : 0,
			static_cast<unsigned long long>(pSize) * static_cast<unsigned long long>(sizeof(JitCacheEntry)),
			static_cast<unsigned long long>(pSize) * static_cast<unsigned long long>(sizeof(TJitFunc)));

		if(_usedFuncSize)
			ensureFuncSize(_usedFuncSize);
	}

	JitBlockChain::~JitBlockChain()
	{
		for (size_t i = 0; i < m_jitCache.size(); ++i)
		{
			auto& e = m_jitCache[i];

			if(e.block)
				destroy(e.block);

			releaseSingleOpCache(static_cast<TWord>(i));
		}

		m_jitCache.clear();
	}

	void JitBlockChain::releaseSingleOpCache(const TWord _pc)
	{
		if(_pc >= m_jitCache.size())
			return;

		auto& e = m_jitCache[_pc];

		if(!e.singleOpCache)
			return;

		for (const auto& it : *e.singleOpCache)
			release(it.second);

		delete e.singleOpCache;
		e.singleOpCache = nullptr;
	}

	// 2026-10-12 recreate-pingpong fix: defined here, Jit is only forward-declared in the header
	const std::set<TWord>& JitBlockChain::getForcedSplits() const { return m_jit.getForcedSplits(); }

	bool JitBlockChain::canBeDefaultExecuted(TWord _pc) const
	{
		if(_pc >= m_jitCache.size())
			return false;

		const auto& e = m_jitCache[_pc];
		if (!e.block)
			return false;
		return m_jitFuncs[_pc] == e.block->getFunc() || m_jitFuncs[_pc] == &funcRun;
	}

	void JitBlockChain::occupyArea(JitBlockRuntimeData* _block)
	{
		const auto first = _block->getPCFirst();
		const auto last = first + _block->getPMemSize();

		// 2026-10-13 hunt: log EVERY write to the hot cell - first word AND interior - with the
		// writing thread and the caller. The honest ASM trace proved the dispatch really reads
		// &funcRecreate from this cell, but no instrumented writer has ever been seen writing it.
		if (first <= 0xFF0000 && 0xFF0000 < last)
		{
			static thread_local int s_p = 0;
			if (s_p < 40)
			{
				++s_p;
				std::fprintf(stderr, "[CELL] OCCUPY [0x%06X..+0x%03X) hot=%s tid=%llu caller=%p\n",
					first, static_cast<unsigned>(_block->getPMemSize()), (first == 0xFF0000) ? "FIRST" : "INTERIOR",
					static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id())),
					_ReturnAddress());
			}
		}

		ensureSize(last - 1);

		for (auto i = first; i < last; ++i)
		{
			assert(m_jitCache[i].block == nullptr || m_jitCache[i].block == _block);
			m_jitCache[i].block = _block;
			if (i == first)
				m_jitFuncs[i] = Jit::updateRunFunc(m_jitCache[i]);
			else
				m_jitFuncs[i] = &funcRecreate;
		}

		m_jit.addLoop(_block->getInfo());
	}

	void JitBlockChain::unoccupyArea(const JitBlockRuntimeData* _block)
	{
		const auto first = _block->getPCFirst();
		const auto last = first + _block->getPMemSize();

		ensureSize(last - 1);

		for(auto i=first; i<last; ++i)
		{
			// 2026-10-13 hunt: log every write to the hot cell (both branches; unoccupy writes funcCreate)
			if (i == 0xFF0000)
			{
				static thread_local int s_pu = 0;
				if (s_pu < 40)
				{
					++s_pu;
					std::fprintf(stderr, "[CELL] UNOCCUPY [0x%06X..+0x%03X) tid=%llu caller=%p\n",
						first, static_cast<unsigned>(_block->getPMemSize()),
						static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id())),
						_ReturnAddress());
				}
			}
			m_jitCache[i].block = nullptr;
			m_jitFuncs[i] = &funcCreate;
		}

		const auto& info = _block->getInfo();
		m_jit.removeLoop(info);
	}

	void JitBlockChain::create(const TWord _pc, bool _execute)
	{
	// 2026-10-12 MS2000 H-bank vocoder crash hunt (STACK OVERFLOW): every first-time block runs from
	// inside create() via exec() - each new block nests one C++ frame deeper. A long linear section
	// of first-time code (a program change loading a big vocoder program) nests thousands of frames
	// and kills the stack. Count the nesting, expose it to the crash handler.
	static thread_local int s_createDepth = 0;
	++s_createDepth;
	struct DepthGuard { int* d; ~DepthGuard(){ --*d; } } dg{ &s_createDepth };
	g_jitCreateDepth.store(s_createDepth, std::memory_order_relaxed);
	// when called from JIT'd code (funcCreate), do NOT exec from here: the caller block's ret returns
	// to the trampoline on a flat stack, and the next execOne picks up the freshly compiled block
	const bool runFromHere = _execute && !g_inFuncCreate;

	// 2026-10-13 H-bank wild-pc ride-through: the MS2k program change drives the PC into the
	// external-P hole (measured pc 0x800010 - a valid 24-bit P address with no physical backing on
	// this machine: internal P is 0x18000 words, the boot ROM starts at 0xFF0000). On real silicon
	// the half-uploaded program's transient wild jumps ride through: the garbage fetch still
	// consumes cycles and advances the PC, the pending interrupts come due, and the ISR's stacked
	// return pulls execution out of the hole. A bare return here froze the machine instead
	// (measured: 16 billion dispatches, zero cycle progress, t= stuck at 6.53 s), so mirror the
	// silicon: never compile in the hole, but advance one fetched word and its cycles.
	if (_pc >= 0x18000 && _pc < 0xFF0000)
	{
		static thread_local int s_holeCreate = 0;
		if ((++s_holeCreate & 1048575) == 1)
		{
			const auto& intF = m_jit.dsp().getInterruptFunc();
			std::fprintf(stderr, "[JIT] create: pc 0x%06X in the external-P hole - advancing; interruptFunc=%p isPeriph=%d\n",
				_pc, *reinterpret_cast<void* const*>(&intF),
				intF == m_jit.dsp().getExecPeripheralsFunc() ? 1 : 0);
		}
		m_jit.dsp().fastForward(1, 2);
		m_jit.dsp().setPC(_pc + 1);
		return;
	}

//		LOG("Create @ " << HEX(_pc));// << std::endl << cacheEntry.block->getDisasm());

		ensureCacheSize(_pc+1);

		if(!makeRoomForFirstInstruction(_pc))
		{
			// only a block that is being generated can be in the way, and no code runs while blocks are being generated
			assert(!_execute);
			// 2026-10-12: reaching this from JIT'd code is a livelock candidate (the dispatch loop would
			// re-call funcCreate forever without progress). It should be impossible now that create no
			// longer executes blocks while generating - if this log ever fires, that assumption is wrong.
			static thread_local int s_makeRoomFails = 0;
			if ((++s_makeRoomFails & 1023) == 1)
				LOG("JitBlockChain::create: makeRoomForFirstInstruction FAILED at pc " << HEX(_pc)
					<< " depth " << s_createDepth << " (block being generated is in the way)");
			return;
		}

		auto& cacheEntry = m_jitCache[_pc];

		if(cacheEntry.singleOpCache)
		{
			TWord opA;
			TWord opB;
			m_jit.dsp().memory().getOpcode(_pc, opA, opB);

			// try to find two-word op first
			auto key = JitBlockRuntimeData::getSingleOpCacheKey(opA, opB);

			auto it = cacheEntry.findSingleOp(key);

			uint32_t cacheEntryLen = 2;

			// if not found, try one-word op
			if(!cacheEntry.isValid(it))
			{
				key = JitBlockRuntimeData::getSingleOpCacheKey(opA, JitBlockRuntimeData::SingleOpCacheIgnoreWordB);
				it = cacheEntry.findSingleOp(key);
				cacheEntryLen = 1;
			}

			if(cacheEntry.isValid(it))
			{
				if(cacheEntryLen == 1 || m_jitCache[_pc+1].block == nullptr)
				{
//					LOG("Returning single-op " << HEX(opA) << " at PC " << HEX(_pc));
					assert(cacheEntry.block == nullptr);

					cacheEntry.block = it->second;
					cacheEntry.removeSingleOp(it);

					occupyArea(cacheEntry.block);

					if(runFromHere)
						exec(_pc);
					return;
				}
			}
		}

		emit(_pc);
		if(runFromHere)
			exec(_pc);
	}

	bool JitBlockChain::makeRoomForFirstInstruction(const TWord _pc)
	{
		/*	A block takes its first instruction whole, see JitBlock::getInfo, so a block that starts on a later word of it
			has to go - unless it, or a block that jumps to it, is being generated. Then no block can start at _pc for now.
		*/
		const auto pcEnd = _pc + JitBlock::getInstructionLength(m_jit.dsp(), _pc);

		for(auto pc = _pc + 1; pc < pcEnd; ++pc)
		{
			const auto block = getBlock(pc);

			if(!block)
				continue;

			if(isBeingGeneratedRecursive(block))
				return false;

			// 2026-10-12 recreate-pingpong FIX: a block that already STARTS at _pc and reaches into
			// the first instruction's later words IS a valid block for this pc - the create is
			// redundant. destroyToRecreate skips starts-here blocks for the same reason; this
			// destroy did not, and it fed a destroy/re-emit rotation at ~75k/s (measured: every
			// cycle re-emitted ~256 bytes into the asmjit pool, and the retired code never released
			// while the trampoline stayed entered -> 4+ GB commit, warden kill, no boot).
			// Abort the create; the caller's dispatch re-reads the entry and runs the live block.
			if (block->getPCFirst() == _pc)
				return false;

			// 2026-10-12 memory-appetite hunt: name the destroy side of the suspected block ping-pong
			static thread_local int s_makeRoomDestroys = 0;
			if ((++s_makeRoomDestroys & 1023) == 1)
				std::fprintf(stderr, "[JIT] makeRoom: create @ 0x%06X destroys block @ 0x%06X\n", _pc, block->getPCFirst());

			destroy(block);
		}
		return true;
	}

	void JitBlockChain::exec(const TWord _pc, const TJitFunc& _f) const
	{
		// 2026-10-12 recreate-pingpong hunt: the chain's own dispatch passes the table value into
		// execOne; if that is the recreate thunk, print the moment and the caller.
		if (_f == &funcRecreate)
		{
			static thread_local int s_pce = 0;
			if (s_pce < 20)
			{
				++s_pce;
				std::fprintf(stderr, "[TR] chain-exec RECREATE pc=0x%06X caller=%p\n", _pc, _ReturnAddress());
			}
		}
		m_jit.getTrampoline().execOne(&m_jit.dsp().regs(), _pc, _f);
	}

	void JitBlockChain::notifyPMemWrite(const TWord _addr, const bool _isCurrentChain)
	{
		// 2026-10-12 memory-appetite hunt: a P write into the bootstrap/ROM area at the top of P is a
		// no-op on real silicon; if the model accepts it, every write invalidates the boot blocks and
		// the dispatch re-creates them forever (measured: 28k+ funcRecreate at pc 0xFF0000).
		if (_addr >= 0xFF0000 && _addr < m_jitCache.size())
		{
			static thread_local int s_romWrites = 0;
			if ((++s_romWrites & 1023) == 1)
				std::fprintf(stderr, "[JIT] notifyPMemWrite INTO THE BOOTSTRAP AREA: addr=0x%06X\n", _addr);
		}
		destroy(_addr);
		if(_isCurrentChain)
			ensureFuncSize(_addr);
	}

	void JitBlockChain::destroyParents(JitBlockRuntimeData* _block)
	{
		for (const auto parent : _block->getParents())
		{
			if(parent >= m_jitCache.size())
				continue;

			const auto& e = m_jitCache[parent];

			if (e.block)
				destroy(e.block);

			// single op cached entries that are calling the child block need to go, too. They have been created at a time when _block was not a volatile P block yet
			if(e.singleOpCache)
			{
				for(auto it = e.singleOpCache->begin(); it != e.singleOpCache->end();)
				{
					auto* b = it->second;
					if (b->getChild() == _block->getPCFirst() || b->getNonBranchChild() == _block->getPCFirst())
					{
						release(b);
						e.singleOpCache->erase(it++);
					}
					else
					{
						++it;
					}
				}
			}
		}
		_block->clearParents();
	}

	void JitBlockChain::destroy(JitBlockRuntimeData* _block)
	{
		destroyParents(_block);

		unoccupyArea(_block);

		if(m_jit.getConfig().cacheSingleOpBlocks && _block->getPMemSize() <= 2 && _block->getEncodedInstructionCount() == 1)
		{
			// if a single-word-op, cache it
			const auto first = _block->getPCFirst();
			auto& cacheEntry = m_jitCache[first];
			const auto op = _block->getSingleOpCacheKey();

			if(!cacheEntry.containsSingleOp(op))
			{
//				LOG("Caching single-op block " << HEX(op) << " at PC " << HEX(first));

				cacheEntry.addSingleOp(op, _block);
				return;
			}
		}

//		LOG("Destroying JIT block at PC " << HEX(first) << ", length " << _block->getPMemSize() << ", asm = " << _block->getDisasm());

		release(_block);
	}

	void JitBlockChain::destroy(const TWord _pc)
	{
		if(_pc >= m_jitCache.size())
			return;

		const auto block = m_jitCache[_pc].block;
		if (block)
			destroy(block);
	}

	void JitBlockChain::destroyToRecreate(const TWord _pc)
	{
		if(_pc >= m_jitCache.size())
			return;

		const auto block = m_jitCache[_pc].block;

		if (block && block->getPCFirst() != _pc)
			destroy(block);
	}

	void JitBlockChain::release(JitBlockRuntimeData* _block)
	{
#if DSP56300_DEBUGGER
		auto* d = m_jit.dsp().getDebugger();
		if(d)
			d->onJitBlockDestroyed(m_mode, _block->getInfo().pc);
#endif
		assert(m_codeSize >= _block->codeSize());
		m_codeSize -= _block->codeSize();
		--m_blockCount;	// 2026-10-12 MS2000 memory-appetite hunt: live-block counter for the status line
		m_jit.getTrampoline().releaseCode(_block->getFunc());

		m_jit.releaseBlockRuntimeData(_block);

//		LOG("Total code size now " << (m_codeSize >> 10) << "kb");
	}

	void JitBlockChain::recreate(const TWord _pc)
	{
		// 2026-10-12 recreate-pingpong hunt: name the covering block before the destroy, on pair
		// transitions only (capped) - this identifies the other side of any destroy/re-emit rotation.
		if(_pc < m_jitCache.size())
		{
			const auto cover = m_jitCache[_pc].block;
			if (cover)
			{
				static thread_local TWord s_lastPc = 0xFFFFFF;
				static thread_local TWord s_lastCover = 0xFFFFFF;
				static thread_local int s_prints = 0;
				if (s_prints < 200 && (s_lastPc != _pc || s_lastCover != cover->getPCFirst()))
				{
					s_lastPc = _pc; s_lastCover = cover->getPCFirst(); ++s_prints;
					std::fprintf(stderr, "[JIT] recreate @ 0x%06X: covering block [0x%06X..+0x%03X)%s\n",
						_pc, cover->getPCFirst(), static_cast<unsigned>(cover->getPMemSize()),
						(cover->getPCFirst() == _pc) ? " starts-here" : "");
				}
			}
		}

		// 2026-10-12 recreate-pingpong fix: hot-entry bookkeeping - at the threshold the pc becomes a
		// forced block boundary in the planner and no future block can cover it again
		m_jit.markRecreateEntry(_pc);

		// there is code, but the JIT block does not start at the PC position that we want to run. We need to throw the block away and regenerate
//		LOG("Unable to jump into the middle of a block, destroying existing block & recreating from " << HEX(pc));
		m_jit.destroyToRecreate(_pc);
		create(_pc, true);
	}

	JitBlockRuntimeData* JitBlockChain::getChildBlock(JitBlockRuntimeData* _parent, TWord _pc, bool _allowCreate/* = true*/)
	{
		if (!m_jit.getConfig().linkJitBlocks)
			return nullptr;

		// MS2000 VOC-DEATH-3: a link back to the parent's own range or below closes a loop; such a loop would run
		// linked without returning to the dispatcher, where pending interrupts are taken. Forward links stay.
		if (!m_jit.getConfig().linkBackwardJitBlocks && _parent && _pc < _parent->getPCFirst() + _parent->getPMemSize())
			return nullptr;

		if (_parent)
			occupyArea(_parent);

		if (m_jit.isVolatileP(_pc))
			return nullptr;

		if(!_allowCreate && _pc >= m_jitCache.size())
			return nullptr;

		ensureSize(_pc);

		{
			const auto& e = m_jitCache[_pc];

			if (e.block && e.block->getPCFirst() == _pc)
			{
				// block is still being generated (circular reference)
				if (m_jitFuncs[_pc] == nullptr)
					return nullptr;

				if (!canBeDefaultExecuted(_pc))
					return nullptr;

				return e.block;
			}

			if (!_allowCreate)
				return nullptr;

			// If we jump into the middle of an existing block, this block needs to be regenerated.
			// However, we can only destroy blocks that are not part of the recursive generation that is running at the moment
			if (isBeingGeneratedRecursive(e.block))
				return nullptr;

			// regenerate otherwise
			if (e.block)
				destroy(e.block);
		}

		create(_pc, false);

		if (!canBeDefaultExecuted(_pc))
			return nullptr;

		// if the block covers any volatile P, do not return it as a child block. If this block is recreated because
		// it is overwritten, it will cause recreations of all parent blocks, we don't want that
		const auto& e = m_jitCache[_pc];
		for(TWord i=_pc; i<_pc + e.block->getPMemSize(); ++i)
		{
			if(m_jit.isVolatileP(i))
				return nullptr;
		}
		return e.block;
	}

	JitBlockRuntimeData* JitBlockChain::emit(TWord _pc)
	{
		/*	Without dynamicFastInterrupts, every block below Vba_End is compiled as a fast interrupt: two words at most and
			no PC update, the interrupted program's PC is restored by the caller. Blocks are compiled on first execution and
			an interrupt sets the processing mode before its vector runs, so a vector block compiled in any other mode is
			ordinary code in the vector region, reached by a jump or call. It would never advance the PC and the DSP spins
			on it without any other symptom, so say so.
		*/
#ifndef NDEBUG
		if(_pc < Vba_End && !m_jit.getConfig().dynamicFastInterrupts && m_jit.dsp().getProcessingMode() != DSP::FastInterrupt)
		{
			LOG("Ordinary code runs in the interrupt vector region at " << HEX(_pc) << " but JitConfig::dynamicFastInterrupts is "
				"disabled. The JIT compiles it as fast interrupt code that does not advance the PC and the DSP will hang there. "
				"Enable dynamicFastInterrupts for this device.");
			assert(false && "ordinary code in the vector region with dynamicFastInterrupts disabled");
		}
#endif

		const ProfilerScope profilerScope("JitBlockChain::emit");

		auto* emitter = m_jit.acquireEmitter(_pc);

//		m_logger->addFlags(asmjit::FormatFlags::kHexImms | /*asmjit::FormatFlags::kHexOffsets |*/ asmjit::FormatFlags::kMachineCode);
//		emitter->codeHolder.setLogger(m_logger.get());

		if(m_jit.getConfig().asmjitDiagnostics)
		{
			emitter->emitter.addDiagnosticOptions(asmjit::DiagnosticOptions::kValidateIntermediate);
			emitter->emitter.addDiagnosticOptions(asmjit::DiagnosticOptions::kValidateAssembler);
		}

		emitter->codeHolder.setErrorHandler(m_errorHandler.get());
		emitter->codeHolder.init(m_jit.getRuntime()->environment());
		emitter->codeHolder.attach(&emitter->emitter);

		auto* b = m_jit.acquireBlockRuntimeData();

		m_errorHandler->setBlock(b);

		m_generatingBlocks.insert(std::make_pair(_pc, b));

		if(!emitter->block.emit(*b, this, _pc, m_jitCache, m_jit.getVolatileP(), m_jit.getLoops(), m_jit.getLoopEnds(), m_jit.getProfilingSupport()))
		{
			LOG("FATAL: code generation failed for PC " << HEX(_pc));
			m_jit.releaseBlockRuntimeData(b);
			m_generatingBlocks.erase(_pc);
			m_jit.releaseEmitter(emitter);
			return nullptr;
		}

		m_generatingBlocks.erase(_pc);

		if(m_jit.getConfig().enableOptimizer)
		{
			JitOptimizer optimizer(emitter->emitter);
			optimizer.optimize();
		}

		emitter->emitter.finalize();

		TJitFunc func;

		const auto err = m_jit.getRuntime()->add(&func, &emitter->codeHolder);

		if(err)
		{
			const auto* const errString = asmjit::DebugUtils::errorAsString(err);
			LOG("JIT failed: " << err << " - " << errString << "PC " << HEX(_pc));
			m_jit.releaseEmitter(emitter);
			return nullptr;
		}

		b->finalize(func, emitter->codeHolder);
		m_codeSize += emitter->codeHolder.codeSize();
		++m_blockCount;	// 2026-10-12 MS2000 memory-appetite hunt: live-block counter for the status line

		m_jit.releaseEmitter(emitter);

//		LOG("Total code size now " << (m_codeSize >> 10) << "kb");

		occupyArea(b);

		auto* profiling = m_jit.getProfilingSupport();
		if (profiling)
			profiling->addJitBlock(*b);

#if DSP56300_DEBUGGER
		auto* d = m_jit.dsp().getDebugger();
		if(d)
			d->onJitBlockCreated(m_mode, b);
#endif
		return b;
	}

	bool JitBlockChain::isBeingGeneratedRecursive(const JitBlockRuntimeData* _block) const
	{
		if (!_block)
			return false;

		if (isBeingGenerated(_block))
			return true;

		for (const auto parent : _block->getParents())
		{
			if(parent >= m_jitCache.size())
				continue;

			const auto& e = m_jitCache[parent];
			if (isBeingGeneratedRecursive(e.block))
				return true;
		}
		return false;
	}

	bool JitBlockChain::isBeingGenerated(const JitBlockRuntimeData* _block) const
	{
		if (_block == nullptr)
			return false;

		for (const auto& it : m_generatingBlocks)
		{
			if (it.second == _block)
				return true;
		}
		return false;
	}

	bool JitBlockChain::ensureSize(const size_t _address)
	{
		const auto a = m_jitCache.ensureBlockForIndex(_address);
		const auto b = m_jitFuncs.ensureBlockForIndex(_address);
		return a || b;
	}

	void JitBlockChain::onFuncsResized() const
	{
		m_jit.onFuncsResized(*this);
	}

	bool JitBlockChain::ensureCacheSize(const size_t _address)
	{
		return m_jitCache.ensureBlockForIndex(_address);
	}

	bool JitBlockChain::ensureFuncSize(const size_t _address)
	{
		return m_jitFuncs.ensureBlockForIndex(_address);
	}
}
