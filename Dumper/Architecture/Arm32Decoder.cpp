#include <cstring>

#ifdef __APPLE__
#include <KittyMemory/KittyAsm.hpp>
#else
#include <KittyMemoryEx/KittyAsm.hpp>
#endif

#include "IArchDecoder.h"

namespace
{

	class Arm32Decoder final : public IArchDecoder
	{
	public:
		EArch Arch() const override { return EArch::Arm32; }
		uint32_t InstructionStride() const override { return KittyArm32::kInstructionStride; }

		// R0-R14. PC (R15) is deliberately not a tracked register index: every
		// PC-relative idiom (ADR-shaped ADD/SUB, LDR-literal) resolves through
		// Insn.target, already computed by KittyArm32::decodeInsn from the
		// instruction's own address - exactly like AArch64's ADR - so nothing here
		// ever needs to look up a "current PC" value from register state.
		int RegisterCount() const override { return KittyArm32::kNumGPRegisters; }

		bool CallClobbers(int Reg) const override
		{
			// AAPCS32: R0-R3 are argument/caller-saved, R4-R11 callee-saved, R12(IP)
			// is a caller-saved scratch register, R14(LR) is overwritten by any call.
			return (Reg >= 0 && Reg <= 3) || Reg == 12 || Reg == 14;
		}

		bool DecodeLocalBytes(const uint8_t* At, size_t Avail, uint64_t Addr, NormalizedInsn& Out) const override
		{
			Out        = {};
			Out.Length = 4;
			if (Avail < 4)
				return false;

			uint32_t Word = 0;
			std::memcpy(&Word, At, 4);

			// This runs tens of millions of times per image and reads only register
			// indices - nothing here ever names an operand, so it costs nothing that
			// a "fast" alternative could have skipped.
			const KittyInsnArm32 Insn = KittyArm32::decodeInsn(Word, static_cast<uint32_t>(Addr));
			if (!Insn.isValid())
				return false;

			const int Rd = Insn.rd;
			const int Rn = Insn.rn;

			switch (Insn.type)
			{
			case EKittyInsnTypeArm32::ADR:
				// The immediate form names its address outright.
				Out.Kind          = NormalizedInsn::EKind::SetBase;
				Out.Dest          = Rd;
				Out.Value         = Insn.target;
				Out.bExactAddress = true;
				return true;

			case EKittyInsnTypeArm32::ADR_REG:
				// `ADD Rd,PC,Rm` - the second half of the PIC idiom, and the most
				// common way this target forms an address. Expressed as Base + Value
				// with Rm as the base and the biased PC as the addend, which keeps
				// r15 out of the register tracker entirely. The subtracting form goes
				// the other way and is not an addition, so it only invalidates Rd.
				Out.Dest = Rd;
				if (Insn.subtractsOffset)
				{
					Out.Kind = NormalizedInsn::EKind::Other;
					return true;
				}
				Out.Kind  = NormalizedInsn::EKind::AddImm;
				Out.Base  = Insn.rt;
				Out.Value = static_cast<uint64_t>(Insn.address) + KittyArm32::kPcBias;
				return true;

			case EKittyInsnTypeArm32::SUB_SP_IMM:
				Out.Kind = NormalizedInsn::EKind::Prologue;
				return true;

			case EKittyInsnTypeArm32::MOV_IMM:
				// A32 builds addresses with MOVW/MOVT or the PIC idiom, so a plain
				// MOV immediate is a scalar constant.
				Out.Kind         = NormalizedInsn::EKind::MoveImm;
				Out.Dest         = Rd;
				Out.Value        = static_cast<uint64_t>(Insn.immediate);
				Out.bComposes    = false;
				Out.bAddressLike = false;
				return true;

			case EKittyInsnTypeArm32::MOV_REG:
				Out.Kind = NormalizedInsn::EKind::MoveReg;
				Out.Dest = Rd;
				Out.Base = Insn.rt;
				return true;

			case EKittyInsnTypeArm32::MOV_SHIFTED:
				// A shift, not a copy: the result is not Rm's value, and the amount
				// is often a runtime register. Invalidate rather than propagate.
				Out.Kind = NormalizedInsn::EKind::Other;
				Out.Dest = Rd;
				return true;

			case EKittyInsnTypeArm32::ADD:
				Out.Dest = Rd;
				// Rd = Rn + Rm is a three-register operation, and NormalizedInsn only
				// models Dest = f(Base); invalidate rather than fabricate a value.
				if (Insn.registerForm)
				{
					Out.Kind = NormalizedInsn::EKind::Other;
					return true;
				}
				Out.Kind  = NormalizedInsn::EKind::AddImm;
				Out.Base  = Rn;
				Out.Value = static_cast<uint64_t>(Insn.immediate);
				return true;

			case EKittyInsnTypeArm32::SUB:
				// Modelled only so the destination is invalidated: SUB is rarely how
				// an address is formed, so no value is fabricated for it.
				Out.Kind = NormalizedInsn::EKind::Other;
				Out.Dest = Rd;
				return true;

			case EKittyInsnTypeArm32::LDR_LITERAL:
				// Rd receives the word *stored at* the pool slot, not the slot's own
				// address. What the word means is left open: in the PIC idiom it is a
				// displacement completed by a following ADR_REG, and calling it an
				// address here would invent a global from every displacement that
				// happens to land inside the image.
				Out.Kind  = NormalizedInsn::EKind::LoadLiteral;
				Out.Dest  = Rd;
				Out.Value = Insn.target;
				return true;

			case EKittyInsnTypeArm32::LDR_PC_REG:
				// `LDR Rd,[PC,Rm]` forms PC+Rm and loads through it in one
				// instruction. Rm is the tracked base and the biased PC rides in
				// Value, so the recorded struct offset stays 0 - this reads a whole
				// slot, not a field of one.
				Out.Kind        = NormalizedInsn::EKind::Load;
				Out.Dest        = Rd;
				Out.Base        = Insn.rt;
				Out.Value       = static_cast<uint64_t>(Insn.address) + KittyArm32::kPcBias;
				Out.Offset      = 0;
				Out.AccessWidth = KittyArm32::loadStoreWidth(Insn.type);
				return true;

			case EKittyInsnTypeArm32::BL:
				Out.Kind  = NormalizedInsn::EKind::Call;
				Out.Value = Insn.target;
				return true;

			case EKittyInsnTypeArm32::B:
			case EKittyInsnTypeArm32::B_COND:
				Out.Kind  = NormalizedInsn::EKind::Branch;
				Out.Value = Insn.target;
				return true;

			case EKittyInsnTypeArm32::BX:
				// A32 has no RET opcode; BX LR is how a function returns. BX through
				// any other register is an indirect jump, not necessarily a return -
				// conservatively not treated as one, since a wrong Return truncates a
				// function boundary.
				Out.Kind = (Insn.rn == KittyArm32::kLinkRegister)
				               ? NormalizedInsn::EKind::Return
				               : NormalizedInsn::EKind::Branch;
				return true;

			case EKittyInsnTypeArm32::PUSH_SP:
				Out.Kind = NormalizedInsn::EKind::Prologue;
				return true;

			case EKittyInsnTypeArm32::POP_PC:
				// Only produced when the register list includes PC, so this is a
				// return rather than a mid-function restore.
				Out.Kind = NormalizedInsn::EKind::Return;
				return true;

			case EKittyInsnTypeArm32::MOVW:
				Out.Kind         = NormalizedInsn::EKind::MoveImm;
				Out.Dest         = Rd;
				Out.Value        = static_cast<uint64_t>(Insn.immediate);
				Out.bComposes    = false;
				Out.bAddressLike = true;
				return true;

			case EKittyInsnTypeArm32::MOVT:
				Out.Kind = NormalizedInsn::EKind::MoveImm;
				Out.Dest = Rd;
				// Already pre-shifted by 16 in the decode, ready to OR in. Widened
				// through uint32_t, not straight from the signed field: MOVT with a
				// top-bit-set imm16 is negative as an int32, and sign-extending that
				// set all 32 high bits and corrupted every composed address.
				Out.Value        = static_cast<uint32_t>(Insn.immediate);
				Out.bComposes    = true;
				Out.bAddressLike = true;
				return true;

			default:
				if (KittyArm32::isLoad(Insn.type) || KittyArm32::isStore(Insn.type))
				{
					Out.Kind = KittyArm32::isLoad(Insn.type) ? NormalizedInsn::EKind::Load
					                                         : NormalizedInsn::EKind::Store;
					Out.Dest = Rd;
					// A register offset is a runtime value, and a post-indexed access
					// happens at [Rn] with its immediate applied afterwards. Neither
					// contributes an offset the tracker can use, and treating either as
					// one places the access at an address the program never forms.
					Out.Base            = Rn;
					Out.Offset          = (Insn.registerOffset || Insn.postIndexed) ? 0 : Insn.immediate;
					Out.AccessWidth     = KittyArm32::loadStoreWidth(Insn.type);
					Out.bWritesBackBase = Insn.writeback;
					return true;
				}
				return false;
			}
		}
	};

} // namespace

std::unique_ptr<IArchDecoder> CreateArm32Decoder()
{
	return std::make_unique<Arm32Decoder>();
}
