// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "SplatCreator/SplatCreatorSubsystem.h"
#include "Engine/GameInstance.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

void EmptyLinkFunctionForGeneratedCodeSplatCreatorSubsystem() {}

// ********** Begin Cross Module References ********************************************************
ENGINE_API UClass* Z_Construct_UClass_UGameInstanceSubsystem();
REALITYSTREAM_API UClass* Z_Construct_UClass_USplatCreatorSubsystem();
REALITYSTREAM_API UClass* Z_Construct_UClass_USplatCreatorSubsystem_NoRegister();
UPackage* Z_Construct_UPackage__Script_RealityStream();
// ********** End Cross Module References **********************************************************

// ********** Begin Class USplatCreatorSubsystem Function StartPointCloudSystem ********************
struct Z_Construct_UFunction_USplatCreatorSubsystem_StartPointCloudSystem_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "SplatCreator" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Start the point cloud system - CALL THIS FROM BLUEPRINT to initialize\n// In Blueprint: Get Splat Creator Subsystem -> Start Point Cloud System\n" },
#endif
		{ "ModuleRelativePath", "Public/SplatCreator/SplatCreatorSubsystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Start the point cloud system - CALL THIS FROM BLUEPRINT to initialize\nIn Blueprint: Get Splat Creator Subsystem -> Start Point Cloud System" },
#endif
	};
#endif // WITH_METADATA
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_USplatCreatorSubsystem_StartPointCloudSystem_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_USplatCreatorSubsystem, nullptr, "StartPointCloudSystem", nullptr, 0, 0, RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_USplatCreatorSubsystem_StartPointCloudSystem_Statics::Function_MetaDataParams), Z_Construct_UFunction_USplatCreatorSubsystem_StartPointCloudSystem_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_USplatCreatorSubsystem_StartPointCloudSystem()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_USplatCreatorSubsystem_StartPointCloudSystem_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(USplatCreatorSubsystem::execStartPointCloudSystem)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->StartPointCloudSystem();
	P_NATIVE_END;
}
// ********** End Class USplatCreatorSubsystem Function StartPointCloudSystem **********************

// ********** Begin Class USplatCreatorSubsystem ***************************************************
void USplatCreatorSubsystem::StaticRegisterNativesUSplatCreatorSubsystem()
{
	UClass* Class = USplatCreatorSubsystem::StaticClass();
	static const FNameNativePtrPair Funcs[] = {
		{ "StartPointCloudSystem", &USplatCreatorSubsystem::execStartPointCloudSystem },
	};
	FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
}
FClassRegistrationInfo Z_Registration_Info_UClass_USplatCreatorSubsystem;
UClass* USplatCreatorSubsystem::GetPrivateStaticClass()
{
	using TClass = USplatCreatorSubsystem;
	if (!Z_Registration_Info_UClass_USplatCreatorSubsystem.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			StaticPackage(),
			TEXT("SplatCreatorSubsystem"),
			Z_Registration_Info_UClass_USplatCreatorSubsystem.InnerSingleton,
			StaticRegisterNativesUSplatCreatorSubsystem,
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
	return Z_Registration_Info_UClass_USplatCreatorSubsystem.InnerSingleton;
}
UClass* Z_Construct_UClass_USplatCreatorSubsystem_NoRegister()
{
	return USplatCreatorSubsystem::GetPrivateStaticClass();
}
struct Z_Construct_UClass_USplatCreatorSubsystem_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "BlueprintType", "true" },
		{ "IncludePath", "SplatCreator/SplatCreatorSubsystem.h" },
		{ "ModuleRelativePath", "Public/SplatCreator/SplatCreatorSubsystem.h" },
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FClassFunctionLinkInfo FuncInfo[] = {
		{ &Z_Construct_UFunction_USplatCreatorSubsystem_StartPointCloudSystem, "StartPointCloudSystem" }, // 4248716846
	};
	static_assert(UE_ARRAY_COUNT(FuncInfo) < 2048);
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<USplatCreatorSubsystem>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_USplatCreatorSubsystem_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UGameInstanceSubsystem,
	(UObject* (*)())Z_Construct_UPackage__Script_RealityStream,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_USplatCreatorSubsystem_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_USplatCreatorSubsystem_Statics::ClassParams = {
	&USplatCreatorSubsystem::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	FuncInfo,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	UE_ARRAY_COUNT(FuncInfo),
	0,
	0,
	0x001000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_USplatCreatorSubsystem_Statics::Class_MetaDataParams), Z_Construct_UClass_USplatCreatorSubsystem_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_USplatCreatorSubsystem()
{
	if (!Z_Registration_Info_UClass_USplatCreatorSubsystem.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_USplatCreatorSubsystem.OuterSingleton, Z_Construct_UClass_USplatCreatorSubsystem_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_USplatCreatorSubsystem.OuterSingleton;
}
USplatCreatorSubsystem::USplatCreatorSubsystem() {}
DEFINE_VTABLE_PTR_HELPER_CTOR(USplatCreatorSubsystem);
USplatCreatorSubsystem::~USplatCreatorSubsystem() {}
// ********** End Class USplatCreatorSubsystem *****************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_alexi_OneDrive_Documents_Unreal_Projects_Reconstruction_3D_Plugins_RealityStream_Source_RealityStream_Public_SplatCreator_SplatCreatorSubsystem_h__Script_RealityStream_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_USplatCreatorSubsystem, USplatCreatorSubsystem::StaticClass, TEXT("USplatCreatorSubsystem"), &Z_Registration_Info_UClass_USplatCreatorSubsystem, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(USplatCreatorSubsystem), 2124018205U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_alexi_OneDrive_Documents_Unreal_Projects_Reconstruction_3D_Plugins_RealityStream_Source_RealityStream_Public_SplatCreator_SplatCreatorSubsystem_h__Script_RealityStream_438118672(TEXT("/Script/RealityStream"),
	Z_CompiledInDeferFile_FID_Users_alexi_OneDrive_Documents_Unreal_Projects_Reconstruction_3D_Plugins_RealityStream_Source_RealityStream_Public_SplatCreator_SplatCreatorSubsystem_h__Script_RealityStream_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_alexi_OneDrive_Documents_Unreal_Projects_Reconstruction_3D_Plugins_RealityStream_Source_RealityStream_Public_SplatCreator_SplatCreatorSubsystem_h__Script_RealityStream_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
