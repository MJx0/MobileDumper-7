#pragma once

#include "../../IProfile.h"

// Thanks @droidxrx
// https://github.com/MJx0/MobileDumper-7/issues/10

class NewStateProfile : public IProfile
{
private:
	struct
	{
		// Encryption Keys
		uintptr_t Key64    = 0xD2CA397CEA681802;
		uintptr_t Key32    = 0xB5C76D21;
		uintptr_t FNameKey = 354233679;
	} Config;

public:
	NewStateProfile() = default;

	std::vector<std::string> GetSupportedGames() const override
	{
		return {"com.pubg.newstate"};
	}

	/*void OverrideSettings(FSettings &Settings) const override
	{
	    Settings.Debug.bGenerateAssertionFile = false;
	}*/

	int Decrypt_UObject_Index(uintptr_t Address) const
	{
		uint32 Values[3]{};
		GMemory->ReadBytes(Address, Values, sizeof(Values));

		if (!Values[0] || !Values[1] || !Values[2])
			return 0;

		uint32 EncryptedIndex = Values[0];
		uint32 KeyPart        = Values[2] ^ Values[1] ^ Config.Key32;
		uint32 RotatedKey     = Utils::Memory::Ror(KeyPart, 7);
		uint32 DecryptedIndex = EncryptedIndex ^ RotatedKey;

		return DecryptedIndex;
	}

	uint64 Decrypt_UObject_ClassAndOuter(uintptr_t Address) const
	{
		uint64 Values[3]{};
		GMemory->ReadBytes(Address, Values, sizeof(Values));

		if (!Values[0] || !Values[1] || !Values[2])
			return 0;

		uint64 EncryptedPointer = Values[0];
		uint64 KeyPart          = Values[2] ^ Values[1] ^ Config.Key64;
		uint64 RotatedKey       = Utils::Memory::Ror(KeyPart, 1);
		uint64 DecryptedPointer = EncryptedPointer ^ RotatedKey;

		return DecryptedPointer;
	}

	void OverrideDecryptCallbacks(FDecryptCallbacks& Callbacks) const override
	{
		Callbacks.UObject.Index = [this](const FDecryptContext<int32>& Context)
		{
			return this->Decrypt_UObject_Index(Context.Address);
		};

		Callbacks.UObject.Class = [this](const FDecryptContext<uintptr_t>& Context)
		{
			return this->Decrypt_UObject_ClassAndOuter(Context.Address);
		};

		Callbacks.UObject.Outer = [this](const FDecryptContext<uintptr_t>& Context)
		{
			return this->Decrypt_UObject_ClassAndOuter(Context.Address);
		};
	}

	void DecryptUTF8(char* Data, int32_t Len) const override
	{
		if (!Data || Len <= 0)
			return;

		uint64_t Key = Config.FNameKey;

		for (int32_t i = 0; i < Len; ++i)
		{
			uint8_t Char = static_cast<uint8_t>(Data[i]);

			Char = static_cast<uint8_t>(
			    (((Char >> 1) & 7) |
			     ((Char & 1) << 3) |
			     (((Char >> 1) & 8) << 4) |
			     ((Char >> 5) << 4)) -
			    static_cast<uint8_t>(Key));

			Data[i] = static_cast<char>(Char);

			uint32_t Key32 = static_cast<uint32_t>(Key);

			Key += ((((Key32 >> 13) | (static_cast<uint8_t>(Key >> 8))) & 3) |
			        (((Key32 >> 22) & 0x1C) << 3) |
			        (((Key32 >> 27) & 0x1F) << 3)) ^
			       0x91;

			Key = Utils::Memory::Ror(uint64_t(Key), 56);
		}
	}

	void DecryptUTF16(char16_t* Data, int32_t Len) const override
	{
		if (!Data || Len <= 0)
			return;

		uint64_t Key = Config.FNameKey;

		for (int32_t i = 0; i < Len; ++i)
		{
			uint16_t Char   = static_cast<uint16_t>(Data[i]);
			uint8_t LowByte = static_cast<uint8_t>(Char);

			LowByte = static_cast<uint8_t>(
			    (((LowByte >> 1) & 7) |
			     ((LowByte & 1) << 3) |
			     (((LowByte >> 1) & 8) << 4) |
			     ((LowByte >> 5) << 4)) -
			    static_cast<uint8_t>(Key));

			Data[i] = static_cast<char16_t>((Char & 0xFF00) | LowByte);

			uint32_t Key32 = static_cast<uint32_t>(Key);

			Key += ((((Key32 >> 13) | (static_cast<uint8_t>(Key >> 8))) & 3) |
			        (((Key32 >> 22) & 0x1C) << 3) |
			        (((Key32 >> 27) & 0x1F) << 3)) ^
			       0x91;

			Key = Utils::Memory::Ror(uint64_t(Key), 56);
		}
	}
};