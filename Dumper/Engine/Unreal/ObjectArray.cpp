#include "ObjectArray.h"

#include <filesystem>
#include <unordered_set>

#include "../../Memory/IMemory.h"
#include "../../Utils/Logger.h"

#include "../OffsetFinder/Layouts.h"
#include "../OffsetFinder/Offsets.h"

namespace fs = std::filesystem;


int32 ObjectArray::Num()
{
	if (!GLayouts.ObjectsLayout)
		return 0;

	if (GLayouts.ObjectsLayout->GetType() == EObjectsType::Array)
	{
		FFixedUObjectArrayLayout* objLayout = reinterpret_cast<FFixedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get());
		return GMemory->Read<int32>(GObjects + objLayout->NumObjects);
	}

	FChunkedUObjectArrayLayout* objLayout = reinterpret_cast<FChunkedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get());
	return GMemory->Read<int32>(GObjects + objLayout->NumElements);
}

template <typename UEType>
UEType ObjectArray::GetByIndex(int32 Index)
{
	return UEType(ByIndexFn ? reinterpret_cast<uint8*>(ByIndexFn(Index)) : nullptr);
}

template <typename UEType>
UEType ObjectArray::FindObject(const std::string& FullName, EClassCastFlags RequiredType)
{
	const int MaxNumObjectsConsidered = ObjectArray::Num();
	int NumObjectsConsidered          = 0;

	for (UEObject Obj : ObjectArray())
	{
		if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
			break;

		if (Obj.IsA(RequiredType) && Obj.GetFullName() == FullName)
		{
			return Obj.Cast<UEType>();
		}
	}

	return UEType();
}

template <typename UEType>
UEType ObjectArray::FindObjectFast(const std::string& Name, EClassCastFlags RequiredType)
{
	const int MaxNumObjectsConsidered = ObjectArray::Num();
	int NumObjectsConsidered          = 0;

	for (UEObject Obj : ObjectArray())
	{
		if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
			break;

		if (Obj.IsA(RequiredType) && Obj.GetName() == Name)
		{
			return Obj.Cast<UEType>();
		}
	}

	return UEType();
}

template <typename UEType>
UEType ObjectArray::FindObjectFastInOuter(const std::string& Name, std::string Outer)
{
	const int MaxNumObjectsConsidered = ObjectArray::Num();
	int NumObjectsConsidered          = 0;

	for (UEObject Obj : ObjectArray())
	{
		if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
			break;

		if (Obj.GetName() == Name && Obj.GetOuter().GetName() == Outer)
		{
			return Obj.Cast<UEType>();
		}
	}

	return UEType();
}

UEStruct ObjectArray::FindStruct(const std::string& Name)
{
	return FindObjectFast<UEClass>(Name, EClassCastFlags::Struct);
}

UEStruct ObjectArray::FindStructFast(const std::string& Name)
{
	return FindObjectFast<UEClass>(Name, EClassCastFlags::Struct);
}

UEClass ObjectArray::FindClass(const std::string& FullName)
{
	return FindObject<UEClass>(FullName, EClassCastFlags::Class);
}

UEClass ObjectArray::FindClassFast(const std::string& Name)
{
	return FindObjectFast<UEClass>(Name, EClassCastFlags::Class);
}

ObjectArray::ObjectsIterator ObjectArray::begin()
{
	return ObjectsIterator();
}
ObjectArray::ObjectsIterator ObjectArray::end()
{
	return ObjectsIterator(Num());
}


ObjectArray::ObjectsIterator::ObjectsIterator(int32 StartIndex)
    : CurrentObject(ObjectArray::GetByIndex(StartIndex)),
      CurrentIndex(StartIndex)
{
}

UEObject ObjectArray::ObjectsIterator::operator*() const
{
	return CurrentObject;
}

ObjectArray::ObjectsIterator& ObjectArray::ObjectsIterator::operator++()
{
	CurrentObject = ObjectArray::GetByIndex(++CurrentIndex);

	while (!CurrentObject && CurrentIndex < (ObjectArray::Num() - 1))
	{
		CurrentObject = ObjectArray::GetByIndex(++CurrentIndex);
	}

	if (!CurrentObject && CurrentIndex == (ObjectArray::Num() - 1)) [[unlikely]]
		CurrentIndex++;

	return *this;
}

bool ObjectArray::ObjectsIterator::operator==(const ObjectsIterator& Other) const
{
	// >= rather than ==: end() freezes Num() once at loop-start, but a live target keeps
	// growing its object count while being dumped, letting operator++ (which re-reads
	// Num() fresh every time) push CurrentIndex past that frozen bound while chasing new
	// slots. An exact-equality check could then never fire again, looping forever.
	return CurrentIndex >= Other.CurrentIndex;
}

bool ObjectArray::ObjectsIterator::operator!=(const ObjectsIterator& Other) const
{
	return CurrentIndex < Other.CurrentIndex;
}

int32 ObjectArray::ObjectsIterator::GetIndex() const
{
	return CurrentIndex;
}

bool AllFFieldIterator::operator!=(const AllFFieldIterator& Other) const
{
	return CurrentObject != Other.CurrentObject || PropertyIndex != Other.PropertyIndex;
}

AllFFieldIterator& AllFFieldIterator::operator++()
{
	if (CurrenStructHasMoreMembers())
	{
		PropertyIndex++;

		return *this;
	}

	IterateToNextStructWithMembers();

	return *this;
}

UEFField AllFFieldIterator::operator*() const
{
	return Fields[PropertyIndex];
}


void AllFFieldIterator::IterateToNextStruct()
{
	if (IsEndIterator())
		return;

	++CurrentObject;

	while (CurrentObject != ObjectEndIterator && !IsCurrentObjectStruct())
		++CurrentObject;
}
void AllFFieldIterator::IterateToNextStructWithMembers()
{
	// Loop, in case we meet a struct wihtout any properties
	while (!CurrenStructHasMoreMembers())
	{
		IterateToNextStruct();
		PropertyIndex = 0;

		if (IsEndIterator())
			return;

		/*
		 * Collects every FField in the chain, unfiltered - this iterator only exists to help guess
		 * FField.Name's own offset (Init_FField_Name), which runs before FFieldClass.CastFlags and
		 * Property.* are known. Reusing UEStruct::GetProperties() here would classify fields using
		 * those same not-yet-resolved offsets, making the sample (and the whole guess) depend on
		 * incidental memory contents. FField.Name sits at the same offset on every FField-derived
		 * type regardless of which one it is, so an unfiltered sample works just as well.
		 */
		Fields.clear();

		std::unordered_set<uintptr_t> VisitedFields;

		for (UEFField Field = GetCurrentStruct().GetChildProperties(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Field.GetAddress())); Field = Field.GetNext())
		{
			if (!VisitedFields.insert(reinterpret_cast<uintptr_t>(Field.GetAddress())).second)
				break;

			Fields.push_back(Field);
		}
	}
}


/*
 * The compiler won't generate functions for a specific template type unless it's used in the .cpp file corresponding to the
 * header it was declatred in.
 *
 * See https://stackoverflow.com/questions/456713/why-do-i-get-unresolved-external-symbol-errors-when-using-templates
 */
template UEObject ObjectArray::FindObject<UEObject>(const std::string& FullName, EClassCastFlags RequiredType);
template UEField ObjectArray::FindObject<UEField>(const std::string& FullName, EClassCastFlags RequiredType);
template UEEnum ObjectArray::FindObject<UEEnum>(const std::string& FullName, EClassCastFlags RequiredType);
template UEStruct ObjectArray::FindObject<UEStruct>(const std::string& FullName, EClassCastFlags RequiredType);
template UEClass ObjectArray::FindObject<UEClass>(const std::string& FullName, EClassCastFlags RequiredType);
template UEFunction ObjectArray::FindObject<UEFunction>(const std::string& FullName, EClassCastFlags RequiredType);
template UEProperty ObjectArray::FindObject<UEProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEByteProperty ObjectArray::FindObject<UEByteProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEBoolProperty ObjectArray::FindObject<UEBoolProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEObjectProperty ObjectArray::FindObject<UEObjectProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEClassProperty ObjectArray::FindObject<UEClassProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEStructProperty ObjectArray::FindObject<UEStructProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEArrayProperty ObjectArray::FindObject<UEArrayProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEMapProperty ObjectArray::FindObject<UEMapProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UESetProperty ObjectArray::FindObject<UESetProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEEnumProperty ObjectArray::FindObject<UEEnumProperty>(const std::string& FullName, EClassCastFlags RequiredType);

template UEObject ObjectArray::FindObjectFast<UEObject>(const std::string& FullName, EClassCastFlags RequiredType);
template UEField ObjectArray::FindObjectFast<UEField>(const std::string& FullName, EClassCastFlags RequiredType);
template UEEnum ObjectArray::FindObjectFast<UEEnum>(const std::string& FullName, EClassCastFlags RequiredType);
template UEStruct ObjectArray::FindObjectFast<UEStruct>(const std::string& FullName, EClassCastFlags RequiredType);
template UEClass ObjectArray::FindObjectFast<UEClass>(const std::string& FullName, EClassCastFlags RequiredType);
template UEFunction ObjectArray::FindObjectFast<UEFunction>(const std::string& FullName, EClassCastFlags RequiredType);
template UEProperty ObjectArray::FindObjectFast<UEProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEByteProperty ObjectArray::FindObjectFast<UEByteProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEBoolProperty ObjectArray::FindObjectFast<UEBoolProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEObjectProperty ObjectArray::FindObjectFast<UEObjectProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEClassProperty ObjectArray::FindObjectFast<UEClassProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEStructProperty ObjectArray::FindObjectFast<UEStructProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEArrayProperty ObjectArray::FindObjectFast<UEArrayProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEMapProperty ObjectArray::FindObjectFast<UEMapProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UESetProperty ObjectArray::FindObjectFast<UESetProperty>(const std::string& FullName, EClassCastFlags RequiredType);
template UEEnumProperty ObjectArray::FindObjectFast<UEEnumProperty>(const std::string& FullName, EClassCastFlags RequiredType);

template UEObject ObjectArray::FindObjectFastInOuter<UEObject>(const std::string& FullName, std::string Outer);
template UEField ObjectArray::FindObjectFastInOuter<UEField>(const std::string& FullName, std::string Outer);
template UEEnum ObjectArray::FindObjectFastInOuter<UEEnum>(const std::string& FullName, std::string Outer);
template UEStruct ObjectArray::FindObjectFastInOuter<UEStruct>(const std::string& FullName, std::string Outer);
template UEClass ObjectArray::FindObjectFastInOuter<UEClass>(const std::string& FullName, std::string Outer);
template UEFunction ObjectArray::FindObjectFastInOuter<UEFunction>(const std::string& FullName, std::string Outer);
template UEProperty ObjectArray::FindObjectFastInOuter<UEProperty>(const std::string& FullName, std::string Outer);
template UEByteProperty ObjectArray::FindObjectFastInOuter<UEByteProperty>(const std::string& FullName, std::string Outer);
template UEBoolProperty ObjectArray::FindObjectFastInOuter<UEBoolProperty>(const std::string& FullName, std::string Outer);
template UEObjectProperty ObjectArray::FindObjectFastInOuter<UEObjectProperty>(const std::string& FullName, std::string Outer);
template UEClassProperty ObjectArray::FindObjectFastInOuter<UEClassProperty>(const std::string& FullName, std::string Outer);
template UEStructProperty ObjectArray::FindObjectFastInOuter<UEStructProperty>(const std::string& FullName, std::string Outer);
template UEArrayProperty ObjectArray::FindObjectFastInOuter<UEArrayProperty>(const std::string& FullName, std::string Outer);
template UEMapProperty ObjectArray::FindObjectFastInOuter<UEMapProperty>(const std::string& FullName, std::string Outer);
template UESetProperty ObjectArray::FindObjectFastInOuter<UESetProperty>(const std::string& FullName, std::string Outer);
template UEEnumProperty ObjectArray::FindObjectFastInOuter<UEEnumProperty>(const std::string& FullName, std::string Outer);

template UEObject ObjectArray::GetByIndex<UEObject>(int32 Index);
template UEField ObjectArray::GetByIndex<UEField>(int32 Index);
template UEEnum ObjectArray::GetByIndex<UEEnum>(int32 Index);
template UEStruct ObjectArray::GetByIndex<UEStruct>(int32 Index);
template UEClass ObjectArray::GetByIndex<UEClass>(int32 Index);
template UEFunction ObjectArray::GetByIndex<UEFunction>(int32 Index);
template UEProperty ObjectArray::GetByIndex<UEProperty>(int32 Index);
template UEByteProperty ObjectArray::GetByIndex<UEByteProperty>(int32 Index);
template UEBoolProperty ObjectArray::GetByIndex<UEBoolProperty>(int32 Index);
template UEObjectProperty ObjectArray::GetByIndex<UEObjectProperty>(int32 Index);
template UEClassProperty ObjectArray::GetByIndex<UEClassProperty>(int32 Index);
template UEStructProperty ObjectArray::GetByIndex<UEStructProperty>(int32 Index);
template UEArrayProperty ObjectArray::GetByIndex<UEArrayProperty>(int32 Index);
template UEMapProperty ObjectArray::GetByIndex<UEMapProperty>(int32 Index);
template UESetProperty ObjectArray::GetByIndex<UESetProperty>(int32 Index);
template UEEnumProperty ObjectArray::GetByIndex<UEEnumProperty>(int32 Index);