// Pavel Gornostaev <https://github.com/Pavreally>

#include "SubsytemLPT.h"
#include "LevelPreloadDatabase.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "Kismet/GameplayStatics.h"


void ULevelProgressTrackerSubsytem::OpenLevelLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, bool PreloadingResources)
{
	if (LevelSoftPtr.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (OpenLevelLPT): Invalid level pointer."));

		return;
	}

	// Preloads the target level's resources, waits for all resources to load, and starts the level itself.
	AsyncLoadAssetsLPT(LevelSoftPtr, PreloadingResources);
}

void ULevelProgressTrackerSubsytem::LoadLevelInstanceLPT(TSoftObjectPtr<UWorld> LevelSoftPtr, const FTransform Transform, TSubclassOf<ULevelStreamingDynamic> OptionalLevelStreamingClass, bool bLoadAsTempPackage, bool PreloadingResources)
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
	AsyncLoadAssetsLPT(LevelSoftPtr, PreloadingResources, true, LevelInstanceState);
}

void ULevelProgressTrackerSubsytem::AsyncLoadAssetsLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, bool PreloadingResources, bool bIsStreamingLevel, FLevelInstanceState LevelInstanceState)
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

	if (bIsStreamingLevel)
	{
		LevelState->LoadMethod = ELevelLoadMethod::LevelStreaming;
	}

	LevelLoadedMap.Add(PackagePath, LevelState);

	if (PreloadingResources)
	{
		StartPreloadingResources(PackagePath, LevelSoftPtr, LevelState, bIsStreamingLevel);
	}
	else
	{
		StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
	}
}

void ULevelProgressTrackerSubsytem::StartPreloadingResources(FName PackagePath, const TSoftObjectPtr<UWorld>& LevelSoftPtr, TSharedRef<FLevelState>& LevelState, bool bIsStreamingLevel)
{
	ULevelPreloadDatabase* PreloadDatabase = PreloadDatabaseAsset.LoadSynchronous();
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

	const FLevelPreloadEntry* LevelEntry = PreloadDatabase->FindEntryByLevel(LevelSoftPtr);
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

	LevelState->bUseChunkedPreload = LevelEntry->Rules.bUseChunkedPreload;
	LevelState->PreloadChunkSize = FMath::Max(1, LevelEntry->Rules.PreloadChunkSize);

	TArray<FSoftObjectPath> Paths;
	TSet<FSoftObjectPath> UniquePaths;
	Paths.Reserve(LevelEntry->Assets.Num());

	for (const FSoftObjectPath& AssetPath : LevelEntry->Assets)
	{
		if (!AssetPath.IsValid() || UniquePaths.Contains(AssetPath))
		{
			continue;
		}

		UniquePaths.Add(AssetPath);
		Paths.Add(AssetPath);
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

