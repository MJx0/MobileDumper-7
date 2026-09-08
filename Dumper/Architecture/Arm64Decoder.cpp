#include <cstring>

#ifdef __APPLE__
#include <KittyMemory/KittyAsm.hpp>
#else
#include <KittyMemoryEx/KittyAsm.hpp>
#endif

#include "IArchDecoder.h"

namespace
{

	class Arm64Decoder final : public IArchDecoder
	{
	public:
		EArch Arch() const override { return EArch::Arm64; }
		uint32_t InstructionStride() const override { return KittyArm64::kInstructionStride; }
		int RegisterCount() const override { return KittyArm64::kNumGPRegisters; }

		bool CallClobbers(int Reg) const override
		{
			// AAPCS64: X0-X18 are caller-saved, X19-X28 callee-saved, X30 is LR.
			// The callee-saved range matters - those are exactly the registers the
			// compiler picks for a base hoisted across a call or a loop.
			return (Reg >= 0 && Reg <= 18) || Reg == 30;
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
			const KittyInsnArm64 Insn = KittyArm64::decodeInsn(Word, Addr);
			if (!Insn.isValid())
				return false;

			const int Rd = Insn.rd;
			const int Rn = Insn.rn;
			const int Rt = Insn.rt;

			switch (Insn.type)
			{
			case EKittyInsnTypeArm64::RET:
				Out.Kind = NormalizedInsn::EKind::Return;
				return true;

			case EKittyInsnTypeArm64::STP_PRE_SP:
			case EKittyInsnTypeArm64::SUB_SP_IMM:
				Out.Kind = NormalizedInsn::EKind::Prologue;
				return true;

			case EKittyInsnTypeArm64::MOV:
				// MOV Xd,Xm is ORR Xd,XZR,Xm, which the base decoder does not model.
				// Dropping it is not harmless: compilers routinely copy `this` or a
				// base pointer into a callee-saved register at function entry, so
				// losing the copy loses the value everywhere downstream.
				//
				// No manual "is the source XZR" check is needed here: the index is
				// already -1 for XZR/WZR, and MoveReg's Src<0 handling (every
				// consumer) already treats that as "untracked" - the same net effect
				// the old explicit check produced, for free.
				Out.Kind = NormalizedInsn::EKind::MoveReg;
				Out.Dest = Rd;
				Out.Base = Insn.rm;
				return true;

			case EKittyInsnTypeArm64::ADR:
			case EKittyInsnTypeArm64::ADRP:
				Out.Kind  = NormalizedInsn::EKind::SetBase;
				Out.Dest  = Rd;
				Out.Value = Insn.target;
				// ADR names the address outright; ADRP only names its 4K page and
				// needs a completing ADD/LDR/STR.
				Out.bExactAddress = (Insn.type == EKittyInsnTypeArm64::ADR);
				return true;

			case EKittyInsnTypeArm64::ADD:
				Out.Kind  = NormalizedInsn::EKind::AddImm;
				Out.Dest  = Rd;
				Out.Base  = Rn;
				Out.Value = static_cast<uint64_t>(Insn.immediate);
				return true;

			case EKittyInsnTypeArm64::MOVZ:
			case EKittyInsnTypeArm64::MOVK:
			case EKittyInsnTypeArm64::MOVN:
				Out.Kind      = NormalizedInsn::EKind::MoveImm;
				Out.Dest      = Rd;
				Out.Value     = static_cast<uint64_t>(Insn.immediate);
				Out.bComposes = (Insn.type == EKittyInsnTypeArm64::MOVK);
				// AArch64 forms addresses with ADRP/ADD; MOVZ/MOVK here are scalar
				// constants such as NumElementsPerChunk.
				Out.bAddressLike = false;
				return true;

			case EKittyInsnTypeArm64::SUB:
				// Modelled only so the destination is invalidated.
				Out.Kind = NormalizedInsn::EKind::Other;
				Out.Dest = Rd;
				return true;

			case EKittyInsnTypeArm64::BL:
				Out.Kind  = NormalizedInsn::EKind::Call;
				Out.Value = Insn.target; // callee, for call-graph construction
				return true;

			case EKittyInsnTypeArm64::B:
				Out.Kind  = NormalizedInsn::EKind::Branch;
				Out.Value = Insn.target; // tail calls reach a callee this way too
				return true;

			case EKittyInsnTypeArm64::B_COND:
			case EKittyInsnTypeArm64::CBZ:
			case EKittyInsnTypeArm64::CBNZ:
			case EKittyInsnTypeArm64::TBZ:
			case EKittyInsnTypeArm64::TBNZ:
				Out.Kind = NormalizedInsn::EKind::Branch;
				return true;

			default:
			{
				const bool bLoad = KittyArm64::isLoad(Insn.type);
				if (bLoad || KittyArm64::isStore(Insn.type))
				{
					Out.Kind = bLoad ? NormalizedInsn::EKind::Load : NormalizedInsn::EKind::Store;
					Out.Dest = Rt; // transfer register
					// A register offset is a runtime value, so the address cannot be
					// resolved: leave the base untracked, which records nothing and
					// still invalidates the destination.
					Out.Base = Insn.registerOffset ? -1 : Rn;
					// A post-indexed access happens at [Rn]; its immediate is the
					// write-back amount applied afterwards, not an access offset.
					Out.Offset          = Insn.postIndexed ? 0 : static_cast<int64_t>(Insn.immediate);
					Out.AccessWidth     = KittyArm64::loadStoreWidth(Insn.type);
					Out.bWritesBackBase = Insn.writeback;
					return true;
				}
				return false;
			}
			}
		}
	};

} // namespace

std::unique_ptr<IArchDecoder> CreateArm64Decoder()
{
	return std::make_unique<Arm64Decoder>();
}
