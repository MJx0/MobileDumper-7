#pragma once

#include <string>

#include "Engine/Unreal/Enums.h"

struct FSettings
{
	struct General
	{
		/// Canonical filenames shipped inside every Unreal Engine iOS IPA.
		/// @c IsUnrealGame() scans the main bundle for at least one of these to
		/// confirm the host process is a UE title.
		std::vector<std::string> iOSUnrealMarkFiles = {"cookeddata", "mute.caf", "uecommandline.txt", "ue4commandline.txt", "ue5commandline.txt", "Manifest_UFSFiles_IOS.txt", "Manifest_NonUFSFiles_IOS.txt", "Manifest_DebugFiles_IOS.txt"};

		/// Canonical shared-library basenames used to locate the UE module on
		/// Android when @c UnrealModuleName is empty.
		std::vector<std::string> AndroidUnrealDefaultNames = {"libUE4.so", "libUnreal.so"};

		/// Name or full path of the binary that contains UE's MD7::Globals::GObjects / MD7::Globals::GNames.
		/// Leave empty so the platform backend auto-selects the UE module
		std::string UnrealModuleName;

		/// Read limit when no explicit length field is available in the target engine version.
		int32 MaxFNameLen = 512;
	} General;

	struct Generator
	{
		/// Absolute path for all generated output files.  Set by the platform
		/// entry point (@c main_ios.mm / @c main_android.cpp) before @c Run().
		std::string SDKGenerationPath;

		/// Generate the MD7::Globals::GObjects dump file listing all live UObjects.
		bool bGenerateGObjects = true;

		/// Generate the MD7::Globals::GObjects dump with all property values for each object.
		bool bGenerateGObjectsWithProps = true;

		/// Include editor-only metadata (names, tooltips, categories) in the SDK.
		/// Disable for shipping builds that strip editor data to reduce output size.
		bool bGenerateEditorOnlyMetadata = false;

		/// Generate C++ SDK headers (packages, classes, structs, enums, functions).
		bool bGenerateCppSDK = true;

		/// Generate a @c .usmap mapping file for use with Unreal tools.
		bool bGenerateMapping = true;

		/// Generate an IDA-compatible mapping script.
		bool bGenerateIDAMapping = true;

		/// Generate a Dumpspace-compatible mapping file.
		bool bGenerateDumpspace = true;

		/// Deflate level used when zipping a completed dump folder, 0-9, where 0
		/// stores without compressing and 9 is smallest but slowest. A dump is
		/// almost entirely text, so the default trades a little CPU on device for
		/// a much smaller archive to transfer off it. Values outside 0-9 are clamped.
		int32 ZipCompressionLevel = 6;
	} Generator;

	struct EngineCore
	{
		/// Enable support for @c TEncryptedObjectProperty, which stores its inner
		/// @c UObject pointer in an encrypted/obfuscated form.
		bool bEnableEncryptedObjectPropertySupport = false;
	} EngineCore;

	struct CppGenerator
	{
	public:
		/// Enables custom alignment for generated member declarations.
		/// When disabled, member spacing is calculated automatically.
		bool UseMemberCustomSpacing = false;

		/// Width of the column reserved for the member type.
		/// Only used when @c UseCustomSpacing is enabled.
		int MemberTypeColumnWidth = 45;

		/// Width of the column reserved for the member name and spacing
		/// before the trailing comment.
		/// Only used when @c UseCustomSpacing is enabled.
		int MemberNameColumnWidth = 50;

		/// Filename of the precompiled header included at the top of every
		/// generated source file.  Empty string disables PCH emission.
		std::string PrecompiledHeaderFileName = "";

		/// Prefix prepended to every generated filename (e.g. @c "MyGame_").
		/// Empty string uses the package name without a prefix.
		std::string FilePrefix = "";

		/// C++ namespace that wraps all generated SDK types.
		/// Set to an empty string to emit types directly in the global namespace.
		std::string SDKNamespaceName = "SDK";

		/// Namespace name for generated function-parameter structs.
		/// Empty string places parameter structs in the same namespace as types.
		std::string ParamNamespaceName = "Params";

		/// Name of the XOR obfuscation wrapper used around generated string
		/// literals (e.g. @c "xorstr_" → @c xorstr_("Pawn")).
		/// Empty string disables XOR wrapping.
		std::string XORString = "";

		/// Header filename that provides the XOR wrapper declared in @c XORString
		/// (e.g. @c "xorstr.hpp").  Empty string disables the include.
		std::string XORStringInclude = "";

		struct FInSDKUtils
		{
			/// Override body for @c InSDKUtils::GetImageBase().
			/// Empty emits a default that resolves the Unreal module by name at runtime.
			std::string GetImageBaseFuncBody = "";

			/// Override body for @c InSDKUtils::GetGNames().
			/// Empty emits a default built for the layout that was detected.
			std::string GetGNamesFuncBody = "";

			/// Override body for @c InSDKUtils::GetGObjects().
			/// Empty emits a default built for the layout that was detected.
			std::string GetGObjectsBody = "";

			/// Override body for @c InSDKUtils::GetNameByIndex().
			/// Empty emits a default built for the layout that was detected.
			std::string GetNameByIndexFuncBody = "";

			/// Override body for @c InSDKUtils::GetObjectByIndex().
			/// Empty emits a default built for the layout that was detected.
			std::string GetObjectByIndexFuncBody = "";

			/// Customisable body for @c InSDKUtils::CallGameFunction.
			std::string CallGameFunction =
			    R"(
	template<typename FuncType, typename... ParamTypes>
	requires std::invocable<FuncType, ParamTypes...>
	inline auto CallGameFunction(FuncType Function, ParamTypes&&... Args)
	{
		return Function(std::forward<ParamTypes>(Args)...);
	}
)";
		} InSDKUtils;

		struct FDecryptionUtils
		{
			/// Override body for @c DecryptionUtils::DecryptGNames(uintptr_t).
			/// Empty emits a default that returns the pointer unchanged.
			std::string DecryptGNamesFuncBody = "";

			/// Override body for @c DecryptionUtils::DecryptGObjects(uintptr_t).
			/// Empty emits a default that returns the pointer unchanged.
			std::string DecryptGObjectsFuncBody = "";

			/// Override body for @c DecryptionUtils::DecryptFNameChunk(uintptr_t, int32).
			/// Empty emits a default that returns the address unchanged.
			std::string DecryptFNameChunkFuncBody = "";

			/// Override body for @c DecryptionUtils::DecryptFNameEntry(uintptr_t, int32).
			/// Empty emits a default that returns the address unchanged.
			std::string DecryptFNameEntryFuncBody = "";

			/// Override body for @c DecryptionUtils::DecryptFObjectItemEntry(UObject*).
			/// Empty emits a default that returns the pointer unchanged.
			std::string DecryptFObjectItemEntryFuncBody = "";

			/// Override body for @c DecryptionUtils::DecryptFNameUTF8(char*, int32).
			/// Empty emits a default no-op.
			std::string DecryptFNameUTF8FuncBody = "";

			/// Override body for @c DecryptionUtils::DecryptFNameUTF16(char16_t*, int32).
			/// Empty emits a default no-op.
			std::string DecryptFNameUTF16FuncBody = "";

			/// Override body for @c DecryptionUtils::DecryptFNameUTF32(char32_t*, int32).
			/// Empty emits a default no-op.
			std::string DecryptFNameUTF32FuncBody = "";
		} DecryptionUtils;

		/// When true, omits @c UWorld::GetWorld() from the generated SDK and
		/// forces callers to obtain the world via a @c UEngine instance instead.
		bool bForceNoGWorldInSDK = false;

		/// Emit helper functions that let callers override MD7::Globals::GObjects, MD7::Globals::GNames, and
		/// AppendString addresses at runtime without recompiling the SDK.
		bool bAddManualOverrideOptions = true;

		/// Annotate classes that have no loaded child class at generation time
		/// with the @c final specifier to improve compiler optimisations.
		bool bAddFinalSpecifier = true;

		/// Whether to include parameter structs when importing the SDK into IDA.
		bool bIncludeParameterStructsInIDA = true;

		/// Generate a self-contained validation project at @c CppSDK/TestSuit/ that
		/// builds into a library you load into the live game to check the generated
		/// SDK against the running process.
		bool bGenerateSDKCompilationTest = true;
	} CppGenerator;

	struct MappingGenerator
	{
		/// When true, the mapping generator deduplicates name-table entries before
		/// writing them, reducing the output file size at the cost of a small
		/// memory overhead during generation.
		bool bShouldCheckForDuplicatedNames = true;

		/// Exclude editor-only properties from the mapping file.  Matches the
		/// behaviour of @c Generator::bGenerateEditorOnlyMetadata for consistency.
		bool bExcludeEditorOnlyProperties = true;

		/// Compression algorithm applied to the generated @c .usmap file.
		EUsmapCompressionMethod CompressionMethod = EUsmapCompressionMethod::ZStandard;
	} MappingGenerator;

	/** Partially implemented — controls debug-assertion output. */
	struct Debug
	{
		/// Generate a dedicated header defining preprocessor macros for static
		/// assertions.  Only meaningful when inline assertions are disabled.
		bool bGenerateAssertionFile = true;

		/// Prefix for all assertion macros in the assertion header.
		/// Example: @c "DUMPER7_ASSERTS_" produces
		/// @c DUMPER7_ASSERTS_PARAMS_MyPackage.
		std::string AssertionMacroPrefix = "DUMPER7_ASSERTS_";

		/// Emit @c static_assert checks for struct size and alignment in the
		/// generated SDK headers.
		bool bGenerateInlineAssertionsForStructSize = false;

		/// Emit @c static_assert checks for individual member offsets in the
		/// generated SDK headers.
		bool bGenerateInlineAssertionsForStructMembers = false;

		/// Print verbose diagnostic information during mapping generation to the
		/// active logger.
		bool bShouldPrintMappingDebugData = false;
	} Debug;
};

namespace MD7::Globals
{
	inline FSettings GSettings;
}

//* * * * * * * * * * * * * * * * * * * * *//
// Do **NOT** change any of these settings //
//* * * * * * * * * * * * * * * * * * * * *//
namespace InternalSettings
{
	/// Game name automatically set by @c IProfile::GetGameIdentifier().
	inline std::string GameName = "";

	/// Game version string automatically set by @c IProfile::GetGameVersion().
	inline std::string GameVersion = "";

	/// Whether @c UEnum::Names stores bare name values (true) or
	/// @c TPair<FName,int64> pairs (false).
	inline bool bIsEnumNameOnly = false;

	/// When true, the value component of the @c UEnum::Names pair is a @c uint8
	/// rather than the default @c int64.
	inline bool bIsSmallEnumValue = false;

	/// Whether @c UEnum contains an explicit @c EUnderlyingType UnderlyingType
	/// field (introduced in a later engine version).
	inline bool bHasUnderlayingTypeInUEnum = false;

	/// Whether @c UEnum::Names uses the newer @c FNameData container type instead
	/// of a plain @c TArray.
	inline bool bIsNewUE5EnumNamesContainer = false;

	/// When true, @c TWeakObjectPtr omits the @c TagAtLastTest field present in
	/// older engine versions.
	inline bool bIsWeakObjectPtrWithoutTag = false;

	/// Whether this engine version uses @c FProperty (UE 4.25+) rather than the
	/// legacy @c UProperty hierarchy.
	inline bool bUseFProperty = false;

	/// Whether this engine version uses @c FNamePool (UE 4.23+) rather than the
	/// older @c TNameEntryArray.
	inline bool bUseNamePool = false;

	/// Whether @c UObject::Name precedes @c UObject::Class in memory.  Affects
	/// the @c FName size calculation in fixup code; unused after
	/// @c MD7::Globals::GOffsets.Init().
	inline bool bIsObjectNameBeforeClass = false;

	/// Whether FNames are case-preserving, adding a @c int32 DisplayIndex field
	/// to the @c FName struct.
	inline bool bUseCasePreservingName = false;

	/// Whether @c FNameOutlineNumber is in use, moving the @c Number component
	/// from @c FName into @c FNameEntry inside @c FNamePool.
	inline bool bUseOutlineNumberName = false;

	/// Whether @c FFieldPathProperty cast flags are repurposed for a custom
	/// @c FObjectPtrProperty instead.
	inline bool bIsObjPtrInsteadOfFieldPathProperty = false;

	/// Whether @c FFieldVariant uses a constexpr bitmask to distinguish between
	/// a @c UObject* and an @c FField* owner rather than a separate bool.
	inline bool bUseMaskForFieldOwner = false;

	/// Whether this engine version uses @c double for @c FVector components
	/// (Large World Coordinates, introduced in UE 5.0).
	inline bool bUseLargeWorldCoordinates = false;

	/// Whether @c UEProperty::ArrayDim is stored as @c uint8 instead of the
	/// standard @c int32.
	inline bool bUseUint8ArrayDim = false;

	/// Whether the engine uses @c char16_t for FName strings (UE 4.21+).
	/// Auto-detected by @c UEAnalyzer::GetTCharKind() and @c FInSDKOffsets::InitFText().
	inline bool bUseChar16String = true;

	/// Whether @c Offsets::GObjects holds a pointer to the object array rather than
	/// being the array itself. Set by @c Generator::InitObjects, which accepts either.
	inline bool bIsGObjectsPointer = false;

	/// Whether @c Offsets::GNames holds a pointer to the names container rather than
	/// being the container itself. Set by @c Generator::InitNames, which accepts either.
	inline bool bIsGNamesPointer = false;

	/// Filename of the Unreal module the offsets were measured against, e.g. @c "libUE4.so".
	/// Set by @c Generator::InitUnrealModule; the generated SDK resolves this name at runtime to
	/// recover the value @c ModuleInfo::GetBase() had when the dump was taken.
	inline std::string UnrealModuleFileName = "";

	/// Detected engine version. Zeroed when detection failed, which the SDK reports as
	/// an unknown version rather than a wrong one.
	inline int32 EngineMajor = 0;
	inline int32 EngineMinor = 0;
	inline int32 EnginePatch = 0;

	/// Full engine version string as the engine formats it, e.g.
	/// @c "4.27.2-0+++UE4+Release-4.27". Empty when detection failed.
	inline std::string EngineVersionString = "";

	/// Detect and set @c bIsWeakObjectPtrWithoutTag.
	extern void InitWeakObjectPtrSettings();

	/// Detect and set @c bUseLargeWorldCoordinates.
	extern void InitLargeWorldCoordinateSettings();

	/// Detect and set @c bIsObjPtrInsteadOfFieldPathProperty.
	extern void InitObjectPtrPropertySettings();

	/// Detect and set @c bUseUint8ArrayDim.
	extern void InitArrayDimSizeSettings();
}
