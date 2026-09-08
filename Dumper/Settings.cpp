#include "Settings.h"

#include <fmt/format.h>

#include "Utils/Logger.h"

#include "Engine/Unreal/ObjectArray.h"
#include "Engine/Unreal/UnrealObjects.h"

void InternalSettings::InitWeakObjectPtrSettings()
{
	const UEStruct LoadAsset = ObjectArray::FindObjectFast<UEFunction>("LoadAsset", EClassCastFlags::Function);

	if (!LoadAsset)
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitWeakObjectPtrSettings: 'LoadAsset' wasn't found, could not determine value for 'bIsWeakObjectPtrWithoutTag'!\n");
		return;
	}

	const UEProperty Asset = LoadAsset.FindMember("Asset", EClassCastFlags::SoftObjectProperty);
	if (!Asset)
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitWeakObjectPtrSettings: 'Asset' wasn't found, could not determine value for 'bIsWeakObjectPtrWithoutTag'!\n");
		return;
	}

	const UEStruct SoftObjectPath = ObjectArray::FindStructFast("SoftObjectPath");

	constexpr int32 SizeOfFFWeakObjectPtr = 0x08;
	constexpr int32 OldUnrealAssetPtrSize = 0x10;
	const int32 SizeOfSoftObjectPath      = SoftObjectPath ? SoftObjectPath.GetStructSize() : OldUnrealAssetPtrSize;

	InternalSettings::bIsWeakObjectPtrWithoutTag = Asset.GetSize() <= (SizeOfSoftObjectPath + SizeOfFFWeakObjectPtr);
}

void InternalSettings::InitLargeWorldCoordinateSettings()
{
	const UEStruct FVectorStruct = ObjectArray::FindStructFast("Vector");

	if (!FVectorStruct) [[unlikely]]
	{
		GLogger.FmtWrite(ELogLevel::Error, "Something went horribly wrong, FVector wasn't even found!\n");
		return;
	}

	const UEProperty XProperty = FVectorStruct.FindMember("X");

	if (!XProperty) [[unlikely]]
	{
		GLogger.FmtWrite(ELogLevel::Error, "Something went horribly wrong, FVector::X wasn't even found!\n");
		return;
	}

	InternalSettings::bUseLargeWorldCoordinates = XProperty.IsA(EClassCastFlags::DoubleProperty);
}

void InternalSettings::InitObjectPtrPropertySettings()
{
	const UEClass ObjectPtrPropertyClass = ObjectArray::FindClassFast("ObjectPtrProperty");

	if (!ObjectPtrPropertyClass)
	{
		InternalSettings::bIsObjPtrInsteadOfFieldPathProperty = false;
		return;
	}

	InternalSettings::bIsObjPtrInsteadOfFieldPathProperty = ObjectPtrPropertyClass.GetDefaultObject().IsA(EClassCastFlags::FieldPathProperty);
}

void InternalSettings::InitArrayDimSizeSettings()
{
	for (const UEObject Obj : ObjectArray())
	{
		if (!Obj.IsA(EClassCastFlags::Struct))
			continue;

		const UEStruct AsStruct = Obj.Cast<UEStruct>();

		for (const UEProperty Property : AsStruct.GetProperties())
		{
			if (Property.GetArrayDim() >= 0x000F0001)
			{
				InternalSettings::bUseUint8ArrayDim = true;
				return;
			}
		}
	}

	InternalSettings::bUseUint8ArrayDim = false;
}
