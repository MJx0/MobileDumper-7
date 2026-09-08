#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Enums.h"
#include "UnrealTypes.h"

class UEClass;
class UEFField;
class UEObject;
class UEProperty;

class UEFFieldClass
{
protected:
	uint8* Class;

public:
	UEFFieldClass()                                = default;
	UEFFieldClass& operator=(const UEFFieldClass&) = default;

	UEFFieldClass(void* NewFieldClass);
	UEFFieldClass(const UEFFieldClass& OldFieldClass);

	void* GetAddress();

	explicit operator bool() const;

	EClassCastFlags GetCastFlags() const;
	FName GetFName() const;

	bool IsType(EClassCastFlags Flags) const;

	std::string GetName() const;
	std::string GetValidName() const;
	std::string GetCppName() const;
};

class UEFField
{
protected:
	uint8* Field;

public:
	UEFField()                           = default;
	UEFField& operator=(const UEFField&) = default;

	UEFField(void* NewField);
	UEFField(const UEFField& OldField);

	void* GetAddress();
	const void* GetAddress() const;

	class UEObject GetOwnerAsUObject() const;
	class UEFField GetOwnerAsFField() const;
	class UEObject GetOwnerUObject() const;
	UEFFieldClass GetClass() const;
	FName GetFName() const;
	UEFField GetNext() const;

	std::vector<std::pair<std::string, std::string>> GetMetaData() const;

	template <typename UEType>
	UEType Cast() const;

	bool IsOwnerUObject() const;
	bool IsA(EClassCastFlags Flags) const;

	std::string GetName() const;
	std::string GetValidName() const;
	std::string GetCppName() const;

	explicit operator bool() const;
	bool operator==(const UEFField& Other) const;
	bool operator!=(const UEFField& Other) const;
};

class UEObject
{
private:
	static void (*PE)(void*, void*, void*);

protected:
	uint8* Object;

public:
	UEObject() = default;

	UEObject(void* NewObject);

	UEObject(const UEObject&)            = default;
	UEObject& operator=(const UEObject&) = default;

	void* GetAddress();
	const void* GetAddress() const;

	void* GetVft() const;
	EObjectFlags GetFlags() const;
	int32 GetIndex() const;
	UEClass GetClass() const;
	FName GetFName() const;
	UEObject GetOuter() const;

	int32 GetPackageIndex() const;

	bool HasAnyFlags(EObjectFlags Flags) const;

	bool IsA(EClassCastFlags TypeFlags) const;
	bool IsA(UEClass Class) const;

	UEObject GetOutermost() const;

	std::string StringifyObjFlags() const;

	std::string GetName() const;
	std::string GetNameWithPath() const;
	std::string GetValidName() const;
	std::string GetCppName() const;
	std::string GetFullName(int32& OutNameLength) const;
	std::string GetFullName() const;
	std::string GetPathName() const;

	explicit operator bool() const;
	explicit operator uint8*();
	bool operator==(const UEObject& Other) const;
	bool operator!=(const UEObject& Other) const;

	void ProcessEvent(class UEFunction Func, void* Params);

public:
	template <typename UEType>
	inline UEType Cast()
	{
		return UEType(Object);
	}

	template <typename UEType>
	inline const UEType Cast() const
	{
		return UEType(Object);
	}
};

class UEField : public UEObject
{
	using UEObject::UEObject;

public:
	UEField GetNext() const;
	bool IsNextValid() const;
};

class UEEnum : public UEField
{
	using UEField::UEField;

public:
	enum class EUnderlyingType : uint8
	{
		int8,
		int16,
		int32,
		int64,
		uint8,
		uint16,
		uint32,
		uint64,
	};

public:
	std::vector<std::pair<FName, int64>> GetNameValuePairs() const;
	std::string GetSingleName(int32 Index) const;
	std::string GetEnumPrefixedName() const;
	std::string GetEnumTypeAsStr() const;

	/* Only valid on UE5.8+ (or games that added 'EUnderlyingType'). Returns Pair<Size, IsSigned> */
	std::pair<uint8_t, bool> GetSizeSignedPair() const;
};

class UEStruct : public UEField
{
	using UEField::UEField;

public:
	UEStruct GetSuper() const;
	UEField GetChild() const;
	UEFField GetChildProperties() const;
	int32 GetStructSize() const;

	/*
	 * The type of UStruct::MinAlignemnt was changed from int32 to int16 on UE5.6.
	 *
	 * Using int16 in the dumper is likely fully backwards compatible, as I've never seen any class with a MinAlignment value greater than 0x10.
	 */
	int16 GetMinAlignment() const;

	bool HasType(UEStruct Type) const;

	std::vector<UEProperty> GetProperties() const;
	std::vector<UEFunction> GetFunctions() const;

	UEProperty FindMember(const std::string& MemberName, EClassCastFlags TypeFlags = EClassCastFlags::None) const;

	bool HasMembers() const;
};

class UEFunction : public UEStruct
{
	using UEStruct::UEStruct;

public:
	EFunctionFlags GetFunctionFlags() const;
	bool HasFlags(EFunctionFlags Flags) const;
	uint8 GetNumParams() const;
	uint16 GetParamSize() const;
	void* GetExecFunction() const;

	UEProperty GetReturnProperty() const;

	std::string StringifyFlags(const char* Seperator = ", ") const;
	std::string GetParamStructName() const;
};

class UEClass : public UEStruct
{
	using UEStruct::UEStruct;

public:
	EClassCastFlags GetCastFlags() const;
	std::string StringifyCastFlags() const;
	bool IsType(EClassCastFlags TypeFlag) const;
	UEObject GetDefaultObject() const;
	TArray<FImplementedInterface> GetImplementedInterfaces() const;

	UEFunction GetFunction(const std::string& ClassName, const std::string& FuncName) const;
};

class UEProperty
{
protected:
	uint8* Base;

public:
	UEProperty()                             = default;
	UEProperty(const UEProperty&)            = default;
	UEProperty& operator=(const UEProperty&) = default;

	UEProperty(void* NewProperty)
	    : Base(reinterpret_cast<uint8*>(NewProperty))
	{
	}

public:
	void* GetAddress();
	const void* GetAddress() const;

	std::pair<UEClass, UEFFieldClass> GetClass() const;
	EClassCastFlags GetCastFlags() const;

	explicit operator bool() const;

	bool IsA(EClassCastFlags TypeFlags) const;

	FName GetFName() const;
	int32 GetArrayDim() const;
	int32 GetSize() const;
	int32 GetOffset() const;
	EPropertyFlags GetPropertyFlags() const;
	bool HasPropertyFlags(EPropertyFlags PropertyFlag) const;
	bool IsType(EClassCastFlags PossibleTypes) const;

	int32 GetAlignment() const;

	std::string GetName() const;
	std::string GetValidName() const;

	std::string GetCppType() const;

	std::string GetPropClassName() const;

	std::string StringifyFlags() const;

public:
	template <typename UEType>
	UEType Cast()
	{
		return UEType(Base);
	}

	template <typename UEType>
	const UEType Cast() const
	{
		return UEType(Base);
	}
};

class UEByteProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEEnum GetEnum() const;

	std::string GetCppType() const;
};

class UEBoolProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	uint8 GetFieldMask() const;
	uint8 GetByteOffset() const;
	uint8 GetBitIndex() const;
	bool IsNativeBool() const;

	std::string GetCppType() const;
};

class UEObjectProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEClass GetPropertyClass() const;

	std::string GetCppType() const;
};

class UEClassProperty : public UEObjectProperty
{
	using UEObjectProperty::UEObjectProperty;

public:
	UEClass GetMetaClass() const;

	std::string GetCppType() const;
};

class UEWeakObjectProperty : public UEObjectProperty
{
	using UEObjectProperty::UEObjectProperty;

public:
	std::string GetCppType() const;
};

class UELazyObjectProperty : public UEObjectProperty
{
	using UEObjectProperty::UEObjectProperty;

public:
	std::string GetCppType() const;
};

class UESoftObjectProperty : public UEObjectProperty
{
	using UEObjectProperty::UEObjectProperty;

public:
	std::string GetCppType() const;
};

class UESoftClassProperty : public UEClassProperty
{
	using UEClassProperty::UEClassProperty;

public:
	std::string GetCppType() const;
};

class UEInterfaceProperty : public UEObjectProperty
{
	using UEObjectProperty::UEObjectProperty;

public:
	std::string GetCppType() const;
};

class UEStructProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEStruct GetUnderlayingStruct() const;

	std::string GetCppType() const;
};

class UEArrayProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEProperty GetInnerProperty() const;

	std::string GetCppType() const;
};

class UEDelegateProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEFunction GetSignatureFunction() const;

	std::string GetCppType() const;
};

class UEMulticastInlineDelegateProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEFunction GetSignatureFunction() const;

	std::string GetCppType() const;
};

class UEMapProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEProperty GetKeyProperty() const;
	UEProperty GetValueProperty() const;

	std::string GetCppType() const;
};

class UESetProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEProperty GetElementProperty() const;

	std::string GetCppType() const;
};

class UEEnumProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEProperty GetUnderlayingProperty() const;
	UEEnum GetEnum() const;

	std::string GetCppType() const;
};

class UEFieldPathProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEFFieldClass GetFieldClass() const;

	std::string GetCppType() const;
};

class UEOptionalProperty : public UEProperty
{
	using UEProperty::UEProperty;

public:
	UEProperty GetValueProperty() const;

	std::string GetCppType() const;
};