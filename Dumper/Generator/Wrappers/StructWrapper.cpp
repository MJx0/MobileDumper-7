#include "StructWrapper.h"

#include <set>

#include "../../Utils/Logger.h"
#include "../../Utils/Utils.h"

#include "../Managers/MemberManager.h"

StructWrapper::StructWrapper(const PredefinedStruct* const Predef)
    : PredefStruct(Predef),
      InfoHandle()
{
}

StructWrapper::StructWrapper(const UEStruct Str)
    : Struct(Str),
      InfoHandle(StructManager::GetInfo(Str)),
      bIsUnrealStruct(true)
{
}

UEStruct StructWrapper::GetUnrealStruct() const
{
	assert(bIsUnrealStruct && "StructWrapper doesn't contain UnrealStruct. Illegal call to 'GetUnrealStruct()'.");

	return bIsUnrealStruct ? Struct : nullptr;
}

std::string StructWrapper::GetName() const
{
	return bIsUnrealStruct ? Struct.GetValidName() : PredefStruct->UniqueName;
}

std::string StructWrapper::GetRawName() const
{
	return bIsUnrealStruct ? Struct.GetName() : PredefStruct->UniqueName;
}

std::string StructWrapper::GetFullName() const
{
	return bIsUnrealStruct ? Struct.GetFullName() : "Predefined struct " + PredefStruct->UniqueName;
}

StructWrapper StructWrapper::GetSuper() const
{
	return bIsUnrealStruct ? StructWrapper(Struct.GetSuper()) : PredefStruct->Super;
}

MemberManager StructWrapper::GetMembers() const
{
	return bIsUnrealStruct ? MemberManager(Struct) : MemberManager(PredefStruct);
}


/* Name, bIsUnique */
std::pair<std::string, bool> StructWrapper::GetUniqueName() const
{
	if (bIsUnrealStruct)
	{
		/* StructManager hands back a sentinel StructInfo for a null or unregistered
		 * struct, and its name index is InvalidIndex. GetName() only does pointer
		 * arithmetic on the string table, so the bad index is not caught there —
		 * the caller faults on the first read through the returned reference. */
		if (!InfoHandle.HasValidName())
		{
			/* Someone asked for the name of a struct the manager never registered.
			 * Recoverable, but the emitted name will be wrong, so report which one —
			 * deduplicated, since generation revisits the same structs constantly. */
#ifndef NDEBUG
			static std::set<const void*> Reported;
			const void* Key = Struct ? Struct.GetAddress() : nullptr;

			if (Reported.insert(Key).second)
			{
				GLogger.FmtWrite(ELogLevel::Warning,
				                 "StructWrapper::GetUniqueName: unregistered struct {} (0x{:X})\n",
				                 Struct ? Struct.GetValidName() : std::string("<null>"),
				                 reinterpret_cast<uintptr_t>(Key));
			}
#endif

			return {Struct ? Struct.GetValidName() : std::string(), false};
		}

		const auto& StringEntry = InfoHandle.GetName();

		return {StringEntry.GetName(), StringEntry.IsUnique()};
	}

	return {PredefStruct->UniqueName, true};
}

int32 StructWrapper::GetLastMemberEnd() const
{
	return bIsUnrealStruct ? InfoHandle.GetLastMemberEnd() : 0x0;
}

int32 StructWrapper::GetAlignment() const
{
	return bIsUnrealStruct ? InfoHandle.GetAlignment() : PredefStruct->Alignment;
}

int32 StructWrapper::GetSize() const
{
	return bIsUnrealStruct ? InfoHandle.GetSize() : Utils::Align(PredefStruct->Size, PredefStruct->Alignment);
}

int32 StructWrapper::GetUnalignedSize() const
{
	return bIsUnrealStruct ? InfoHandle.GetUnalignedSize() : PredefStruct->Size;
}

bool StructWrapper::ShouldUseExplicitAlignment() const
{
	return bIsUnrealStruct ? InfoHandle.ShouldUseExplicitAlignment() : PredefStruct->bUseExplictAlignment;
}

bool StructWrapper::HasReusedTrailingPadding() const
{
	return bIsUnrealStruct && InfoHandle.HasReusedTrailingPadding();
}

bool StructWrapper::IsFinal() const
{
	return bIsUnrealStruct ? InfoHandle.IsFinal() : PredefStruct->bIsFinal;
}

bool StructWrapper::IsClass() const
{
	return bIsUnrealStruct ? Struct.IsA(EClassCastFlags::Class) : PredefStruct->bIsClass;
}

bool StructWrapper::IsUnion() const
{
	return !bIsUnrealStruct && PredefStruct->bIsUnion;
}

bool StructWrapper::IsFunction() const
{
	return bIsUnrealStruct && Struct.IsA(EClassCastFlags::Function);
}

bool StructWrapper::IsInterface() const
{
	static UEClass InterfaceClass = ObjectArray::FindClassFast("Interface");

	return bIsUnrealStruct && Struct.IsA(EClassCastFlags::Class) && Struct.HasType(InterfaceClass);
}

bool StructWrapper::IsExactClassUObject() const
{
	static UEClass UObjectClass = ObjectArray::FindClassFast("Object");

	return bIsUnrealStruct && Struct == UObjectClass;
}

bool StructWrapper::IsAClassWithType(UEClass TypeClass) const
{
	return IsUnrealStruct() && IsClass() && Struct.Cast<UEClass>().IsA(TypeClass);
}


bool StructWrapper::IsValid() const
{
	// Struct and PredefStruct share the same memory location, if Struct is nullptr so is PredefStruct
	return PredefStruct != nullptr;
}

bool StructWrapper::IsUnrealStruct() const
{
	return bIsUnrealStruct;
}

bool StructWrapper::IsCyclicWithPackage(int32 PackageIndex) const
{
	if (!bIsUnrealStruct || PackageIndex == -1)
		return false;

	if (!InfoHandle.IsPartOfCyclicPackage())
		return false;

	return StructManager::IsStructCyclicWithPackage(Struct.GetIndex(), PackageIndex);
}

bool StructWrapper::HasCustomTemplateText() const
{
	return !IsUnrealStruct() && !PredefStruct->CustomTemplateText.empty();
}

std::string StructWrapper::GetCustomTemplateText() const
{
	assert(!IsUnrealStruct() && "StructWrapper doesn't contain PredefStruct. Illegal call to 'GetCustomTemplateText()'.");

	return PredefStruct->CustomTemplateText;
}
