#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../Engine/OffsetFinder/DecryptCallbacks.h"
#include "../Engine/OffsetFinder/Offsets.h"
#include "../Engine/Unreal/ObjectArray.h"
#include "../Memory/IMemory.h"
#include "../Architecture/IArchDecoder.h"
#include "../Settings.h"
#include "../Utils/Logger.h"
#include "../Utils/Utils.h"

/**
 * @brief Base interface for game-specific profiles.
 */
class IProfile
{
public:
	IProfile()          = default;
	virtual ~IProfile() = default;

protected:
	IProfile(const IProfile&)                = default;
	IProfile(IProfile&&) noexcept            = default;
	IProfile& operator=(const IProfile&)     = default;
	IProfile& operator=(IProfile&&) noexcept = default;

public:
	/**
	 * @brief Returns the game identifier.
	 */
	virtual std::string GetGameIdentifier() const;

	/**
	 * @brief Returns the game version.
	 */
	virtual std::string GetGameVersion() const;

	/**
	 * @brief Returns the games supported by this profile.
	 */
	inline virtual std::vector<std::string> GetSupportedGames() const
	{
		return {GetGameIdentifier()};
	}

	/// @brief Address of GUObjectArray or ObjObjects.
	inline virtual uintptr_t GetGObjects() const
	{
		uintptr_t Sym = GMemory->FindUnrealSymbol("GUObjectArray");
		if (Sym != 0)
			GLogger.FmtWrite(ELogLevel::Info, "GetGObjects: GObjects Resolved via exported symbol \"GUObjectArray\"\n");
		return Sym;
	}

	/**
	 * @brief Decrypts the pointer returned by @ref GetGObjects().
	 * @param[in,out] ObjectsPtr GObjects pointer to decrypt.
	 */
	inline virtual void DecryptGObjects(uintptr_t& ObjectsPtr) const { ((void)ObjectsPtr); }

	/// @brief Resolves the layout of the global objects structure.
	/// @param ObjectsPtr Address of the global objects structure.
	/// @param Out Receives the resolved objects layout on success; set to @c nullptr on failure.
	/// @param OutError Optional. Receives why resolution failed, possibly several lines separated by '\n'. Left untouched on success.
	/// @return @c true if a fixed or chunked objects layout was successfully resolved; otherwise @c false.
	inline virtual bool ResolveGObjectsLayout(uintptr_t ObjectsPtr, std::unique_ptr<IObjectsLayout>& Out, std::string* OutError = nullptr) const
	{
		Out.reset();

		LayoutDetection::FObjectsDetectionResult Result = LayoutDetection::DetectObjectsLayout(ObjectsPtr);

		if (!Result.bSuccess)
		{
			if (OutError)
			{
				*OutError = Result.Summary;
				for (const std::string& Line : Result.Details)
					OutError->append("\n").append(Line);
			}

			return false;
		}

		for (const std::string& Line : Result.Details)
			GLogger.FmtWrite(ELogLevel::Debug, "ResolveGObjectsLayout: {}\n", Line);

		GLogger.FmtWrite(ELogLevel::Info, "ResolveGObjectsLayout: Detected {}\n", Result.Summary);
		Out = std::move(Result.Layout);
		return true;
	}

	/// @brief Address of GNames or NamePoolData.
	inline virtual uintptr_t GetGNames() const
	{
		uintptr_t GNameBlocksDebug = GMemory->FindUnrealSymbol("GNameBlocksDebug");
		if (GNameBlocksDebug != 0)
		{
			uintptr_t NamePoolBlocks = GMemory->Read<uintptr_t>(GNameBlocksDebug);
			if (NamePoolBlocks != 0)
			{
				GLogger.FmtWrite(ELogLevel::Info, "GetGNames: GNames Resolved via exported symbol \"GNameBlocksDebug\"\n");

				// Safe to hardcode offset here.
#ifdef __APPLE__
				return NamePoolBlocks - 0xD0;
#else
#ifdef __LP64__
				return NamePoolBlocks - 0x40;
#else
				return NamePoolBlocks - 0x30;
#endif
#endif
			}
		}

		uintptr_t GFNameTable = GMemory->FindUnrealSymbol("GFNameTableForDebuggerVisualizers_MT");
		if (GFNameTable != 0)
		{
			GLogger.FmtWrite(ELogLevel::Info, "GetGNames: GNames Resolved via exported symbol \"GFNameTableForDebuggerVisualizers_MT\"\n");
			return GFNameTable;
		}

		uintptr_t GNamesGetIsInitialized = GMemory->FindUnrealSymbol("_ZN5FName16GetIsInitializedEv");
		if (GNamesGetIsInitialized != 0)
		{
			std::vector<uint32_t> Insns(8, 0);
			GMemory->ReadBytes(GNamesGetIsInitialized, Insns.data(), Insns.size() * sizeof(uint32_t));
#if defined(__LP64__)
			// ADRL X0, addr  →  ADRP X0, page ; ADD X0, X0, #off
			uintptr_t Address = Utils::Arm64::Find_ADRP_Final_Address(Insns, GNamesGetIsInitialized);
#else
			// LDR R0, [PC, #off] ; ADD R0, PC, R0
			uintptr_t Address = Utils::Arm32::Find_LDR_ADD_PC_Address(Insns, GNamesGetIsInitialized, GMemory.get());
#endif
			if (Address != 0)
			{
				Address = Utils::Memory::AlignUp(Address + 1, static_cast<uintptr_t>(sizeof(uintptr_t)));
				GLogger.FmtWrite(ELogLevel::Info, "GetGNames: GNames Resolved via exported symbol \"_ZN5FName16GetIsInitializedEv\"\n");
				return Address;
			}
		}

		return 0;
	}

	/**
	 * @brief Decrypts the pointer returned by @ref GetGNames().
	 * @param[in,out] NamesPtr GNames pointer to decrypt.
	 */
	inline virtual void DecryptGNames(uintptr_t& NamesPtr) const { ((void)NamesPtr); }


	/// @brief Resolves the layout of the global names structure.
	/// @param NamesPtr Address of the global names structure.
	/// @param Out Receives the resolved names layout on success; set to @c nullptr on failure.
	/// @param OutError Optional. Receives why resolution failed, possibly several lines separated by '\n'. Left untouched on success.
	/// @return @c true if a names pool or names array layout was successfully resolved; otherwise @c false.
	inline virtual bool ResolveGNamesLayout(uintptr_t NamesPtr, std::unique_ptr<INamesLayout>& Out, std::string* OutError = nullptr) const
	{
		Out.reset();

		LayoutDetection::FNamesDetectionResult Result = LayoutDetection::DetectNamesLayout(NamesPtr);

		if (!Result.bSuccess)
		{
			if (OutError)
			{
				*OutError = Result.Summary;
				for (const std::string& Line : Result.Details)
					OutError->append("\n").append(Line);
			}

			return false;
		}

		for (const std::string& Line : Result.Details)
			GLogger.FmtWrite(ELogLevel::Debug, "ResolveGNamesLayout: {}\n", Line);

		GLogger.FmtWrite(ELogLevel::Info, "ResolveGNamesLayout: Detected {}\n", Result.Summary);
		Out = std::move(Result.Layout);
		return true;
	}

	/**
	 * @brief Applies profile-specific settings overrides.
	 * @param[in,out] Settings Settings to override.
	 */
	inline virtual void OverrideSettings(FSettings& Settings) const
	{
		((void)Settings);
	}

	/**
	 * @brief Applies profile-specific generator offset overrides.
	 * @param[in,out] Offsets Offsets to override.
	 */
	inline virtual void OverrideInGenOffsets(FInGenOffsets& Offsets) const { ((void)Offsets); }

	/**
	 * @brief Applies profile-specific SDK offset overrides.
	 * @param[in,out] Offsets Offsets to override.
	 */
	inline virtual void OverrideInSKOffsets(FInSDKOffsets& Offsets) const { ((void)Offsets); }

	/**
	 * @brief Applies profile-specific value decryption overrides.
	 * @param[in,out] Callbacks Decrypt callbacks to override.
	 */
	inline virtual void OverrideDecryptCallbacks(FDecryptCallbacks& Callbacks) const { ((void)Callbacks); }

	/**
	 * @brief Retrieves an FObjectItem by index.
	 * @param[in] ObjectsPtr GObjects address.
	 * @param[in] Layout GObjects layout.
	 * @param[in] Index Object index.
	 * @return FObjectItem pointer, or nullptr.
	 */
	virtual uintptr_t GetObjectByIndex(uintptr_t ObjectsPtr, const std::unique_ptr<IObjectsLayout>& Layout, int32 Index) const
	{
		if (ObjectsPtr == 0 || !Layout || Index < 0 || Index > ObjectArray::Num())
			return 0;

		if (Layout->GetType() == EObjectsType::Array)
		{
			const FFixedUObjectArrayLayout* L = static_cast<const FFixedUObjectArrayLayout*>(Layout.get());
			const uintptr_t ObjectsAddr       = ObjectsPtr + L->Objects;
			uintptr_t BaseAddr                = GDecryptCallbacks.FixedObjects.Objects({.Address = ObjectsAddr, .RawValue = GMemory->Read<uintptr_t>(ObjectsAddr)});
			uintptr_t ItemAddr                = BaseAddr + Index * L->FUObjectItem.Size;
			const uintptr_t ObjectAddr        = ItemAddr + L->FUObjectItem.Object;
			return GDecryptCallbacks.FixedObjects.FUObjectItem.Object({.Address = ObjectAddr, .RawValue = GMemory->Read<uintptr_t>(ObjectAddr)});
		}

		const FChunkedUObjectArrayLayout* L = static_cast<const FChunkedUObjectArrayLayout*>(Layout.get());
		const int32 ChunkIndex              = Index / L->ElementsPerChunk;
		const int32 InChunkIdx              = Index % L->ElementsPerChunk;
		const uintptr_t ObjectsAddr         = ObjectsPtr + L->Objects;
		uintptr_t ChunksBase                = GDecryptCallbacks.ChunkedObjects.Objects({.Address = ObjectsAddr, .RawValue = GMemory->Read<uintptr_t>(ObjectsAddr)});
		uintptr_t ChunkAddr                 = GMemory->Read<uintptr_t>(ChunksBase + ChunkIndex * sizeof(void*));
		uintptr_t ItemAddr                  = ChunkAddr + InChunkIdx * L->FUObjectItem.Size;

		const uintptr_t ObjectAddr = ItemAddr + L->FUObjectItem.Object;
		return GDecryptCallbacks.ChunkedObjects.FUObjectItem.Object({.Address = ObjectAddr, .RawValue = GMemory->Read<uintptr_t>(ObjectAddr)});
	}

	/**
	 * @brief Retrieves a FNameEntry by index.
	 * @param[in] NamesPtr GNames pointer.
	 * @param[in] NamesLayout GNames layout.
	 * @param[in] Idx Name index.
	 * @return FNameEntry pointer, or nullptr.
	 */
	virtual uintptr_t GetNameEntryByIndex(uintptr_t NamesPtr, const std::unique_ptr<INamesLayout>& NamesLayout, int32_t Idx) const
	{
		if (NamesPtr == 0 || !NamesLayout || Idx < 0)
			return 0;

		if (NamesLayout->GetType() == ENamesType::Array)
		{
			const FNameArrayLayout* Layout = static_cast<const FNameArrayLayout*>(NamesLayout.get());
			if (Layout->ElementsPerChunk <= 0)
				return 0;

			const int32 ChunkIdx = Idx / Layout->ElementsPerChunk;
			const int32 InChunk  = Idx % Layout->ElementsPerChunk;

			uintptr_t ChunkAddr = GMemory->Read<uintptr_t>(NamesPtr + Layout->Chunks + ChunkIdx * sizeof(void*));
			DecryptNameChunk(NamesPtr, ChunkIdx, ChunkAddr);

			uintptr_t NameEntry = GMemory->Read<uintptr_t>(ChunkAddr + InChunk * sizeof(void*));
			DecryptNameEntry(NamesPtr, NameEntry);

			if (GMemory->IsAddressReadable(NameEntry))
				return NameEntry;
		}
		else
		{
			auto Layout = reinterpret_cast<FNamePoolLayout*>(NamesLayout.get());

			const int32 ChunkIdx      = Idx >> Layout->BlocksBit;
			const int32 InChunkOffset = (Idx & ((1 << Layout->BlocksBit) - 1)) * Layout->FNameEntry.Stride;

			if (ChunkIdx < 0)
				return 0;

			uintptr_t ChunksBase = NamesPtr + Layout->Blocks;
			uintptr_t ChunkAddr  = GMemory->Read<uintptr_t>(ChunksBase + ChunkIdx * sizeof(void*));
			DecryptNameChunk(NamesPtr, ChunkIdx, ChunkAddr);

			if (GMemory->IsAddressReadable(ChunkAddr))
			{
				uintptr_t NameEntry = ChunkAddr + InChunkOffset;
				DecryptNameEntry(NamesPtr, NameEntry);
				return NameEntry;
			}
		}

		return 0;
	}

	/**
	 * @brief Gets string of a FNameEntry.
	 * @param[in] NamesPtr GNames pointer.
	 * @param[in] NamesLayout GNames layout.
	 * @param[in] NameEntry FNameEntry pointer.
	 * @return FNameEntry string.
	 */
	virtual std::wstring GetNameEntryString(uintptr_t NamesPtr, const std::unique_ptr<INamesLayout>& NamesLayout, uintptr_t NameEntry) const
	{
		if (!NamesLayout || !GMemory->IsAddressReadable(NameEntry))
			return std::wstring();

		bool IsWide       = false;
		int StrLen        = 0;
		int StrNumber     = 0;
		uintptr_t StrAddr = 0;

		if (NamesLayout->GetType() == ENamesType::Array)
		{
			FNameArrayLayout* Layout = reinterpret_cast<FNameArrayLayout*>(NamesLayout.get());

			const uintptr_t IndexAddr = NameEntry + Layout->FNameEntry.Index;
			const int32 NameIdx       = static_cast<int32>(GDecryptCallbacks.NameArray.FNameEntry.Index({.Address = IndexAddr, .RawValue = GMemory->Read<uint32>(IndexAddr)}));

			IsWide  = Layout->FNameEntry.GetIsWide ? Layout->FNameEntry.GetIsWide(NameIdx) : (NameIdx & 1);
			StrLen  = GSettings.General.MaxFNameLen;
			StrAddr = NameEntry + Layout->FNameEntry.String;
		}
		else
		{
			auto Layout = reinterpret_cast<FNamePoolLayout*>(NamesLayout.get());

			auto ReadEntryHeader = [&Layout](uintptr_t Entry) -> uint16
			{
				const uintptr_t HeaderAddr = Entry + Layout->FNameEntry.Header;
				return GDecryptCallbacks.NamePool.FNameEntry.Header({.Address = HeaderAddr, .RawValue = GMemory->Read<uint16>(HeaderAddr)});
			};

			uint16 HeaderWithoutNumber = ReadEntryHeader(NameEntry);
			int32 NameLen              = Layout->FNameEntry.GetLength(HeaderWithoutNumber);

			if (NameLen == 0)
			{
				const int32 EntryIdOffset  = Layout->FNameEntry.String + ((Layout->FNameEntry.String == 6) * 2);
				const int32 NextEntryIndex = GMemory->Read<int32>(NameEntry + EntryIdOffset);

				StrNumber = GMemory->Read<int32>(NameEntry + EntryIdOffset + sizeof(int32));
				NameEntry = GetNameEntryByIndex(NamesPtr, NamesLayout, NextEntryIndex);

				if (!GMemory->IsAddressReadable(NameEntry))
					return std::wstring();

				HeaderWithoutNumber = ReadEntryHeader(NameEntry);
				NameLen             = Layout->FNameEntry.GetLength(HeaderWithoutNumber);
			}

			IsWide  = Layout->FNameEntry.GetIsWide(HeaderWithoutNumber);
			StrLen  = NameLen;
			StrAddr = NameEntry + Layout->FNameEntry.String;
		}

		if (!GMemory->IsAddressReadable(StrAddr) || StrLen <= 0 || StrLen > GSettings.General.MaxFNameLen || (IsWide && DisableWideNameEntry()))
			return std::wstring();

		std::wstring Result;

		if (IsWide)
		{
			if (!InternalSettings::bUseChar16String)
			{
				std::u32string Str = GMemory->ReadUTF32(StrAddr, StrLen, [this](char32_t* Data, int32_t Len)
				{
					DecryptUTF32(Data, Len);
				});

				if (!Str.empty())
				{
					Result = Utils::String::UTF32ToWString(Str);
				}
			}
			else
			{
				std::u16string Str = GMemory->ReadUTF16(StrAddr, StrLen, [this](char16_t* Data, int32_t Len)
				{
					DecryptUTF16(Data, Len);
				});

				if (!Str.empty())
				{
					Result = Utils::String::UTF16ToWString(Str);
				}
			}
		}
		else
		{
			std::string Str = GMemory->ReadUTF8(StrAddr, StrLen, [this](char* Data, int32_t Len)
			{
				DecryptUTF8(Data, Len);
			});

			if (!Str.empty())
			{
				Result = Utils::String::StringToWString(Str);
			}
		}

		if (!Result.empty() && StrNumber > 0)
			return Result + L'_' + std::to_wstring(StrNumber - 1);

		return Result;
	}

	/**
	 * @brief Wether to disable wide FNameEntry or not.
	 * Returning true will make @ref GetNameEntryString skip wide FNameEntry
	 */
	inline virtual bool DisableWideNameEntry() const
	{
		return false;
	}

	/**
	 * @brief Function to decrypt a FName Chunk address.
	 * @param[in] NamesPtr GNames pointer.
	 * @param[in,out] ChunkAddr FName Chunk to decrypt.
	 */
	inline virtual void DecryptNameChunk(uintptr_t NamesPtr, int32 ChunkIdx, uintptr_t& ChunkAddr) const
	{
		((void)NamesPtr);
		((void)ChunkIdx);
		((void)ChunkAddr);
	}

	/**
	 * @brief Function to decrypt a FNameEntry address.
	 * @param[in] NamesPtr GNames pointer.
	 * @param[in,out] NameEntry NameEntry to decrypt.
	 */
	inline virtual void DecryptNameEntry(uintptr_t NamesPtr, uintptr_t& NameEntry) const
	{
		((void)NamesPtr);
		((void)NameEntry);
	}

	/**
	 * @brief Decrypts a UTF-8 FNameEntry string buffer.
	 * @param[in,out] Data String buffer.
	 * @param[in] Len Buffer length.
	 */
	inline virtual void DecryptUTF8(char* Data, int32_t Len) const
	{
		((void)Data);
		((void)Len);
	}

	/**
	 * @brief Decrypts a UTF-16 FNameEntry string buffer.
	 * @param[in,out] Data String buffer.
	 * @param[in] Len Buffer length.
	 */
	inline virtual void DecryptUTF16(char16_t* Data, int32_t Len) const
	{
		((void)Data);
		((void)Len);
	}

	/**
	 * @brief Decrypts a UTF-32 FNameEntry string buffer.
	 * @param[in,out] Data String buffer.
	 * @param[in] Len Buffer length.
	 */
	inline virtual void DecryptUTF32(char32_t* Data, int32_t Len) const
	{
		((void)Data);
		((void)Len);
	}
};

/**
 * @brief Global active game profile.
 */
inline std::shared_ptr<IProfile> GProfile = nullptr;
