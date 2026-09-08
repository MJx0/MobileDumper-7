#pragma once

#include <cstdint>
#include <string>

#include "../Unreal/Enums.h"

#include "Layouts.h"

struct FInGenOffsets
{
	bool Init(std::string& OutErrorString);

	struct
	{
		int32 CompIdx = 0;
		int32 Number  = -1;
		int32 SizeOf  = 8;
	} FName;

	struct
	{
		int32 Vft                = 0;
		int32 Class              = -1;
		int32 Owner              = -1;
		int32 Next               = -1;
		int32 Name               = -1;
		int32 EditorOnlyMetadata = -1;
	} FField;

	struct
	{
		int32 Name      = -1;
		int32 CastFlags = -1;
	} FFieldClass;

	struct
	{
		int32 Vft   = 0;
		int32 Flags = -1;
		int32 Index = -1;
		int32 Class = -1;
		int32 Name  = -1;
		int32 Outer = -1;
	} UObject;

	struct
	{
		int32 Next = -1;
	} UField;

	struct
	{
		int32 Names          = -1;
		int32 UnderlyingType = -1;
	} UEnum;

	struct
	{
		int32 StructBaseChain = -1;
		int32 SuperStruct     = -1;
		int32 Children        = -1;
		int32 ChildProperties = -1;
		int32 Size            = -1;
		int32 MinAlignment    = -1;
	} UStruct;

	struct
	{
		int32 FunctionFlags = -1;
		int32 NumParams     = -1;
		int32 ParamSize     = -1;
		int32 ExecFunction  = -1;
	} UFunction;

	struct
	{
		int32 CastFlags             = -1;
		int32 ClassDefaultObject    = -1;
		int32 ImplementedInterfaces = -1;
	} UClass;

	struct
	{
		int32 ArrayDim        = -1;
		int32 ElementSize     = -1;
		int32 PropertyFlags   = -1;
		int32 Offset_Internal = -1;
		int32 SizeOf          = -1;
	} Property;

	struct
	{
		int32 Enum = -1;
	} ByteProperty;

	struct
	{
		int32 Base = -1;
	} BoolProperty;

	struct
	{
		int32 PropertyClass = -1;
	} ObjectProperty;

	struct
	{
		int32 MetaClass = -1;
	} ClassProperty;

	struct
	{
		int32 Struct = -1;
	} StructProperty;

	struct
	{
		int32 Inner = -1;
	} ArrayProperty;

	struct
	{
		int32 SignatureFunction = -1;
	} DelegateProperty;

	struct
	{
		int32 Base = -1;
	} MapProperty;

	struct
	{
		int32 ElementProp = -1;
	} SetProperty;

	struct
	{
		int32 Base = -1;
	} EnumProperty;

	struct
	{
		int32 FieldClass = -1;
	} FieldPathProperty;

	struct
	{
		int32 ValueProperty = -1;
	} OptionalProperty;

	struct
	{
		int32 ScriptStruct = 0;
		int32 StructMemory = sizeof(void*);
	} FInstancedStruct;

	/* UObject */
	bool Init_UObject_Flags();
	bool Init_UObject_Index();
	bool Init_UObject_Class();
	bool Init_UObject_Name();
	bool Init_UObject_Outer();

	/* FName */
	void PreInit_FName();
	void PostInit_FName();

	/* UField */
	bool Init_UField_Next();

	/* FField */
	bool Init_FField_Name();
	bool Init_FField_Class();
	bool Init_FField_Owner();
	bool Init_FField_Next();
	bool Init_FField_EditorOnlyMetaData();

	/* FFieldClass */
	bool Init_FFieldClass_Name();
	bool Init_FFieldClass_CastFlags();

	/* UEnum */
	bool Init_UEnum_Names();
	bool Init_UEnum_UnderlayingType();

	/* UStruct */
	bool Init_UStruct_SuperStruct();
	bool Init_UStruct_Children();
	bool Init_UStruct_ChildProperties();
	bool Init_UStruct_Size();
	bool Init_UStruct_MinAlignment();
	bool Init_UStruct_StructBaseChain();

	/* UFunction */
	bool Init_UFunction_FunctionFlags();
	bool Init_UFunction_NumParams();
	bool Init_UFunction_ParamSize();
	bool Init_UFunction_ExecFunction();

	/* UClass */
	bool Init_UClass_CastFlags();
	bool Init_UClass_ClassDefaultObject();
	bool Init_UClass_InitImplementedInterfaces();

	/* Property */
	bool Init_Property_ElementSize();
	bool Init_Property_ArrayDim();
	bool Init_Property_PropertyFlags();
	bool Init_Property_OffsetInternal();
	bool Init_Property_SizeOf();

	/* BoolProperty */
	bool Init_BoolProperty_Base();

	/* EnumProperty */
	bool Init_EnumProperty_Base();

	/* ObjectProperty */
	bool Init_ObjectProperty_PropertyClass();

	/* ByteProperty */
	bool Init_ByteProperty_Enum();

	/* StructProperty */
	bool Init_StructProperty_Struct();

	/* DelegateProperty */
	bool Init_DelegateProperty_SignatureFunction();

	/* ArrayProperty */
	bool Init_ArrayProperty_Inner();

	/* SetProperty */
	bool Init_SetProperty_ElementProp();

	/* MapProperty */
	bool Init_MapProperty_Base();
};

// Offsets not to be used during generation but inside of the generated SDK
struct FInSDKOffsets
{
	struct
	{
		int64 GNames   = 0x0;
		int64 GObjects = 0x0;
		int64 GEngine  = 0x0;
		int64 GWorld   = 0x0;
		int64 PEOffset = 0x0;
		int32 PEIndex  = 0x0;
	} Statics;

	struct
	{
		int32 Size             = -1;
		int32 TextData         = -1;
		int32 InTextDataString = -1;
	} FText;

	struct
	{
		int32 Actors = -1;
	} ULevel;

	struct
	{
		int32 RowMap = -1;
	} UDataTable;

	/* These properties' size might change depending on the UE version or compiler flags. */
	struct
	{
		int32 SizeOf = -1;
	} DelegateProperty;

	struct
	{
		int32 SizeOf = -1;
	} FieldPathProperty;

	struct
	{
		int32 SizeOf = -1;
	} MulticastInlineDelegateProperty;

	void Init();

private:
	void InitLevelActorsOffset();
	void InitDatatableRowMapOffset();
	void InitUEngineAndUWorld();
	void InitFText();
	void InitTDelegateSize();
	void InitFFieldPathSize();
	void InitTMulticastInlineDelegateSize();
	void InitProcessEvent();
};

namespace PropertyBaseTypes
{
	struct UBoolPropertyBase
	{
		uint8 FieldSize;
		uint8 ByteOffset;
		uint8 ByteMask;
		uint8 FieldMask;
	};

	struct UMapPropertyBase
	{
		void* KeyProperty;
		void* ValueProperty;
	};

	struct UEnumPropertyBase
	{
		void* UnderlayingProperty;
		class UEnum* Enum;
	};
}

extern uintptr_t GObjects;
extern uintptr_t GNames;
extern FLayouts GLayouts;
extern FInGenOffsets GOffsets;
extern FInSDKOffsets GInSDKOffsets;