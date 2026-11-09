#include "NameArray.h"

#include <algorithm>

#include "../../Memory/IMemory.h"
#include "../../Utils/Logger.h"
#include "../../Utils/Utils.h"

#include "../OffsetFinder/DecryptCallbacks.h"
#include "../OffsetFinder/Offsets.h"

#include "UnrealTypes.h"

FNameEntry::FNameEntry(uint8* Ptr)
{
	Address = !MD7::Globals::GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Ptr)) ? nullptr : Ptr;
}

std::wstring FNameEntry::GetWString()
{
	if (!Address || !GetStrFn)
		return L"";

	return GetStrFn(reinterpret_cast<uintptr_t>(Address));
}

std::string FNameEntry::GetString()
{
	if (!Address)
		return "";

	return Utils::String::WStringToString(GetWString());
}

void* FNameEntry::GetAddress()
{
	return Address;
}


int32 NameArray::GetNumElements()
{
	FNameArrayLayout* NamesLayout = reinterpret_cast<FNameArrayLayout*>(MD7::Globals::GLayouts.NamesLayout.get());
	if (!NamesLayout || NamesLayout->GetType() != ENamesType::Array)
		return 0;

	if (NamesLayout->NumElements == -1)
		return 0;

	const uintptr_t Addr = MD7::Globals::GNames + NamesLayout->NumElements;
	return MD7::Globals::GDecryptCallbacks.NameArray.NumElements({.Address = Addr, .RawValue = MD7::Globals::GMemory->Read<int32>(Addr)});
}

int32 NameArray::GetNumChunks()
{
	FNamePoolLayout* NamesLayout = reinterpret_cast<FNamePoolLayout*>(MD7::Globals::GLayouts.NamesLayout.get());
	if (!NamesLayout || NamesLayout->GetType() != ENamesType::Pool)
		return 0;

	const uintptr_t Addr = MD7::Globals::GNames + NamesLayout->MaxChunkIndex;
	return MD7::Globals::GDecryptCallbacks.NamePool.MaxChunkIndex({.Address = Addr, .RawValue = MD7::Globals::GMemory->Read<int32>(Addr)});
}

int32 NameArray::GetByteCursor()
{
	FNamePoolLayout* NamesLayout = reinterpret_cast<FNamePoolLayout*>(MD7::Globals::GLayouts.NamesLayout.get());
	if (!NamesLayout || NamesLayout->GetType() != ENamesType::Pool)
		return 0;

	const uintptr_t Addr = MD7::Globals::GNames + NamesLayout->ByteCursor;
	return MD7::Globals::GDecryptCallbacks.NamePool.ByteCursor({.Address = Addr, .RawValue = MD7::Globals::GMemory->Read<int32>(Addr)});
}

FNameEntry NameArray::GetNameEntry(const void* Name)
{
	int32 Idx = FName(Name).GetCompIdx();
	return ByIndexFn ? reinterpret_cast<uint8*>(ByIndexFn(Idx)) : FNameEntry{};
}

FNameEntry NameArray::GetNameEntry(int32 Idx)
{
	return ByIndexFn ? reinterpret_cast<uint8*>(ByIndexFn(Idx)) : FNameEntry{};
}
