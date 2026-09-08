#include "SharedPredefinedMembers.h"

#include "../Engine/OffsetFinder/Offsets.h"
#include "../Engine/Unreal/ObjectArray.h"

void InitCorePredefinedMembers(PredefinedMemberLookupMapType& OutMembers)
{
	if (GInSDKOffsets.ULevel.Actors != -1)
	{
		UEClass Level = ObjectArray::FindClassFast("Level");

		if (Level == nullptr)
			Level = ObjectArray::FindClassFast("level");

		PredefinedElements& ULevelPredefs = OutMembers[Level.GetIndex()];
		ULevelPredefs.Members =
		    {
		        PredefinedMember{
		            .Comment           = "THIS IS THE ARRAY YOU'RE LOOKING FOR! [NOT AUTO-GENERATED PROPERTY]",
		            .Type              = "class TArray<class AActor*>",
		            .Name              = "Actors",
		            .Offset            = GInSDKOffsets.ULevel.Actors,
		            .Size              = sizeof(TArray<int>),
		            .ArrayDim          = 0x1,
		            .Alignment         = alignof(TArray<int>),
		            .bIsStatic         = false,
		            .bIsZeroSizeMember = false,
		            .bIsBitField       = false,
		            .BitIndex          = 0xFF},
		};
	}

	UEClass DataTable = ObjectArray::FindClassFast("DataTable");

	PredefinedElements& UDataTablePredefs = OutMembers[DataTable.GetIndex()];
	UDataTablePredefs.Members =
	    {
	        PredefinedMember{
	            .Comment           = "So, here's a RowMap. Good luck with it.",
	            .Type              = "TMap<class FName, uint8*>",
	            .Name              = "RowMap",
	            .Offset            = GInSDKOffsets.UDataTable.RowMap,
	            .Size              = sizeof(TMap<int, int>),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(TMap<int, int>),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	};

	PredefinedElements& UObjectPredefs = OutMembers[ObjectArray::FindClassFast("Object").GetIndex()];
	UObjectPredefs.Members =
	    {
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "void*",
	            .Name              = "VTable",
	            .Offset            = GOffsets.UObject.Vft,
	            .Size              = sizeof(void**),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(void**),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "EObjectFlags",
	            .Name              = "Flags",
	            .Offset            = GOffsets.UObject.Flags,
	            .Size              = sizeof(EObjectFlags),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(EObjectFlags),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "int32",
	            .Name              = "Index",
	            .Offset            = GOffsets.UObject.Index,
	            .Size              = sizeof(int32),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(int32),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "class UClass*",
	            .Name              = "Class",
	            .Offset            = GOffsets.UObject.Class,
	            .Size              = sizeof(void*),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(void*),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "class FName",
	            .Name              = "Name",
	            .Offset            = GOffsets.UObject.Name,
	            .Size              = GOffsets.FName.SizeOf,
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(int32),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "class UObject*",
	            .Name              = "Outer",
	            .Offset            = GOffsets.UObject.Outer,
	            .Size              = sizeof(void*),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(void*),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	};

	const UEClass UField              = ObjectArray::FindClassFast("Field");
	PredefinedElements& UFieldPredefs = OutMembers[UField.GetIndex()];

	// Starting from UE5.7 UField::Next is reflected and doesn't need to be added manually anymore
	if (!UField.FindMember("Next", EClassCastFlags::ObjectProperty))
	{
		UFieldPredefs.Members.insert(UFieldPredefs.Members.begin(),
		                             PredefinedMember{
		                                 .Comment           = "NOT AUTO-GENERATED PROPERTY",
		                                 .Type              = "class UField*",
		                                 .Name              = "Next",
		                                 .Offset            = GOffsets.UField.Next,
		                                 .Size              = sizeof(void*),
		                                 .ArrayDim          = 0x1,
		                                 .Alignment         = alignof(void*),
		                                 .bIsStatic         = false,
		                                 .bIsZeroSizeMember = false,
		                                 .bIsBitField       = false,
		                                 .BitIndex          = 0xFF});
	}

	PredefinedElements& UEnumPredefs = OutMembers[ObjectArray::FindClassFast("Enum").GetIndex()];
	UEnumPredefs.Members =
	    {
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "class TArray<class TPair<class FName, int64>>",
	            .Name              = "Names",
	            .Offset            = GOffsets.UEnum.Names,
	            .Size              = sizeof(TArray<int>),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(TArray<int>),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	};

	UEClass UStruct = ObjectArray::FindClassFast("Struct");

	if (UStruct == nullptr)
		UStruct = ObjectArray::FindClassFast("struct");

	PredefinedElements& UStructPredefs = OutMembers[UStruct.GetIndex()];
	UStructPredefs.Members =
	    {
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "int16",
	            .Name              = "MinAlignment",
	            .Offset            = GOffsets.UStruct.MinAlignment,
	            .Size              = sizeof(int16),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(int16),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "int32",
	            .Name              = "Size",
	            .Offset            = GOffsets.UStruct.Size,
	            .Size              = sizeof(int32),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(int32),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	};

	// Starting from UE5.7 UStruct::SuperStruct is reflected and doesn't need to be added manually anymore
	if (!UStruct.FindMember("SuperStruct", EClassCastFlags::ObjectProperty))
	{
		UStructPredefs.Members.insert(UStructPredefs.Members.begin(),
		                              PredefinedMember{
		                                  .Comment           = "NOT AUTO-GENERATED PROPERTY",
		                                  .Type              = "class UStruct*",
		                                  .Name              = "SuperStruct",
		                                  .Offset            = GOffsets.UStruct.SuperStruct,
		                                  .Size              = sizeof(void*),
		                                  .ArrayDim          = 0x1,
		                                  .Alignment         = alignof(void*),
		                                  .bIsStatic         = false,
		                                  .bIsZeroSizeMember = false,
		                                  .bIsBitField       = false,
		                                  .BitIndex          = 0xFF});
	}

	// Starting from UE5.7 UStruct::Children is reflected and doesn't need to be added manually anymore
	if (!UStruct.FindMember("Children", EClassCastFlags::ObjectProperty))
	{
		UStructPredefs.Members.insert(UStructPredefs.Members.begin(),
		                              PredefinedMember{
		                                  .Comment           = "NOT AUTO-GENERATED PROPERTY",
		                                  .Type              = "class UField*",
		                                  .Name              = "Children",
		                                  .Offset            = GOffsets.UStruct.Children,
		                                  .Size              = sizeof(void*),
		                                  .ArrayDim          = 0x1,
		                                  .Alignment         = alignof(void*),
		                                  .bIsStatic         = false,
		                                  .bIsZeroSizeMember = false,
		                                  .bIsBitField       = false,
		                                  .BitIndex          = 0xFF});
	}
	if (InternalSettings::bUseFProperty)
	{
		UStructPredefs.Members.push_back({.Comment           = "NOT AUTO-GENERATED PROPERTY",
		                                  .Type              = "class FField*",
		                                  .Name              = "ChildProperties",
		                                  .Offset            = GOffsets.UStruct.ChildProperties,
		                                  .Size              = sizeof(void*),
		                                  .ArrayDim          = 0x1,
		                                  .Alignment         = alignof(void*),
		                                  .bIsStatic         = false,
		                                  .bIsZeroSizeMember = false,
		                                  .bIsBitField       = false,
		                                  .BitIndex          = 0xFF});
	}

	if (GOffsets.UStruct.StructBaseChain != -1)
	{
		// Use packed struct to fix alignment
		UStructPredefs.Members.push_back({.Comment           = "NOT AUTO-GENERATED PROPERTY",
		                                  .Type              = "struct __attribute__((packed)) FStructBaseChainPacked { FStructBaseChainPacked** StructBaseChainArray = nullptr; int32 NumStructBasesInChainMinusOne = 0; }",
		                                  .Name              = "BaseChain",
		                                  .Offset            = GOffsets.UStruct.StructBaseChain,
		                                  .Size              = sizeof(void*) + sizeof(int32),
		                                  .ArrayDim          = 0x1,
		                                  .Alignment         = alignof(int32),
		                                  .bIsStatic         = false,
		                                  .bIsZeroSizeMember = false,
		                                  .bIsBitField       = false,
		                                  .BitIndex          = 0xFF});
	}

	PredefinedElements& UFunctionPredefs = OutMembers[ObjectArray::FindClassFast("Function").GetIndex()];
	UFunctionPredefs.Members =
	    {
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "EFunctionFlags",
	            .Name              = "FunctionFlags",
	            .Offset            = GOffsets.UFunction.FunctionFlags,
	            .Size              = sizeof(EFunctionFlags),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(EFunctionFlags),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "void*",
	            .Name              = "ExecFunction",
	            .Offset            = GOffsets.UFunction.ExecFunction,
	            .Size              = sizeof(void*),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(void*),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	};

	const UEClass UClass              = ObjectArray::FindClassFast("Class");
	PredefinedElements& UClassPredefs = OutMembers[UClass.GetIndex()];
	UClassPredefs.Members =
	    {
	        PredefinedMember{
	            .Comment           = "NOT AUTO-GENERATED PROPERTY",
	            .Type              = "EClassCastFlags",
	            .Name              = "CastFlags",
	            .Offset            = GOffsets.UClass.CastFlags,
	            .Size              = sizeof(EClassCastFlags),
	            .ArrayDim          = 0x1,
	            .Alignment         = alignof(EClassCastFlags),
	            .bIsStatic         = false,
	            .bIsZeroSizeMember = false,
	            .bIsBitField       = false,
	            .BitIndex          = 0xFF},
	};

	// Starting from UE5.7 UClass::ClassDefaultObject is reflected and doesn't need to be added manually anymore
	if (!UClass.FindMember("ClassDefaultObject", EClassCastFlags::ObjectProperty))
	{
		UClassPredefs.Members.insert(UClassPredefs.Members.begin(),
		                             PredefinedMember{
		                                 .Comment           = "NOT AUTO-GENERATED PROPERTY",
		                                 .Type              = "class UObject*",
		                                 .Name              = "ClassDefaultObject",
		                                 .Offset            = GOffsets.UClass.ClassDefaultObject,
		                                 .Size              = sizeof(void*),
		                                 .ArrayDim          = 0x1,
		                                 .Alignment         = alignof(void*),
		                                 .bIsStatic         = false,
		                                 .bIsZeroSizeMember = false,
		                                 .bIsBitField       = false,
		                                 .BitIndex          = 0xFF});
	}
}