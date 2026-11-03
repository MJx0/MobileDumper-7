#pragma once

#include "../../IProfile.h"

class DeltaForceProfile : public IProfile
{
public:
	DeltaForceProfile() = default;

	std::vector<std::string> GetSupportedGames() const override
	{
		return {"com.proxima.dfm", "com.garena.game.df", "com.tencent.tmgp.dfm"};
	}

	/*void OverrideSettings(FSettings& Settings) const override
	{
	    Settings.EngineCore.bEnableEncryptedObjectPropertySupport = true;
	}*/

	void OverrideDecryptCallbacks(FDecryptCallbacks& Callbacks) const override
	{
		Callbacks.String.UTF8 = [](char* Data, int32 Len)
		{
			DecryptFNameUTF8(Data, Len);
		};

		Callbacks.String.UTF16 = [](char16_t* Data, int32 Len)
		{
			DecryptFNameUTF16(Data, Len);
		};
	}

	void OverrideSettings(FSettings& Settings) const override
	{
		Settings.CppGenerator.DecryptionUtils.DecryptFNameUTF8FuncBody = R"({
	if (!Data || Len == 0)
		return;

	uint32_t Key = 0;
	switch (Len % 9)
	{
	case 0u:
		Key = ((Len & 0x1F) + Len);
		break;
	case 1u:
		Key = ((Len ^ 0xDF) + Len);
		break;
	case 2u:
		Key = ((Len | 0xCF) + Len);
		break;
	case 3u:
		Key = (33 * Len);
		break;
	case 4u:
		Key = (Len + (Len >> 2));
		break;
	case 5u:
		Key = (3 * Len + 5);
		break;
	case 6u:
		Key = (((4 * Len) | 5) + Len);
		break;
	case 7u:
		Key = (((Len >> 4) | 7) + Len);
		break;
	case 8u:
		Key = ((Len ^ 0xC) + Len);
		break;
	default:
		Key = ((Len ^ 0x40) + Len);
		break;
	}

	for (int32_t i = 0; i < Len; i++)
	{
		Data[i] = (Key & 0x80) ^ (uint8_t)(~Data[i]);
	}
})";

		Settings.CppGenerator.DecryptionUtils.DecryptFNameUTF16FuncBody = R"({
	if (!Data || Len == 0)
		return;

	uint32_t Key = 0;
	switch (Len % 9)
	{
	case 0u:
		Key = ((Len & 0x1F) + Len);
		break;
	case 1u:
		Key = ((Len ^ 0xDF) + Len);
		break;
	case 2u:
		Key = ((Len | 0xCF) + Len);
		break;
	case 3u:
		Key = (33 * Len);
		break;
	case 4u:
		Key = (Len + (Len >> 2));
		break;
	case 5u:
		Key = (3 * Len + 5);
		break;
	case 6u:
		Key = (((4 * Len) | 5) + Len);
		break;
	case 7u:
		Key = (((Len >> 4) | 7) + Len);
		break;
	case 8u:
		Key = ((Len ^ 0xC) + Len);
		break;
	default:
		Key = ((Len ^ 0x40) + Len);
		break;
	}
	uint16_t FinalKey = (Key | 0x7F) + 0x80;
	for (int32_t i = 0; i < Len; i++)
	{
		Data[i] ^= FinalKey;
	}
})";
	}

private:
	// https://github.com/MJx0/AndUEDumper/issues/66

	static void DecryptFNameUTF8(char* Data, int32 Len)
	{
		if (!Data || Len == 0)
			return;

		uint32_t Key = 0;
		switch (Len % 9)
		{
		case 0u:
			Key = ((Len & 0x1F) + Len);
			break;
		case 1u:
			Key = ((Len ^ 0xDF) + Len);
			break;
		case 2u:
			Key = ((Len | 0xCF) + Len);
			break;
		case 3u:
			Key = (33 * Len);
			break;
		case 4u:
			Key = (Len + (Len >> 2));
			break;
		case 5u:
			Key = (3 * Len + 5);
			break;
		case 6u:
			Key = (((4 * Len) | 5) + Len);
			break;
		case 7u:
			Key = (((Len >> 4) | 7) + Len);
			break;
		case 8u:
			Key = ((Len ^ 0xC) + Len);
			break;
		default:
			Key = ((Len ^ 0x40) + Len);
			break;
		}

		for (int32_t i = 0; i < Len; i++)
		{
			Data[i] = (Key & 0x80) ^ (uint8_t)(~Data[i]);
		}
	}

	static void DecryptFNameUTF16(char16_t* Data, int32 Len)
	{
		if (!Data || Len == 0)
			return;

		uint32_t Key = 0;
		switch (Len % 9)
		{
		case 0u:
			Key = ((Len & 0x1F) + Len);
			break;
		case 1u:
			Key = ((Len ^ 0xDF) + Len);
			break;
		case 2u:
			Key = ((Len | 0xCF) + Len);
			break;
		case 3u:
			Key = (33 * Len);
			break;
		case 4u:
			Key = (Len + (Len >> 2));
			break;
		case 5u:
			Key = (3 * Len + 5);
			break;
		case 6u:
			Key = (((4 * Len) | 5) + Len);
			break;
		case 7u:
			Key = (((Len >> 4) | 7) + Len);
			break;
		case 8u:
			Key = ((Len ^ 0xC) + Len);
			break;
		default:
			Key = ((Len ^ 0x40) + Len);
			break;
		}
		uint16_t FinalKey = (Key | 0x7F) + 0x80;
		for (int32_t i = 0; i < Len; i++)
		{
			Data[i] ^= FinalKey;
		}
	}
};