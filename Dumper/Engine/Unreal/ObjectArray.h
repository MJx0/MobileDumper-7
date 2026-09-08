#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "../OffsetFinder/Layouts.h"

#include "UnrealObjects.h"

namespace fs = std::filesystem;

class ObjectArray
{
private:
	friend struct FChunkedFixedUObjectArray;
	friend struct FFixedUObjectArray;

	static inline std::function<uintptr_t(int32 Index)> ByIndexFn = nullptr;

public:
	static inline std::function<void(uintptr_t&)> DecryptObjectItemFn = nullptr;

	inline static void SetByIndexFn(const std::function<uintptr_t(int32 Index)>& Fn) { ByIndexFn = Fn; }
	inline static void SetDecryptObjectItemFn(const std::function<void(uintptr_t&)>& Fn) { DecryptObjectItemFn = Fn; }

	static int32 Num();

	template <typename UEType = UEObject>
	static UEType GetByIndex(int32 Index);

	template <typename UEType = UEObject>
	static UEType FindObject(const std::string& FullName, EClassCastFlags RequiredType = EClassCastFlags::None);

	template <typename UEType = UEObject>
	static UEType FindObjectFast(const std::string& Name, EClassCastFlags RequiredType = EClassCastFlags::None);

	template <typename UEType = UEObject>
	static UEType FindObjectFastInOuter(const std::string& Name, std::string Outer);

	static UEStruct FindStruct(const std::string& FullName);
	static UEStruct FindStructFast(const std::string& Name);

	static UEClass FindClass(const std::string& FullName);
	static UEClass FindClassFast(const std::string& Name);

	class ObjectsIterator
	{
		UEObject CurrentObject;
		int32 CurrentIndex;

	public:
		ObjectsIterator(int32 StartIndex = 0);

		UEObject operator*() const;
		ObjectsIterator& operator++();
		bool operator==(const ObjectsIterator& Other) const;
		bool operator!=(const ObjectsIterator& Other) const;

		int32 GetIndex() const;
	};

	ObjectsIterator begin();
	ObjectsIterator end();
};

class AllFFieldIterator
{
private:
	ObjectArray::ObjectsIterator CurrentObject;
	ObjectArray::ObjectsIterator ObjectEndIterator;
	std::vector<UEFField> Fields;
	int PropertyIndex = 0;

public:
	AllFFieldIterator()
	    : CurrentObject(ObjectArray().begin()),
	      ObjectEndIterator(ObjectArray().end())
	{
		if (!IsCurrentObjectStruct())
			IterateToNextStructWithMembers();
	}

	AllFFieldIterator(ObjectArray::ObjectsIterator StartPos)
	    : CurrentObject(StartPos),
	      ObjectEndIterator(ObjectArray().end())
	{
	}

public:
	inline AllFFieldIterator begin() const
	{
		return AllFFieldIterator();
	}
	inline AllFFieldIterator end() const
	{
		return AllFFieldIterator(ObjectArray().end());
	}

	bool operator!=(const AllFFieldIterator& Other) const;

	AllFFieldIterator& operator++();
	UEFField operator*() const;

private:
	void IterateToNextStruct();
	void IterateToNextStructWithMembers();

private:
	inline bool CurrenStructHasMoreMembers() const
	{
		return (static_cast<size_t>(PropertyIndex) + 1) < Fields.size();
	}

	inline UEStruct GetCurrentStruct()
	{
		return (*CurrentObject).Cast<UEStruct>();
	}

	inline bool IsCurrentObjectStruct()
	{
		return (*CurrentObject).IsA(EClassCastFlags::Struct);
	}

	inline bool IsEndIterator() const
	{
		return CurrentObject == ObjectEndIterator;
	}
};
