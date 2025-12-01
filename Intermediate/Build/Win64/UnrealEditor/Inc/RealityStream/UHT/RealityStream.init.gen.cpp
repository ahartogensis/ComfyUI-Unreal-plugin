// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeRealityStream_init() {}
	REALITYSTREAM_API UFunction* Z_Construct_UDelegateFunction_RealityStream_OnConnectionStatusChanged__DelegateSignature();
	REALITYSTREAM_API UFunction* Z_Construct_UDelegateFunction_RealityStream_OnError__DelegateSignature();
	REALITYSTREAM_API UFunction* Z_Construct_UDelegateFunction_RealityStream_OnFullFrameReady__DelegateSignature();
	REALITYSTREAM_API UFunction* Z_Construct_UDelegateFunction_RealityStream_OnTextureReceived__DelegateSignature();
	static FPackageRegistrationInfo Z_Registration_Info_UPackage__Script_RealityStream;
	FORCENOINLINE UPackage* Z_Construct_UPackage__Script_RealityStream()
	{
		if (!Z_Registration_Info_UPackage__Script_RealityStream.OuterSingleton)
		{
			static UObject* (*const SingletonFuncArray[])() = {
				(UObject* (*)())Z_Construct_UDelegateFunction_RealityStream_OnConnectionStatusChanged__DelegateSignature,
				(UObject* (*)())Z_Construct_UDelegateFunction_RealityStream_OnError__DelegateSignature,
				(UObject* (*)())Z_Construct_UDelegateFunction_RealityStream_OnFullFrameReady__DelegateSignature,
				(UObject* (*)())Z_Construct_UDelegateFunction_RealityStream_OnTextureReceived__DelegateSignature,
			};
			static const UECodeGen_Private::FPackageParams PackageParams = {
				"/Script/RealityStream",
				SingletonFuncArray,
				UE_ARRAY_COUNT(SingletonFuncArray),
				PKG_CompiledIn | 0x00000000,
				0xE2DC75F9,
				0x9DBC5510,
				METADATA_PARAMS(0, nullptr)
			};
			UECodeGen_Private::ConstructUPackage(Z_Registration_Info_UPackage__Script_RealityStream.OuterSingleton, PackageParams);
		}
		return Z_Registration_Info_UPackage__Script_RealityStream.OuterSingleton;
	}
	static FRegisterCompiledInInfo Z_CompiledInDeferPackage_UPackage__Script_RealityStream(Z_Construct_UPackage__Script_RealityStream, TEXT("/Script/RealityStream"), Z_Registration_Info_UPackage__Script_RealityStream, CONSTRUCT_RELOAD_VERSION_INFO(FPackageReloadVersionInfo, 0xE2DC75F9, 0x9DBC5510));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
