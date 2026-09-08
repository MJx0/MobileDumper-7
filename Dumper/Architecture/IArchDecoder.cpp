#include "IArchDecoder.h"

/// @brief Creates the ARM64 decoder.
extern std::unique_ptr<IArchDecoder> CreateArm64Decoder();

/// @brief Creates the ARM32 (A32) decoder.
extern std::unique_ptr<IArchDecoder> CreateArm32Decoder();

std::unique_ptr<IArchDecoder> CreateArchDecoder(EArch Arch)
{
	switch (Arch)
	{
	case EArch::Arm64:
		return CreateArm64Decoder();
	case EArch::Arm32:
		return CreateArm32Decoder();
	default:
		return nullptr;
	}
}
