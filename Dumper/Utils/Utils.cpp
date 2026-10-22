#include "Utils.h"
#include "Logger.h"

#include <filesystem>

#ifdef __ANDROID__
#include "KittyMemoryEx/KittyAsm.hpp"
#else
#include "KittyMemory/KittyAsm.hpp"
#endif

#include "../Memory/IMemory.h"

#include "Zip/zip.h"

#ifdef __ANDROID__
#include <dirent.h>
#include <unordered_map>

#include "BinderShell/BinderShellCommand.h"
#endif

namespace fs = std::filesystem;

namespace Utils
{

	namespace Arm64
	{
		KittyInsnArm64 DecodeInsn(uint32_t Insn, uintptr_t Address)
		{
			return KittyArm64::decodeInsn(Insn, Address);
		}

		uintptr_t Find_ADRP_Final_Address(const std::vector<uint32_t>& Insns, uintptr_t Address)
		{
			if (Insns.empty())
				return 0;

			for (size_t i = 0; i < Insns.size(); i++)
			{
				KittyInsnArm64 Adrp = KittyArm64::decodeInsn(Insns[i], Address + (i * sizeof(uint32_t)));
				if (Adrp.isValid() && Adrp.type == EKittyInsnTypeArm64::ADRP)
				{
					for (size_t j = i + 1; j < Insns.size(); j++)
					{
						KittyInsnArm64 Imm = KittyArm64::decodeInsn(Insns[j], Address + (j * sizeof(uint32_t)));
						if (Imm.isValid() && Imm.immediate && Adrp.rd == Imm.rn)
						{
							return Adrp.target + Imm.immediate;
						}
					}
				}
			}

			return 0;
		}
	}

	namespace Arm32
	{
		KittyInsnArm32 DecodeInsn(uint32_t Insn, uint32_t Address)
		{
			return KittyArm32::decodeInsn(Insn, Address);
		}

		uintptr_t Find_LDR_ADD_PC_Address(const std::vector<uint32_t>& Insns, uintptr_t Address, IMemory* Memory)
		{
			for (size_t i = 0; i + 1 < Insns.size(); i++)
			{
				const KittyInsnArm32 Ldr = KittyArm32::decodeInsn(Insns[i], static_cast<uint32_t>(Address + i * 4));
				const KittyInsnArm32 Add = KittyArm32::decodeInsn(Insns[i + 1], static_cast<uint32_t>(Address + i * 4 + 4));

				if (!Ldr.isValid() || Ldr.type != EKittyInsnTypeArm32::LDR_LITERAL)
					continue;
				if (!Add.isValid() || Add.type != EKittyInsnTypeArm32::ADR_REG)
					continue;
				if (Add.rd != Ldr.rd || Add.rt != Ldr.rd)
					continue;

				// Ldr.target = literal pool slot address (LDR_LITERAL resolves PC + kPcBias + imm).
				// The slot holds the delta that the following ADD adds back to PC (= pool address).
				const int32_t Delta = Memory->Read<int32_t>(static_cast<uintptr_t>(Ldr.target));
				return static_cast<uintptr_t>(static_cast<int64_t>(Ldr.target) + Delta);
			}

			return 0;
		}
	}

	namespace Zip
	{
		bool CreateZipWithDirectory(const std::string& InDir, int CompressionLevel, const std::string& OutZip)
		{
			struct zip_t* Archive = zip_open(OutZip.c_str(), CompressionLevel, 'w');
			if (!Archive)
				return false;

			bool bSuccess = true;

			for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(InDir))
			{
				if (!Entry.is_regular_file())
					continue;

				std::string RelPath = fs::relative(Entry.path(), InDir).string();
				std::replace(RelPath.begin(), RelPath.end(), '\\', '/');

				if (zip_entry_open(Archive, RelPath.c_str()) < 0)
				{
					bSuccess = false;
					break;
				}

				if (zip_entry_fwrite(Archive, Entry.path().string().c_str()) < 0)
				{
					zip_entry_close(Archive);
					bSuccess = false;
					break;
				}

				zip_entry_close(Archive);
			}

			zip_close(Archive);
			return bSuccess;
		}

		bool CreateZipWithFile(const std::string& InFile, int CompressionLevel, const std::string& OutZip)
		{
			struct zip_t* Archive = zip_open(OutZip.c_str(), CompressionLevel, 'w');
			if (!Archive)
				return false;

			const std::string EntryName = fs::path(InFile).filename().string();

			if (zip_entry_open(Archive, EntryName.c_str()) < 0)
			{
				zip_close(Archive);
				return false;
			}

			const bool bSuccess = zip_entry_fwrite(Archive, InFile.c_str()) == 0;
			zip_entry_close(Archive);
			zip_close(Archive);
			return bSuccess;
		}

		bool ExtractZipToFolder(const std::string& InZip, const std::string& OutFolder)
		{
			if (!fs::is_regular_file(InZip))
				return false;

			struct zip_t* Archive = zip_open(InZip.c_str(), 0, 'r');
			if (!Archive)
				return false;

			const ssize_t TotalEntries = zip_entries_total(Archive);
			if (TotalEntries < 0)
			{
				zip_close(Archive);
				return false;
			}

			bool bSuccess = true;

			for (ssize_t I = 0; I < TotalEntries && bSuccess; ++I)
			{
				if (zip_entry_openbyindex(Archive, static_cast<size_t>(I)) < 0)
				{
					bSuccess = false;
					break;
				}

				if (zip_entry_isdir(Archive) > 0)
				{
					zip_entry_close(Archive);
					continue;
				}

				const char* EntryName = zip_entry_name(Archive);
				if (!EntryName)
				{
					zip_entry_close(Archive);
					bSuccess = false;
					break;
				}

				const fs::path OutPath = fs::path(OutFolder) / EntryName;

				std::error_code Ec;
				fs::create_directories(OutPath.parent_path(), Ec);
				if (Ec)
				{
					zip_entry_close(Archive);
					bSuccess = false;
					break;
				}

				if (zip_entry_fread(Archive, OutPath.string().c_str()) < 0)
					bSuccess = false;

				zip_entry_close(Archive);
			}

			zip_close(Archive);
			return bSuccess;
		}

		bool ExtractZipEntryToFolder(const std::string& InZip, const std::string& EntryPath, const std::string& OutFolder)
		{
			if (!fs::is_regular_file(InZip))
				return false;

			struct zip_t* Archive = zip_open(InZip.c_str(), 0, 'r');
			if (!Archive)
				return false;

			if (zip_entry_open(Archive, EntryPath.c_str()) < 0)
			{
				zip_close(Archive);
				return false;
			}

			const fs::path OutPath = fs::path(OutFolder) / fs::path(EntryPath).filename();

			std::error_code Ec;
			fs::create_directories(OutPath.parent_path(), Ec);
			if (Ec)
			{
				zip_entry_close(Archive);
				zip_close(Archive);
				return false;
			}

			const bool bSuccess = zip_entry_fread(Archive, OutPath.string().c_str()) == 0;
			zip_entry_close(Archive);
			zip_close(Archive);
			return bSuccess;
		}

		bool ExtractZipEntryToMemory(const std::string& InZip, const std::string& EntryPath, void** OutData, size_t* OutDataSize)
		{
			if (!OutData || !OutDataSize)
				return false;

			if (!fs::is_regular_file(InZip))
				return false;

			struct zip_t* Archive = zip_open(InZip.c_str(), 0, 'r');
			if (!Archive)
				return false;

			if (zip_entry_open(Archive, EntryPath.c_str()) < 0)
			{
				zip_close(Archive);
				return false;
			}

			const ssize_t BytesRead = zip_entry_read(Archive, OutData, OutDataSize);
			zip_entry_close(Archive);
			zip_close(Archive);
			return BytesRead >= 0;
		}

	} // namespace Zip

	namespace Apk
	{
		bool ParseAndroidManifest(
		    const uint8_t* ManifestData,
		    size_t ManifestDataSize,
		    std::vector<ManifestAttribute>& Attributes)
		{
			Attributes.clear();

			// ------------------------------------------------------------
			// Android Binary XML Constants
			// ------------------------------------------------------------

			constexpr uint32_t kAxmlMagicNumber = 0x00080003;

			constexpr uint32_t kChunkTypeStringPool = 0x001C0001;

			constexpr uint32_t kChunkTypeStartElement = 0x00100102;

			constexpr uint32_t kStringPoolUtf8Flag = 0x00000100;

			constexpr size_t kHeaderSizeBytes = 8;

			constexpr size_t kStringPoolCountOffset      = 8;
			constexpr size_t kStringPoolFlagsOffset      = 16;
			constexpr size_t kStringPoolStartOffset      = 20;
			constexpr size_t kStringPoolIndexTableOffset = 28;

			// constexpr size_t kStartElementNamespaceOffset = 16;
			constexpr size_t kStartElementNameOffset = 20;

			constexpr size_t kStartElementAttrStartOffset = 24;
			constexpr size_t kStartElementAttrSizeOffset  = 26;
			constexpr size_t kStartElementAttrCountOffset = 28;

			constexpr size_t kAttributeNamespaceOffset = 0;
			constexpr size_t kAttributeNameOffset      = 4;
			constexpr size_t kAttributeRawValueOffset  = 8;
			constexpr size_t kAttributeValueOffset     = 12;

			constexpr size_t kResXmlTreeAttrExtOffset = 16;

			constexpr size_t kResValueSize = 8;

			constexpr size_t kMinimumAttributeSize = 20;

			// ------------------------------------------------------------
			// Safe Readers
			// ------------------------------------------------------------

			auto ReadLE16 = [](const uint8_t* Data) -> uint16_t
			{
				return static_cast<uint16_t>(Data[0] | (static_cast<uint16_t>(Data[1]) << 8));
			};

			auto ReadLE32 = [](const uint8_t* Data) -> uint32_t
			{
				return static_cast<uint32_t>(Data[0]) | (static_cast<uint32_t>(Data[1]) << 8) | (static_cast<uint32_t>(Data[2]) << 16) | (static_cast<uint32_t>(Data[3]) << 24);
			};

			auto ReadUtf8Length = [](const uint8_t*& Data, const uint8_t* End) -> uint32_t
			{
				if (Data >= End)
					return 0;

				uint32_t Length = *Data++;

				if (Length & 0x80)
				{
					if (Data >= End)
						return 0;

					Length = ((Length & 0x7F) << 8) | static_cast<uint32_t>(*Data++);
				}

				return Length;
			};

			auto ReadUtf16Length = [&](const uint8_t*& Data, const uint8_t* End) -> uint32_t
			{
				if (Data + sizeof(uint16_t) > End)
					return 0;

				uint32_t Length = ReadLE16(Data);

				Data += sizeof(uint16_t);

				if (Length & 0x8000)
				{
					if (Data + sizeof(uint16_t) > End)
						return 0;

					Length = ((Length & 0x7FFF) << 16) | ReadLE16(Data);

					Data += sizeof(uint16_t);
				}

				return Length;
			};

			// ------------------------------------------------------------
			// Validate Input
			// ------------------------------------------------------------

			if (!ManifestData || ManifestDataSize < kHeaderSizeBytes)
				return false;

			if (ReadLE32(ManifestData) != kAxmlMagicNumber)
				return false;

			// ------------------------------------------------------------
			// String Pool
			// ------------------------------------------------------------

			std::vector<std::string> StringPool;

			size_t Offset = kHeaderSizeBytes;

			while (Offset + kHeaderSizeBytes <= ManifestDataSize)
			{
				const uint32_t ChunkType = ReadLE32(ManifestData + Offset);
				const uint32_t ChunkSize = ReadLE32(ManifestData + Offset + 4);

				if (ChunkSize < kHeaderSizeBytes || ChunkSize > ManifestDataSize - Offset)
					return false;

				const size_t ChunkEnd = Offset + ChunkSize;

				if (ChunkType == kChunkTypeStringPool)
				{
					if (ChunkSize < 28)
						return false;

					const uint32_t StringCount = ReadLE32(ManifestData + Offset + kStringPoolCountOffset);
					const uint32_t Flags       = ReadLE32(ManifestData + Offset + kStringPoolFlagsOffset);
					const uint32_t StringStart = ReadLE32(ManifestData + Offset + kStringPoolStartOffset);

					const size_t StringIndexOffset = Offset + kStringPoolIndexTableOffset;

					if (StringIndexOffset > ChunkEnd)
						return false;

					if (StringCount > (ChunkEnd - StringIndexOffset) / sizeof(uint32_t))
						return false;

					// An empty pool (StringCount == 0) legitimately has StringStart == ChunkSize:
					// zero bytes of string data are needed, so the data region starts exactly at
					// the chunk's end.
					if (StringStart > ChunkSize)
						return false;

					const uint8_t* StringData = ManifestData + Offset + StringStart;

					StringPool.resize(StringCount);

					std::vector<uint32_t> StringIndexes(StringCount);

					for (uint32_t Index = 0; Index < StringCount; Index++)
					{
						StringIndexes[Index] = ReadLE32(ManifestData + StringIndexOffset + Index * sizeof(uint32_t));
						if (StringIndexes[Index] >= ChunkSize - StringStart)
							return false;
					}

					const bool IsUtf8 = (Flags & kStringPoolUtf8Flag) != 0;

					for (uint32_t Index = 0; Index < StringCount; Index++)
					{
						const uint8_t* StringPointer = StringData + StringIndexes[Index];
						const uint8_t* ChunkLimit    = ManifestData + ChunkEnd;

						if (IsUtf8)
						{
							// Skip UTF-16 character count.
							ReadUtf8Length(StringPointer, ChunkLimit);

							// Read UTF-8 byte count.
							const uint32_t ByteLength = ReadUtf8Length(StringPointer, ChunkLimit);
							if (StringPointer + ByteLength > ChunkLimit)
								return false;

							StringPool[Index] = std::string(reinterpret_cast<const char*>(StringPointer), ByteLength);
						}
						else
						{
							const uint32_t CodeUnitCount = ReadUtf16Length(StringPointer, ChunkLimit);

							std::string Value;

							for (uint32_t CodeUnitIndex = 0; CodeUnitIndex < CodeUnitCount; CodeUnitIndex++)
							{
								if (StringPointer + sizeof(uint16_t) > ChunkLimit)
									return false;

								uint32_t CodePoint;

								uint16_t Character = ReadLE16(StringPointer);

								StringPointer += sizeof(uint16_t);

								if (Character >= 0xD800 && Character <= 0xDBFF)
								{
									// High surrogate.

									if (StringPointer + sizeof(uint16_t) > ChunkLimit)
										return false;

									const uint16_t LowSurrogate = ReadLE16(StringPointer);

									StringPointer += sizeof(uint16_t);

									if (LowSurrogate < 0xDC00 || LowSurrogate > 0xDFFF)
										return false;

									CodePoint = 0x10000 + (((Character - 0xD800) << 10) | (LowSurrogate - 0xDC00));

									// One Unicode character consumed two UTF-16 code units.
									CodeUnitIndex++;
								}
								else if (Character >= 0xDC00 && Character <= 0xDFFF)
								{
									// Unexpected low surrogate.
									return false;
								}
								else
								{
									CodePoint = Character;
								}

								// UTF-32 -> UTF-8
								if (CodePoint <= 0x7F)
								{
									Value.push_back(static_cast<char>(CodePoint));
								}
								else if (CodePoint <= 0x7FF)
								{
									Value.push_back(static_cast<char>(0xC0 | (CodePoint >> 6)));
									Value.push_back(static_cast<char>(0x80 | (CodePoint & 0x3F)));
								}
								else if (CodePoint <= 0xFFFF)
								{
									Value.push_back(static_cast<char>(0xE0 | (CodePoint >> 12)));
									Value.push_back(static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F)));
									Value.push_back(static_cast<char>(0x80 | (CodePoint & 0x3F)));
								}
								else if (CodePoint <= 0x10FFFF)
								{
									Value.push_back(static_cast<char>(0xF0 | (CodePoint >> 18)));
									Value.push_back(static_cast<char>(0x80 | ((CodePoint >> 12) & 0x3F)));
									Value.push_back(static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F)));
									Value.push_back(static_cast<char>(0x80 | (CodePoint & 0x3F)));
								}
								else
								{
									// Invalid Unicode code point.
									return false;
								}
							}

							StringPool[Index] = std::move(Value);
						}
					}
				}
				else if (ChunkType == kChunkTypeStartElement)
				{
					if (ChunkSize < 36)
						return false;

					const uint16_t AttributeStart = ReadLE16(ManifestData + Offset + kStartElementAttrStartOffset);
					const uint16_t AttributeSize  = ReadLE16(ManifestData + Offset + kStartElementAttrSizeOffset);
					const uint16_t AttributeCount = ReadLE16(ManifestData + Offset + kStartElementAttrCountOffset);
					const uint32_t ElementIndex   = ReadLE32(ManifestData + Offset + kStartElementNameOffset);

					if (AttributeSize < kMinimumAttributeSize)
						return false;

					size_t AttributeOffset = Offset + kResXmlTreeAttrExtOffset + AttributeStart;
					if (AttributeOffset > ChunkEnd)
						return false;

					// An element with zero attributes legitimately has AttributeOffset == ChunkEnd
					// (no room needed for any attribute record); only demand room for one when
					// there's actually at least one to read.
					if (AttributeCount > 0 && AttributeSize > ChunkEnd - AttributeOffset)
						return false;

					for (uint16_t Index = 0; Index < AttributeCount; Index++)
					{
						if (AttributeOffset > ChunkEnd || AttributeSize > ChunkEnd - AttributeOffset)
							return false;

						if (AttributeOffset > ChunkEnd || kAttributeValueOffset + kResValueSize > ChunkEnd - AttributeOffset)
							return false;

						const uint32_t NamespaceIndex = ReadLE32(ManifestData + AttributeOffset + kAttributeNamespaceOffset);
						const uint32_t NameIndex      = ReadLE32(ManifestData + AttributeOffset + kAttributeNameOffset);
						const uint32_t RawValueIndex  = ReadLE32(ManifestData + AttributeOffset + kAttributeRawValueOffset);

						// Res_value: uint16 size, uint8 res0, uint8 dataType, uint32 data - dataType at
						// +3, data at +4.
						const uint8_t ValueType  = ManifestData[AttributeOffset + kAttributeValueOffset + 3];
						const uint32_t ValueData = ReadLE32(ManifestData + AttributeOffset + kAttributeValueOffset + 4);

						ManifestAttribute Attribute{};

						if (ElementIndex < StringPool.size())
							Attribute.ElementName = StringPool[ElementIndex];

						if (NamespaceIndex < StringPool.size())
							Attribute.Namespace = StringPool[NamespaceIndex];

						if (NameIndex < StringPool.size())
							Attribute.Name = StringPool[NameIndex];

						{
							Attribute.Type = ValueType;
							Attribute.Data = ValueData;

							switch (ValueType)
							{
							case ManifestAttribute::kTypeString:
							{
								if (RawValueIndex < StringPool.size())
									Attribute.StringValue = StringPool[RawValueIndex];

								break;
							}
							case ManifestAttribute::kTypeBoolean:
							{
								Attribute.StringValue = ValueData ? "true" : "false";
								break;
							}
							case ManifestAttribute::kTypeIntDec:
							{
								Attribute.StringValue = std::to_string(ValueData);
								break;
							}
							case ManifestAttribute::kTypeIntHex:
							{
								char Buffer[32] = {};
								snprintf(Buffer, sizeof(Buffer), "0x%08X", ValueData);
								Attribute.StringValue = Buffer;
								break;
							}
							case ManifestAttribute::kTypeReference:
							{
								char Buffer[32] = {};
								snprintf(Buffer, sizeof(Buffer), "@0x%08X", ValueData);
								Attribute.StringValue = Buffer;
								break;
							}
							default:
								break;
							}
						}

						Attributes.push_back(Attribute);

						AttributeOffset += AttributeSize;
					}
				}

				Offset = ChunkEnd;
			}

			return true;
		}

		std::string GetApkVersion(const uint8_t* ManifestData, size_t ManifestDataSize)
		{
			std::vector<ManifestAttribute> Attributes;
			if (!ParseAndroidManifest(ManifestData, ManifestDataSize, Attributes))
				return "";

			return GetManifestAttribute(Attributes, "manifest", "versionName").StringValue;
		}
	}

#ifdef __ANDROID__
	namespace Android
	{
		// Cached uid->package map, built once per run.
		static const std::unordered_map<uid_t, std::string>& GetUidPackageMap()
		{
			static const std::unordered_map<uid_t, std::string> Map = []
			{
				std::unordered_map<uid_t, std::string> Result;

				if (FILE* ListFile = fopen("/data/system/packages.list", "r"))
				{
					char LineBuf[1024];
					while (fgets(LineBuf, sizeof(LineBuf), ListFile))
					{
						char Name[256]  = {};
						unsigned IntUid = 0;

						if (sscanf(LineBuf, "%255s %u", Name, &IntUid) == 2)
							Result.emplace(static_cast<uid_t>(IntUid), Name);
					}
					fclose(ListFile);
				}

				if (!Result.empty())
					return Result;

				// Fallback: same listing via binder, no process spawned.
				std::string Output;
				if (BinderShell::Run("package", {"list", "packages", "-U"}, Output))
				{
					std::istringstream Lines(Output);
					std::string Line;
					while (std::getline(Lines, Line))
					{
						char Name[256]  = {};
						unsigned IntUid = 0;

						if (sscanf(Line.c_str(), "package:%255[^ ] uid:%u", Name, &IntUid) == 2)
							Result.emplace(static_cast<uid_t>(IntUid), Name);
					}
				}

				return Result;
			}();

			return Map;
		}

		bool GetPackageNameByUid(uid_t Uid, std::string& OutPackageName)
		{
			const auto& Map = GetUidPackageMap();
			const auto It   = Map.find(Uid);
			if (It == Map.end())
				return false;

			OutPackageName = It->second;
			return true;
		}

		bool GetPackageUid(const std::string& PackageName, uid_t& OutUid)
		{
			for (const auto& [Uid, Name] : GetUidPackageMap())
			{
				if (Name == PackageName)
				{
					OutUid = Uid;
					return true;
				}
			}

			return false;
		}

		std::vector<pid_t> GetProcessIDsByUid(uid_t Uid)
		{
			std::vector<pid_t> Pids;

			DIR* ProcDir = opendir("/proc");
			if (!ProcDir)
				return Pids;

			struct dirent* Entry;
			while ((Entry = readdir(ProcDir)) != nullptr)
			{
				const pid_t Pid = atoi(Entry->d_name);
				if (Pid <= 0)
					continue;

				char StatusPath[64];
				snprintf(StatusPath, sizeof(StatusPath), "/proc/%d/status", Pid);

				FILE* StatusFile = fopen(StatusPath, "r");
				if (!StatusFile)
					continue;

				char LineBuf[256];
				while (fgets(LineBuf, sizeof(LineBuf), StatusFile))
				{
					unsigned RealUid = 0;

					// First field of "Uid:" is the real UID.
					if (sscanf(LineBuf, "Uid: %u", &RealUid) == 1)
					{
						if (RealUid == Uid)
							Pids.push_back(Pid);
						break;
					}
				}

				fclose(StatusFile);
			}
			closedir(ProcDir);

			return Pids;
		}

		/*
		 * Fallback only. An install directory is named "<package>-<random>", and since Android 11
		 * it sits one level deeper inside a "~~<random>" parent; both layouts are walked.
		 *
		 * The package name is everything before the FIRST '-', not the last: OEM builds append
		 * their own marker, so com.android.deskclock installs as
		 * "com.android.deskclock-oui-<random>". A package name can never contain '-' itself.
		 *
		 * This sees every app on the data partition, which includes updated system apps, so it
		 * over-reports next to the PackageManager query below.
		 */
		void CollectInstallDirs(const std::string& Path, std::unordered_set<std::string>& Out, bool bDescend)
		{
			DIR* Dir = opendir(Path.c_str());
			if (!Dir)
				return;

			struct dirent* Entry;
			while ((Entry = readdir(Dir)) != nullptr)
			{
				const std::string Name(Entry->d_name);
				if (Name == "." || Name == "..")
					continue;

				if (bDescend && Name.starts_with("~~"))
				{
					CollectInstallDirs(Path + "/" + Name, Out, false);
					continue;
				}

				const size_t Dash = Name.find('-');
				if (Dash != std::string::npos && Dash > 0)
					Out.emplace(Name.substr(0, Dash));
			}

			closedir(Dir);
		}

		const std::unordered_set<std::string>& GetUserInstalledPackages()
		{
			static const std::unordered_set<std::string> Packages = []
			{
				std::unordered_set<std::string> Result;

				// PackageManager's own notion of a third-party package, so an updated system app
				// is still reported as a system app. No process spawned.
				std::string Output;
				if (BinderShell::Run("package", {"list", "packages", "-3"}, Output))
				{
					std::istringstream Lines(Output);
					std::string Line;
					while (std::getline(Lines, Line))
					{
						while (!Line.empty() && (Line.back() == '\r' || Line.back() == '\n'))
							Line.pop_back();

						if (Line.starts_with("package:") && Line.size() > 8)
							Result.emplace(Line.substr(8));
					}
				}

				if (Result.empty())
					CollectInstallDirs("/data/app", Result, true);

				return Result;
			}();

			return Packages;
		}

		// "ProcessRecord{<hex id> <pid>:<name>/<uid string>}", same text either way Output came from.
		void ParseProcessRecords(const std::string& Output, std::unordered_map<pid_t, std::string>& OutMap)
		{
			const char* Needle = "ProcessRecord{";
			size_t Pos         = 0;
			while ((Pos = Output.find(Needle, Pos)) != std::string::npos)
			{
				Pos += strlen(Needle);

				int Pid        = 0;
				char Name[256] = {};
				if (sscanf(Output.c_str() + Pos, "%*[0-9a-f] %d:%255[^/]", &Pid, Name) == 2)
					OutMap.emplace(static_cast<pid_t>(Pid), Name);
			}
		}

		// Cached pid->name table from ActivityManager's own records, built once per run.
		static const std::unordered_map<pid_t, std::string>& GetProcessRecordNames()
		{
			static const std::unordered_map<pid_t, std::string> Map = []
			{
				std::unordered_map<pid_t, std::string> Result;

				// Primary: dump() via binder, no process spawned.
				std::string Output;
				if (BinderShell::Dump("activity", {"processes"}, Output) && !Output.empty())
				{
					ParseProcessRecords(Output, Result);
					if (!Result.empty())
						return Result;
				}

				// Fallback: same data via the dumpsys binary, for whatever this device's binder
				// setup does not cover.
				if (FILE* Pipe = popen("dumpsys activity processes", "r"))
				{
					Output.clear();
					char Buf[4096];
					size_t NumRead;
					while ((NumRead = fread(Buf, 1, sizeof(Buf), Pipe)) > 0)
						Output.append(Buf, NumRead);
					pclose(Pipe);

					ParseProcessRecords(Output, Result);
				}

				return Result;
			}();

			return Map;
		}

		std::string GetProcessRole(pid_t Pid, const std::string& PackageName)
		{
			const auto& Map = GetProcessRecordNames();
			const auto It   = Map.find(Pid);
			if (It == Map.end())
				return "unregistered";

			if (It->second == PackageName)
				return "main";

			const size_t Colon = It->second.find(':');
			return Colon != std::string::npos ? It->second.substr(Colon + 1) : It->second;
		}
	}
#endif
}