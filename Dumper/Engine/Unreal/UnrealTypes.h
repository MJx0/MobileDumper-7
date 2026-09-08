#pragma once

#include <string>

#include "Enums.h"
#include "UnrealContainers.h"

using namespace UC;

extern std::string MakeNameValid(std::wstring&& Name);

template <typename Type>
struct TImplementedInterface
{
	Type InterfaceClass;
	int32 PointerOffset;
	bool bImplementedByK2;
};

using FImplementedInterface = TImplementedInterface<class UEClass>;

class FName
{
private:
	const uint8* Address;

public:
	FName() = default;
	FName(const void* Ptr);

public:
	inline const void* GetAddress() const { return Address; }

	std::wstring ToWString() const;
	std::wstring ToRawWString() const;

	std::string ToString() const;
	std::string ToRawString() const;
	std::string ToValidString() const;

	int32 GetCompIdx() const;
	uint32 GetNumber() const;

	bool operator==(FName Other) const;

	bool operator!=(FName Other) const;

	static std::string CompIdxToString(int CmpIdx);
};
