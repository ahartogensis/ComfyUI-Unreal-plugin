// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MeshImport/Hyper3DObjectsSubsystem.h"
#include "Engine/GameInstance.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

void EmptyLinkFunctionForGeneratedCodeHyper3DObjectsSubsystem() {}

// ********** Begin Cross Module References ********************************************************
ENGINE_API UClass* Z_Construct_UClass_UGameInstanceSubsystem();
ENGINE_API UClass* Z_Construct_UClass_UTexture2D_NoRegister();
REALITYSTREAM_API UClass* Z_Construct_UClass_UHyper3DObjectsSubsystem();
REALITYSTREAM_API UClass* Z_Construct_UClass_UHyper3DObjectsSubsystem_NoRegister();
UPackage* Z_Construct_UPackage__Script_RealityStream();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UHyper3DObjectsSubsystem Function ActivateObjectImports ******************
struct Z_Construct_UFunction_UHyper3DObjectsSubsystem_ActivateObjectImports_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Hyper3DObjects" },
		{ "ModuleRelativePath", "Public/MeshImport/Hyper3DObjectsSubsystem.h" },
	};
#endif // WITH_METADATA
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UHyper3DObjectsSubsystem_ActivateObjectImports_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UHyper3DObjectsSubsystem, nullptr, "ActivateObjectImports", nullptr, 0, 0, RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UHyper3DObjectsSubsystem_ActivateObjectImports_Statics::Function_MetaDataParams), Z_Construct_UFunction_UHyper3DObjectsSubsystem_ActivateObjectImports_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_UHyper3DObjectsSubsystem_ActivateObjectImports()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UHyper3DObjectsSubsystem_ActivateObjectImports_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UHyper3DObjectsSubsystem::execActivateObjectImports)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->ActivateObjectImports();
	P_NATIVE_END;
}
// ********** End Class UHyper3DObjectsSubsystem Function ActivateObjectImports ********************

// ********** Begin Class UHyper3DObjectsSubsystem Function DeactivateObjectImports ****************
struct Z_Construct_UFunction_UHyper3DObjectsSubsystem_DeactivateObjectImports_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Hyper3DObjects" },
		{ "ModuleRelativePath", "Public/MeshImport/Hyper3DObjectsSubsystem.h" },
	};
#endif // WITH_METADATA
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UHyper3DObjectsSubsystem_DeactivateObjectImports_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UHyper3DObjectsSubsystem, nullptr, "DeactivateObjectImports", nullptr, 0, 0, RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UHyper3DObjectsSubsystem_DeactivateObjectImports_Statics::Function_MetaDataParams), Z_Construct_UFunction_UHyper3DObjectsSubsystem_DeactivateObjectImports_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_UHyper3DObjectsSubsystem_DeactivateObjectImports()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UHyper3DObjectsSubsystem_DeactivateObjectImports_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UHyper3DObjectsSubsystem::execDeactivateObjectImports)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->DeactivateObjectImports();
	P_NATIVE_END;
}
// ********** End Class UHyper3DObjectsSubsystem Function DeactivateObjectImports ******************

// ********** Begin Class UHyper3DObjectsSubsystem *************************************************
void UHyper3DObjectsSubsystem::StaticRegisterNativesUHyper3DObjectsSubsystem()
{
	UClass* Class = UHyper3DObjectsSubsystem::StaticClass();
	static const FNameNativePtrPair Funcs[] = {
		{ "ActivateObjectImports", &UHyper3DObjectsSubsystem::execActivateObjectImports },
		{ "DeactivateObjectImports", &UHyper3DObjectsSubsystem::execDeactivateObjectImports },
	};
	FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
}
FClassRegistrationInfo Z_Registration_Info_UClass_UHyper3DObjectsSubsystem;
UClass* UHyper3DObjectsSubsystem::GetPrivateStaticClass()
{
	using TClass = UHyper3DObjectsSubsystem;
	if (!Z_Registration_Info_UClass_UHyper3DObjectsSubsystem.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			StaticPackage(),
			TEXT("Hyper3DObjectsSubsystem"),
			Z_Registration_Info_UClass_UHyper3DObjectsSubsystem.InnerSingleton,
			StaticRegisterNativesUHyper3DObjectsSubsystem,
			sizeof(TClass),
			alignof(TClass),
			TClass::StaticClassFlags,
			TClass::StaticClassCastFlags(),
			TClass::StaticConfigName(),
			(UClass::ClassConstructorType)InternalConstructor<TClass>,
			(UClass::ClassVTableHelperCtorCallerType)InternalVTableHelperCtorCaller<TClass>,
			UOBJECT_CPPCLASS_STATICFUNCTIONS_FORCLASS(TClass),
			&TClass::Super::StaticClass,
			&TClass::WithinClass::StaticClass
		);
	}
	return Z_Registration_Info_UClass_UHyper3DObjectsSubsystem.InnerSingleton;
}
UClass* Z_Construct_UClass_UHyper3DObjectsSubsystem_NoRegister()
{
	return UHyper3DObjectsSubsystem::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "BlueprintType", "true" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Imports all OBJ meshes (with optional textures) found in the plugin's MeshImport folder\n * and animates them as floating objects orbiting around the world origin.\n */" },
#endif
		{ "IncludePath", "MeshImport/Hyper3DObjectsSubsystem.h" },
		{ "ModuleRelativePath", "Public/MeshImport/Hyper3DObjectsSubsystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Imports all OBJ meshes (with optional textures) found in the plugin's MeshImport folder\nand animates them as floating objects orbiting around the world origin." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_LoadedTextures_MetaData[] = {
		{ "ModuleRelativePath", "Public/MeshImport/Hyper3DObjectsSubsystem.h" },
	};
#endif // WITH_METADATA
	static const UECodeGen_Private::FObjectPropertyParams NewProp_LoadedTextures_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_LoadedTextures;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
	static UObject* (*const DependentSingletons[])();
	static constexpr FClassFunctionLinkInfo FuncInfo[] = {
		{ &Z_Construct_UFunction_UHyper3DObjectsSubsystem_ActivateObjectImports, "ActivateObjectImports" }, // 4231357277
		{ &Z_Construct_UFunction_UHyper3DObjectsSubsystem_DeactivateObjectImports, "DeactivateObjectImports" }, // 3013261247
	};
	static_assert(UE_ARRAY_COUNT(FuncInfo) < 2048);
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UHyper3DObjectsSubsystem>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::NewProp_LoadedTextures_Inner = { "LoadedTextures", nullptr, (EPropertyFlags)0x0104000000000000, UECodeGen_Private::EPropertyGenFlags::Object | UECodeGen_Private::EPropertyGenFlags::ObjectPtr, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, Z_Construct_UClass_UTexture2D_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FArrayPropertyParams Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::NewProp_LoadedTextures = { "LoadedTextures", nullptr, (EPropertyFlags)0x0144000000000000, UECodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UHyper3DObjectsSubsystem, LoadedTextures), EArrayPropertyFlags::None, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_LoadedTextures_MetaData), NewProp_LoadedTextures_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::NewProp_LoadedTextures_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::NewProp_LoadedTextures,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::PropPointers) < 2048);
UObject* (*const Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UGameInstanceSubsystem,
	(UObject* (*)())Z_Construct_UPackage__Script_RealityStream,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::ClassParams = {
	&UHyper3DObjectsSubsystem::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	FuncInfo,
	Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	UE_ARRAY_COUNT(FuncInfo),
	UE_ARRAY_COUNT(Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::PropPointers),
	0,
	0x001000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::Class_MetaDataParams), Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_UHyper3DObjectsSubsystem()
{
	if (!Z_Registration_Info_UClass_UHyper3DObjectsSubsystem.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UHyper3DObjectsSubsystem.OuterSingleton, Z_Construct_UClass_UHyper3DObjectsSubsystem_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UHyper3DObjectsSubsystem.OuterSingleton;
}
UHyper3DObjectsSubsystem::UHyper3DObjectsSubsystem() {}
DEFINE_VTABLE_PTR_HELPER_CTOR(UHyper3DObjectsSubsystem);
UHyper3DObjectsSubsystem::~UHyper3DObjectsSubsystem() {}
// ********** End Class UHyper3DObjectsSubsystem ***************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_alexi_OneDrive_Documents_Unreal_Projects_Reconstruction_3D_Plugins_RealityStream_Source_RealityStream_Public_MeshImport_Hyper3DObjectsSubsystem_h__Script_RealityStream_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UHyper3DObjectsSubsystem, UHyper3DObjectsSubsystem::StaticClass, TEXT("UHyper3DObjectsSubsystem"), &Z_Registration_Info_UClass_UHyper3DObjectsSubsystem, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UHyper3DObjectsSubsystem), 3655299707U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_alexi_OneDrive_Documents_Unreal_Projects_Reconstruction_3D_Plugins_RealityStream_Source_RealityStream_Public_MeshImport_Hyper3DObjectsSubsystem_h__Script_RealityStream_1512035833(TEXT("/Script/RealityStream"),
	Z_CompiledInDeferFile_FID_Users_alexi_OneDrive_Documents_Unreal_Projects_Reconstruction_3D_Plugins_RealityStream_Source_RealityStream_Public_MeshImport_Hyper3DObjectsSubsystem_h__Script_RealityStream_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_alexi_OneDrive_Documents_Unreal_Projects_Reconstruction_3D_Plugins_RealityStream_Source_RealityStream_Public_MeshImport_Hyper3DObjectsSubsystem_h__Script_RealityStream_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
