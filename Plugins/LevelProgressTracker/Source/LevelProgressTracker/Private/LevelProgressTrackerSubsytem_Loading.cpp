// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerSubsytem.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet/GameplayStatics.h"


void ULevelProgressTrackerSubsytem::OpenLevelLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, TArray<FName> WhiteListDir, bool PreloadingResources)
{
	if (LevelSoftPtr.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (OpenLevelLPT): Invalid level pointer."));

		return;
	}

	// Preloads the target level's resources, waits for all resources to load, and starts the level itself.
	AsyncLoadAssetsLPT(LevelSoftPtr, WhiteListDir, PreloadingResources);
}

void ULevelProgressTrackerSubsytem::LoadLevelInstanceLPT(TSoftObjectPtr<UWorld> LevelSoftPtr, TArray<FName> WhiteListDir, const FTransform Transform, TSubclassOf<ULevelStreamingDynamic> OptionalLevelStreamingClass, bool bLoadAsTempPackage, bool PreloadingResources)
{
	if (LevelSoftPtr.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (LoadLevelInstanceLPT): Invalid level pointer."));

		return;
	}

	FLevelInstanceState LevelInstanceState;
	LevelInstanceState.Transform = Transform;
	LevelInstanceState.OptionalLevelStreamingClass = OptionalLevelStreamingClass;
	LevelInstanceState.bLoadAsTempPackage = bLoadAsTempPackage;

	// Preloads the target level's resources, waits for all resources to load, and starts the level itself.
	AsyncLoadAssetsLPT(LevelSoftPtr, WhiteListDir, PreloadingResources, true, LevelInstanceState);
}

void ULevelProgressTrackerSubsytem::AsyncLoadAssetsLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, TArray<FName>& WhiteListDir, bool PreloadingResources, bool bIsStreamingLevel, FLevelInstanceState LevelInstanceState)
{
	if (LevelSoftPtr.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (AsyncLoadAssetsLPT): Invalid level pointer."));

		return;
	}

	FName PackagePath = FName(*LevelSoftPtr.ToSoftObjectPath().GetLongPackageName());
	FString TargetLevelName = LevelSoftPtr.ToSoftObjectPath().GetAssetName();

	if (LevelLoadedMap.Contains(PackagePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (AsyncLoadAssetsLPT): The requested level \"%s\" is currently loading or has loaded."), *TargetLevelName);

		return;
	}
	
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = RegistryModule.Get();

	// Init load stat
	TSharedRef<FLevelState> LevelState = MakeShared<FLevelState>();
	LevelState->LevelSoftPtr = LevelSoftPtr;
	LevelState->LevelName = FName(TargetLevelName);
	LevelState->TotalAssets = 0;
	LevelState->LoadedAssets = 0;
	LevelState->LevelInstanceState = LevelInstanceState;

	// Determining the level type (World Partition)
	if (!bIsStreamingLevel && CheckWorldPartition(LevelSoftPtr, Registry))
	{
		LevelState->LoadMethod = ELevelLoadMethod::WorldPartition;
	}
	else if (bIsStreamingLevel)
	{
		LevelState->LoadMethod = ELevelLoadMethod::LevelStreaming;
	}

	if (PreloadingResources)
	{
		StartPreloadingResources(Registry, PackagePath, WhiteListDir, LevelState, bIsStreamingLevel);
	}
	else
	{
		StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
	}

	LevelLoadedMap.Add(PackagePath, LevelState);
}

void ULevelProgressTrackerSubsytem::StartPreloadingResources(IAssetRegistry& Registry, FName& PackagePath, TArray<FName>& WhiteListDir, TSharedRef<FLevelState>& LevelState, bool& bIsStreamingLevel)
{
	// Gather dependencies
	TArray<FName> Dependencies;

	Registry.GetDependencies(
		PackagePath,
		Dependencies,
		UE::AssetRegistry::EDependencyCategory::Package,
		UE::AssetRegistry::FDependencyQuery()
	);

	if (Dependencies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (AsyncLoadAssetsLPT): No assets found to load for level '%s'."), *PackagePath.ToString());
		StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
		
		return;
	}

	// Softlink conversion and optional whitelist filtering
	TArray<FSoftObjectPath> Paths;
	GetFilteredWhiteList(WhiteListDir, Dependencies, Paths);

	// Setup load stat
	LevelState->TotalAssets = Paths.Num();

	// Request for async resource loading
	FStreamableManager& StreamableManager = UAssetManager::Get().GetStreamableManager();
	TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(
		Paths,
		FStreamableDelegate::CreateUObject(
			this,
			&ULevelProgressTrackerSubsytem::OnAllAssetsLoaded,
			PackagePath,
			bIsStreamingLevel,
			LevelState),
		FStreamableManager::AsyncLoadHighPriority
	);

	if (Handle.IsValid())
	{
		Handle->BindUpdateDelegate(FStreamableUpdateDelegate::CreateUObject(
			this,
			&ULevelProgressTrackerSubsytem::HandleAssetLoaded,
			PackagePath,
			LevelState
		));
		LevelState->Handle = Handle;
	}
}

void ULevelProgressTrackerSubsytem::StartLevelLPT(FName PackagePath, bool bIsStreamingLevel, TSharedRef<FLevelState> LevelState)
{
	if (bIsStreamingLevel)
	{
		// Load Level Instance
		bool bOutSuccess = false;
		const FString OptionalLevelNameOverride = TEXT("");

		ULevelStreamingDynamic* StreamingLevel = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
			this,
			LevelState->LevelSoftPtr,
			LevelState->LevelInstanceState.Transform,
			bOutSuccess,
			OptionalLevelNameOverride,
			LevelState->LevelInstanceState.OptionalLevelStreamingClass,
			LevelState->LevelInstanceState.bLoadAsTempPackage
		);

		if (StreamingLevel)
		{
			LevelState->LevelInstanceState.LevelReference = StreamingLevel;

			// Subscribe to the event when the streaming level is fully opened and loaded
			StreamingLevel->OnLevelShown.AddDynamic(
				this,
				&ULevelProgressTrackerSubsytem::OnLevelShown
			);
		}
	}
	else
	{
		// Open Level
		UGameplayStatics::OpenLevel(this, PackagePath);
	}
}
