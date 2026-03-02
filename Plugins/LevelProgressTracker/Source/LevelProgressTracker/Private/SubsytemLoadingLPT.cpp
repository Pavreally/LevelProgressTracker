// Pavel Gornostaev <https://github.com/Pavreally>

#include "SubsytemLPT.h"
#include "LevelPreloadDatabaseLPT.h"
#include "AssetCollectionDataLPT.h"
#include "AssetFilterSettingsLPT.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const FName DefaultCollectionKey(TEXT("Default"));

	void SelectCollectionsForLoad(
		const FLevelPreloadEntryLPT& LevelEntry,
		const FLPTLoadOptions& LoadOptions,
		TArray<UAssetCollectionDataLPT*>& OutSelectedCollections)
	{
		OutSelectedCollections.Reset();

		TSet<FSoftObjectPath> UniqueCollectionPaths;
		TSet<FName> RequestedCollectionKeys;
		RequestedCollectionKeys.Reserve(LoadOptions.CollectionKeys.Num());
		for (const FName RequestedKey : LoadOptions.CollectionKeys)
		{
			if (!RequestedKey.IsNone())
			{
				RequestedCollectionKeys.Add(RequestedKey);
			}
		}

		const bool bUseCollectionKeySelection = RequestedCollectionKeys.Num() > 0;
		const bool bUseGroupTagSelection = !bUseCollectionKeySelection && !LoadOptions.GroupTags.IsEmpty();

		for (const TSoftObjectPtr<UAssetCollectionDataLPT>& CollectionRef : LevelEntry.Collections)
		{
			const FSoftObjectPath CollectionPath = CollectionRef.ToSoftObjectPath();
			if (!CollectionPath.IsValid() || UniqueCollectionPaths.Contains(CollectionPath))
			{
				continue;
			}

			UAssetCollectionDataLPT* CollectionAsset = CollectionRef.LoadSynchronous();
			if (!CollectionAsset)
			{
				continue;
			}

			bool bShouldUseCollection = false;
			if (bUseCollectionKeySelection)
			{
				bShouldUseCollection = RequestedCollectionKeys.Contains(CollectionAsset->CollectionKey);
			}
			else if (bUseGroupTagSelection)
			{
				bShouldUseCollection = CollectionAsset->GroupTags.HasAny(LoadOptions.GroupTags);
			}
			else
			{
				bShouldUseCollection = CollectionAsset->CollectionKey == DefaultCollectionKey;
			}

			if (!bShouldUseCollection)
			{
				continue;
			}

			UniqueCollectionPaths.Add(CollectionPath);
			OutSelectedCollections.Add(CollectionAsset);
		}
	}

	void MergeCollectionAssetLists(
		const TArray<UAssetCollectionDataLPT*>& Collections,
		TArray<FSoftObjectPath>& OutMergedPaths)
	{
		OutMergedPaths.Reset();

		TSet<FSoftObjectPath> UniquePaths;
		for (const UAssetCollectionDataLPT* CollectionAsset : Collections)
		{
			if (!CollectionAsset)
			{
				continue;
			}

			for (const FSoftObjectPath& AssetPath : CollectionAsset->AssetList)
			{
				if (!AssetPath.IsValid() || UniquePaths.Contains(AssetPath))
				{
					continue;
				}

				UniquePaths.Add(AssetPath);
				OutMergedPaths.Add(AssetPath);
			}
		}
	}
}

void ULevelProgressTrackerSubsytem::OpenLevelLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, bool PreloadingResources)
{
	OpenLevelLPT(LevelSoftPtr, PreloadingResources, FLPTLoadOptions());
}

void ULevelProgressTrackerSubsytem::OpenLevelLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, bool PreloadingResources, const FLPTLoadOptions& LoadOptions)
{
	if (LevelSoftPtr.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (OpenLevelLPT): Invalid level pointer."));

		return;
	}

	// Preloads the target level's resources, waits for all resources to load, and starts the level itself.
	AsyncLoadAssetsLPT(LevelSoftPtr, PreloadingResources, false, FLevelInstanceState(), LoadOptions);
}

void ULevelProgressTrackerSubsytem::LoadLevelInstanceLPT(TSoftObjectPtr<UWorld> LevelSoftPtr, const FTransform Transform, TSubclassOf<ULevelStreamingDynamic> OptionalLevelStreamingClass, bool bLoadAsTempPackage, bool PreloadingResources)
{
	LoadLevelInstanceLPT(LevelSoftPtr, Transform, OptionalLevelStreamingClass, bLoadAsTempPackage, PreloadingResources, FLPTLoadOptions());
}

void ULevelProgressTrackerSubsytem::LoadLevelInstanceLPT(TSoftObjectPtr<UWorld> LevelSoftPtr, const FTransform Transform, TSubclassOf<ULevelStreamingDynamic> OptionalLevelStreamingClass, bool bLoadAsTempPackage, bool PreloadingResources, const FLPTLoadOptions& LoadOptions)
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
	AsyncLoadAssetsLPT(LevelSoftPtr, PreloadingResources, true, LevelInstanceState, LoadOptions);
}

void ULevelProgressTrackerSubsytem::AsyncLoadAssetsLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, bool PreloadingResources, bool bIsStreamingLevel, FLevelInstanceState LevelInstanceState, const FLPTLoadOptions& LoadOptions)
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

	// Init load stat
	TSharedRef<FLevelState> LevelState = MakeShared<FLevelState>();
	LevelState->LevelSoftPtr = LevelSoftPtr;
	LevelState->LevelName = FName(TargetLevelName);
	LevelState->TotalAssets = 0;
	LevelState->LoadedAssets = 0;
	LevelState->LevelInstanceState = LevelInstanceState;
	LevelState->LoadOptions = LoadOptions;

	if (bIsStreamingLevel)
	{
		LevelState->LoadMethod = ELevelLoadMethod::LevelStreaming;
	}

	LevelLoadedMap.Add(PackagePath, LevelState);

	if (PreloadingResources)
	{
		StartPreloadingResources(PackagePath, LevelSoftPtr, LevelState, bIsStreamingLevel, LevelState->LoadOptions);
	}
	else
	{
		StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
	}
}

void ULevelProgressTrackerSubsytem::StartPreloadingResources(FName PackagePath, const TSoftObjectPtr<UWorld>& LevelSoftPtr, TSharedRef<FLevelState>& LevelState, bool bIsStreamingLevel, const FLPTLoadOptions& LoadOptions)
{
	ULevelPreloadDatabaseLPT* PreloadDatabase = PreloadDatabaseAsset.LoadSynchronous();
	if (!PreloadDatabase)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (StartPreloadingResources): Preload database '%s' is missing. Falling back to level-only loading for '%s'."),
			*PreloadDatabaseAsset.ToString(),
			*PackagePath.ToString()
		);

		LevelState->TotalAssets = 1;
		LevelState->LoadedAssets = 1;
		OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, 1.f, LevelState->LoadedAssets, LevelState->TotalAssets);
		StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
		return;
	}

	const FLevelPreloadEntryLPT* LevelEntry = PreloadDatabase->FindEntryByLevel(LevelSoftPtr);
	if (!LevelEntry)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (StartPreloadingResources): No preload entry found for level '%s'. Falling back to level-only loading."),
			*PackagePath.ToString()
		);

		LevelState->TotalAssets = 1;
		LevelState->LoadedAssets = 1;
		OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, 1.f, LevelState->LoadedAssets, LevelState->TotalAssets);
		StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
		return;
	}

	FLPTFilterSettings RuntimeFilterSettings;
	if (UAssetFilterSettingsLPT* FilterSettingsAsset = LevelEntry->FilterSettings.LoadSynchronous())
	{
		RuntimeFilterSettings = FilterSettingsAsset->ToFilterSettings();
	}
	LevelState->bUseChunkedPreload = RuntimeFilterSettings.bUseChunkedPreload;
	LevelState->PreloadChunkSize = FMath::Max(1, RuntimeFilterSettings.PreloadChunkSize);

	TArray<UAssetCollectionDataLPT*> SelectedCollections;
	SelectCollectionsForLoad(*LevelEntry, LoadOptions, SelectedCollections);

	TArray<FSoftObjectPath> Paths;
	MergeCollectionAssetLists(SelectedCollections, Paths);

	if (LoadOptions.CollectionKeys.Num() > 0 && SelectedCollections.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (StartPreloadingResources): Requested CollectionKeys not found for level '%s'. No preload assets selected."),
			*PackagePath.ToString());
	}
	else if (!LoadOptions.GroupTags.IsEmpty() && SelectedCollections.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (StartPreloadingResources): Requested GroupTags did not match any collection for level '%s'. No preload assets selected."),
			*PackagePath.ToString());
	}
	else if (LoadOptions.CollectionKeys.IsEmpty() && LoadOptions.GroupTags.IsEmpty() && SelectedCollections.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (StartPreloadingResources): Default collection '%s' not found for level '%s'. No preload assets selected."),
			*DefaultCollectionKey.ToString(),
			*PackagePath.ToString()
		);
	}

	// Setup load stat
	LevelState->TotalAssets = Paths.Num();
	LevelState->LoadedAssets = 0;
	LevelState->PreloadPaths.Reset();
	LevelState->NextPreloadPathIndex = 0;
	LevelState->ChunkHandles.Reset();

	if (Paths.IsEmpty())
	{
		OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, 1.f, 0, 0);
		StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
		return;
	}

	if (LevelState->bUseChunkedPreload)
	{
		LevelState->PreloadPaths = MoveTemp(Paths);
		LevelState->NextPreloadPathIndex = 0;
		StartNextPreloadChunk(PackagePath, bIsStreamingLevel, LevelState);
		return;
	}

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
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (StartPreloadingResources): Failed to create streamable handle for level '%s'."),
			*PackagePath.ToString()
		);

		LevelState->TotalAssets = 1;
		LevelState->LoadedAssets = 1;
		OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, 1.f, LevelState->LoadedAssets, LevelState->TotalAssets);
		StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
	}
}

void ULevelProgressTrackerSubsytem::StartNextPreloadChunk(FName PackagePath, bool bIsStreamingLevel, TSharedRef<FLevelState> LevelState)
{
	if (!LevelState->bUseChunkedPreload)
	{
		return;
	}

	if (LevelState->NextPreloadPathIndex >= LevelState->PreloadPaths.Num())
	{
		OnAllAssetsLoaded(PackagePath, bIsStreamingLevel, LevelState);
		return;
	}

	const int32 RemainingAssets = LevelState->PreloadPaths.Num() - LevelState->NextPreloadPathIndex;
	const int32 ChunkAssetCount = FMath::Clamp(LevelState->PreloadChunkSize, 1, RemainingAssets);
	const int32 ChunkBaseLoaded = LevelState->LoadedAssets;

	TArray<FSoftObjectPath> ChunkPaths;
	ChunkPaths.Reserve(ChunkAssetCount);

	for (int32 Index = 0; Index < ChunkAssetCount; ++Index)
	{
		ChunkPaths.Add(LevelState->PreloadPaths[LevelState->NextPreloadPathIndex + Index]);
	}

	LevelState->NextPreloadPathIndex += ChunkAssetCount;

	FStreamableManager& StreamableManager = UAssetManager::Get().GetStreamableManager();
	TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(
		ChunkPaths,
		FStreamableDelegate::CreateUObject(
			this,
			&ULevelProgressTrackerSubsytem::OnPreloadChunkLoaded,
			PackagePath,
			bIsStreamingLevel,
			LevelState,
			ChunkAssetCount),
		FStreamableManager::AsyncLoadHighPriority
	);

	if (!Handle.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (StartNextPreloadChunk): Failed to create chunk streamable handle for level '%s'."), *PackagePath.ToString());

		LevelState->LoadedAssets = FMath::Clamp(ChunkBaseLoaded + ChunkAssetCount, 0, LevelState->TotalAssets);
		const float Progress = LevelState->TotalAssets > 0 ? static_cast<float>(LevelState->LoadedAssets) / LevelState->TotalAssets : 1.f;
		OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, Progress, LevelState->LoadedAssets, LevelState->TotalAssets);

		StartNextPreloadChunk(PackagePath, bIsStreamingLevel, LevelState);
		return;
	}

	Handle->BindUpdateDelegate(FStreamableUpdateDelegate::CreateUObject(
		this,
		&ULevelProgressTrackerSubsytem::HandleChunkAssetLoaded,
		PackagePath,
		LevelState,
		ChunkBaseLoaded,
		ChunkAssetCount
	));

	LevelState->ChunkHandles.Add(Handle);
	LevelState->Handle = Handle;
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

