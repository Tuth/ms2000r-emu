#include "jitblock.h"
#include "jitdspmode.h"
#include "jitops.h"
#include "jitregtypes.h"

namespace dsp56k
{
	constexpr bool g_useSRCache = true;

	void JitOps::ccr_set(CCRMask _mask)
	{
		ccr_clearDirty(_mask);
		m_asm.or_(r32(m_dspRegs.getSR(JitDspRegs::ReadWrite)), asmjit::Imm(_mask));
	}

	bool JitOps::smSaturates() const
	{
		const auto* mode = m_block.getMode();
		if(!mode || !mode->testSR(SRB_SM))
			return false;

		// DSP56300FM 3.2.3: saturation applies to Data ALU results going to an accumulator, and is always disabled for
		// TFR, Tcc, DMACsu, DMACuu, MACsu, MACuu, MPYsu, MPYuu, CMPU and the BFU ops. CMP / CMPM / TST write no
		// accumulator, logical ops do not pass the MAC unit.
		switch(m_aluInstruction)
		{
		case Abs: case ADC:
		case Add_SD: case Add_xx: case Add_xxxx: case Addl: case Addr:
		case Asl_D: case Asl_ii: case Asl_S1S2D: case Asr_D: case Asr_ii: case Asr_S1S2D:
		case Dec: case Inc: case Neg: case Rnd: case Sbc:
		case Sub_SD: case Sub_xx: case Sub_xxxx: case Subl: case Subr:
		case Mac_S1S2: case Mac_S: case Maci_xxxx: case Macr_S1S2: case Macr_S: case Macri_xxxx:
		case Mpy_S1S2D: case Mpy_SD: case Mpyi: case Mpyr_S1S2D: case Mpyr_SD: case Mpyri:
			return true;
		case Dmac:
			return getFieldValue<Dmac, Field_S, Field_s>(m_opWordA) == 0;	// DMACss only
		default:
			return false;
		}
	}

	void JitOps::alu_saturateSM(const JitReg64& _alu)
	{
		// Table 3-1: EXT[7], EXT[0], MSP[23] = accumulator bits 55, 48, 47. 000 / 111: unchanged; EXT[7] = 0:
		// $00 7FFFFF FFFFFF, EXT[7] = 1: $FF 800000 000000. V and L are set when it saturates (3-10). The C bit is
		// not affected (5-16). The constants are not scaled (3-10).
		static_assert(g_leftAlignedAlu, "written for the left-aligned accumulator (bit 55 at host bit 63)");

		const auto done = m_asm.newLabel();
		const RegGP t(m_block);
		const RegGP u(m_block);

		m_asm.mov(r64(t), _alu);
		m_asm.shr(r64(t), asmjit::Imm(47 + g_aluBitOffset));	// [8] = bit 55, [1] = bit 48, [0] = bit 47
		m_asm.mov(r64(u), asmjit::Imm(0x103));
		m_asm.and_(r64(t), r64(u));
		m_asm.test_(r64(t));
		m_asm.jz(done);
		m_asm.cmp(r64(t), asmjit::Imm(0x103));
		m_asm.jz(done);

		// bit 55 set: $FF800000000000 = ~$007FFFFFFFFFFF (left-aligned, low 8 bits clear)
		m_asm.mov(r64(t), _alu);
		m_asm.sar(r64(t), asmjit::Imm(63));
		m_asm.mov(r64(u), asmjit::Imm(0x007FFFFFFFFFFFull << g_aluBitOffset));
		m_asm.xor_(r64(u), r64(t));
		m_asm.and_(r64(u), asmjit::Imm(-static_cast<int64_t>(1ll << g_aluBitOffset)));
		m_asm.mov(_alu, r64(u));
		m_asm.or_(r32(m_dspRegs.getSR(JitDspRegs::ReadWrite)), asmjit::Imm(CCR_V));
		m_asm.or_(r32(m_dspRegs.getSR(JitDspRegs::ReadWrite)), asmjit::Imm(CCR_L));

		m_asm.bind(done);
	}

	void JitOps::ccr_dirty(TWord _aluIndex, const JitReg64& _alu, CCRMask _dirtyBits)
	{
		// SM: V means "saturated" now (5-16: an overflow is a result not representable without the extension), so it
		// is not left to the lazy 56-bit V update
		// (IF form, m_disableCCRUpdates: the result still saturates; op_Ifcc puts the CCR back afterwards)
		const bool smAny = smSaturates();
		const bool sm = smAny && !m_disableCCRUpdates;
		if(smAny && !sm)
			alu_saturateSM(_alu);
		if(sm)
			_dirtyBits = static_cast<CCRMask>(_dirtyBits & ~CCR_V);

		m_ccrWritten |= _dirtyBits;

		if(sm)
		{
			m_ccrWritten |= CCR_V;
			// pending bits of the previous op first (they read regLastModAlu), then V cleared and set by the saturation
			updateDirtyCCR(static_cast<CCRMask>(m_ccrDirty & ~_dirtyBits));
			ccr_clear(CCR_V);
			alu_saturateSM(_alu);
		}

		if constexpr(g_useSRCache)
		{
			// if the last dirty call marked bits as dirty that are no longer to be dirtied now, we need to update them
			const auto lastDirty = m_ccrDirty & ~_dirtyBits;
			updateDirtyCCR(static_cast<CCRMask>(lastDirty));

			if(!m_disableCCRUpdates)
			{
				m_block.stack().setUsed(regLastModAlu);
				m_asm.movq(regLastModAlu, _alu);
			}

			m_ccrDirty = static_cast<CCRMask>(m_ccrDirty | _dirtyBits);
		}
		else
		{
			updateDirtyCCR(_alu, _dirtyBits);
		}
	}

	void JitOps::ccr_clearDirty(const CCRMask _mask)
	{
		m_ccrWritten |= _mask;
		m_ccrDirty = static_cast<CCRMask>(m_ccrDirty & ~_mask);
	}

	void JitOps::updateDirtyCCR()
	{
		if(!m_ccrDirty)
			return;

		updateDirtyCCR(m_ccrDirty);
	}

	void JitOps::updateDirtyCCR(const CCRMask _whatToUpdate)
	{
		const auto dirty = m_ccrDirty & _whatToUpdate;
		if(!dirty)
			return;

		const RegGP r(m_block);
		updateDirtyCCRWithTemp(r, static_cast<CCRMask>(dirty));
	}

	void JitOps::updateDirtyCCRWithTemp(const JitRegGP& _temp, const CCRMask _whatToUpdate)
	{
		const auto dirty = m_ccrDirty & _whatToUpdate;
		if(!dirty)
			return;

		m_asm.movq(r64(_temp), regLastModAlu);
		updateDirtyCCR(r64(_temp), static_cast<CCRMask>(dirty));
	}

	void JitOps::updateDirtyCCR(const JitReg64& _alu, CCRMask _dirtyBits)
	{
		CcrBatchUpdate u(*this, _dirtyBits);

		if(_dirtyBits & CCR_V)
		{
			ccr_v_update(_alu);
			m_dspRegs.mask56(_alu);
		}
		if(_dirtyBits & CCR_Z)
		{
			m_asm.test_(_alu);
			ccr_update_ifZero(CCRB_Z);
		}
		if(_dirtyBits & CCR_N)
			ccr_n_update_by55(_alu);
		if(_dirtyBits & CCR_E)
			ccr_e_update(_alu);
		if(_dirtyBits & CCR_U)
			ccr_u_update(_alu);
	}

	void JitOps::ccr_v_update(const JitReg64& _nonMaskedResult)
	{
		{
			const RegScratch signextended(m_block);
			aluSignextendTo64(signextended, _nonMaskedResult);	// accumulator, not a raw 56-bit value
			m_asm.cmp(signextended, _nonMaskedResult);
		}

		ccr_vl_update_ifNotZero();
	}


	void JitOps::checkCondition(const TWord _cc, const std::function<void()>& _true, const std::function<void()>& _false, bool _hasFalseFunc, bool _updateDirtyCCR, bool _releaseRegPool)
	{
		DspValue sr(m_block, PoolReg::DspSR, true, false);

		If(m_block, m_blockRuntimeData, [&](const asmjit::Label& _toFalse)
		{
#ifdef HAVE_ARM64
			const auto cc = decode_cccc(_cc);
			m_block.dspRegPool().releaseNonLocked();
			m_asm.b(reverseCC(cc), _toFalse);
#else
			const auto cc = decode_cccc(_cc);
			m_block.dspRegPool().releaseNonLocked();
			m_asm.j(reverseCC(cc), _toFalse);
#endif
		}, _true, _false, _hasFalseFunc, _updateDirtyCCR, _releaseRegPool);
	}

	JitOps::CcrBatchUpdate::CcrBatchUpdate(JitOps& _ops, const CCRMask _mask) : m_ops(_ops)
	{
		initialize(_mask);
	}

	JitOps::CcrBatchUpdate::CcrBatchUpdate(JitOps& _ops, CCRMask _maskA, CCRMask _maskB) : CcrBatchUpdate(_ops,  static_cast<CCRMask>(_maskA | _maskB)) {}
	JitOps::CcrBatchUpdate::CcrBatchUpdate(JitOps& _ops, CCRMask _maskA, CCRMask _maskB, CCRMask _maskC) : CcrBatchUpdate(_ops,  static_cast<CCRMask>(_maskA | _maskB | _maskC)) {}

	JitOps::CcrBatchUpdate::~CcrBatchUpdate()
	{
		m_ops.m_ccr_update_clear = true;
	}

	void JitOps::CcrBatchUpdate::initialize(CCRMask _mask) const
	{
#ifdef HAVE_ARM64
		const RegScratch scratch(m_ops.getBlock());
		m_ops.m_asm.mov(r32(scratch), asmjit::Imm(~_mask));
		m_ops.m_asm.and_(r32(m_ops.m_dspRegs.getSR(JitDspRegs::ReadWrite)), r32(scratch));
#else
		m_ops.m_asm.and_(r32(m_ops.m_dspRegs.getSR(JitDspRegs::ReadWrite)), asmjit::Imm(~_mask));
#endif

		m_ops.m_ccrDirty = static_cast<CCRMask>(m_ops.m_ccrDirty & ~_mask);
		m_ops.m_ccr_update_clear = false;
	}

	void JitOps::ccr_vl_update_ifEqual(const JitRegGP& _value, const uint64_t _limit)
	{
		// NEG, ABS, INC and DEC can each overflow into exactly one result, so comparing the result with it is
		// the whole overflow test. V takes the outcome and the sticky L follows it.
		const RegScratch limit(m_block);
		m_asm.mov(limit, asmjit::Imm(_limit));
		m_asm.cmp(_value, limit);
#ifdef HAVE_ARM64
		m_asm.cset(limit, asmjit::arm::CondCode::kZero);
#else
		m_asm.set(asmjit::x86::CondCode::kZero, limit.get().r8());
#endif
		ccr_vl_update(limit);
	}
}
