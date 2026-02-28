// Pavel Gornostaev <https://github.com/Pavreally>

#include "SubsytemLPT.h"
#include "Engine/Level.h"
#include "Engine/StreamableManager.h"

void ULevelProgressTrackerSubsytem::HandleAssetLoaded(TSharedRef<FStreamableHandle> Handle, FName PackagePath, TSharedRef<FLevelState> LevelState)
{
	(void)PackagePath;

	const float Progress = FMath::Clamp(Handle->GetProgress(), 0.f, 1.f);
	LevelState->LoadedAssets = LevelState->TotalAssets > 0
		? FMath::Clamp(FMath::RoundToInt(Progress * LevelState->TotalAssets), 0, LevelState->TotalAssets)
		: 0;

	OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, Progress, LevelState->LoadedAssets, LevelState->TotalAssets);
}

void ULevelProgressTrackerSubsytem::OnAllAssetsLoaded(FName PackagePath, bool bIsStreamingLevel, TSharedRef<FLevelState> LevelState)
{
	LevelState->PreloadPaths.Reset();
	LevelState->NextPreloadPathIndex = 0;

	// Ensure LoadedAssets equals TotalAssets for accurate 100% reporting
	LevelState->LoadedAssets = LevelState->TotalAssets;

	// Broadcast final progress and loaded events
	OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, 1.f, LevelState->LoadedAssets, LevelState->TotalAssets);

	StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
}

void ULevelProgressTrackerSubsytem::ReleaseLevelStateHandles(TSharedRef<FLevelState> LevelState, bool bCancelHandles)
{
	TSet<FStreamableHandle*> ReleasedHandles;
	ReleasedHandles.Reserve(LevelState->ChunkHandles.Num() + 1);

	auto ReleaseOneHandle = [&ReleasedHandles, bCancelHandles](TSharedPtr<FStreamableHandle>& HandleToRelease)
	{
		if (!HandleToRelease.IsValid())
		{
			return;
		}

		FStreamableHandle* RawHandle = HandleToRelease.Get();
		if (!ReleasedHandles.Contains(RawHandle))
		{
			if (bCancelHandles)
			{
				HandleToRelease->CancelHandle();
			}

			HandleToRelease->ReleaseHandle();
			ReleasedHandles.Add(RawHandle);
		}

		HandleToRelease.Reset();
	};

	ReleaseOneHandle(LevelState->Handle);

	for (TSharedPtr<FStreamableHandle>& ChunkHandle : LevelState->ChunkHandles)
	{
		ReleaseOneHandle(ChunkHandle);
	}

	LevelState->ChunkHandles.Reset();
}

void ULevelProgressTrackerSubsytem::OnPreloadChunkLoaded(FName PackagePath, bool bIsStreamingLevel, TSharedRef<FLevelState> LevelState, int32 LoadedChunkAssetCount)
{
	(void)LoadedChunkAssetCount;

	LevelState->LoadedAssets = FMath::Clamp(LevelState->NextPreloadPathIndex, 0, LevelState->TotalAssets);

	const float Progress = LevelState->TotalAssets > 0
		? static_cast<float>(LevelState->LoadedAssets) / LevelState->TotalAssets
		: 1.f;

	OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, Progress, LevelState->LoadedAssets, LevelState->TotalAssets);

	StartNextPreloadChunk(PackagePath, bIsStreamingLevel, LevelState);
}

void ULevelProgressTrackerSubsytem::HandleChunkAssetLoaded(TSharedRef<FStreamableHandle> Handle, FName PackagePath, TSharedRef<FLevelState> LevelState, int32 ChunkBaseLoaded, int32 ChunkAssetCount)
{
	(void)PackagePath;

	const float ChunkProgress = FMath::Clamp(Handle->GetProgress(), 0.f, 1.f);
	const int32 LoadedInChunk = FMath::Clamp(FMath::RoundToInt(ChunkProgress * ChunkAssetCount), 0, ChunkAssetCount);
	LevelState->LoadedAssets = FMath::Clamp(ChunkBaseLoaded + LoadedInChunk, 0, LevelState->TotalAssets);

	const float TotalProgress = LevelState->TotalAssets > 0
		? static_cast<float>(LevelState->LoadedAssets) / LevelState->TotalAssets
		: 0.f;

	OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, TotalProgress, LevelState->LoadedAssets, LevelState->TotalAssets);
}

void ULevelProgressTrackerSubsytem::OnLevelShown()
{
	// Collecting a list of packages ready for removal
	TArray<FName> PackagesToRemove;
	PackagesToRemove.Reserve(LevelLoadedMap.Num());

	// Initial check of the state of each streaming level
	for (TPair<FName, TSharedPtr<FLevelState>>& Level : LevelLoadedMap)
	{
		const FName& PackageName = Level.Key;
		TSharedPtr<FLevelState>& LevelState = Level.Value;

		if (LevelState->LoadMethod == ELevelLoadMethod::LevelStreaming &&
				!LevelState->LevelInstanceState.IsLoaded &&
				LevelState->LevelInstanceState.LevelReference &&
				LevelState->LevelInstanceState.LevelReference->HasLoadedLevel() &&
				LevelState->LevelInstanceState.LevelReference->GetLoadedLevel()->bIsVisible)
		{
			// Mark for cleanup after iteration
			PackagesToRemove.Add(PackageName);
		}
	}
		
	// Process marked packets outside the iterator
	for (const FName& PackageName : PackagesToRemove)
	{
		TSharedPtr<FLevelState> LevelState = LevelLoadedMap.FindRef(PackageName);
		if (!LevelState.IsValid())
		{
			continue;
		}

		// Unsubscribe the level display delegate
		LevelState->LevelInstanceState.LevelReference->OnLevelShown.RemoveDynamic(
				this,
				&ULevelProgressTrackerSubsytem::OnLevelShown
			);

		// Release preload handles
		ReleaseLevelStateHandles(LevelState.ToSharedRef(), false);

		// Mark as loaded
		LevelState->LevelInstanceState.IsLoaded = true;

		// Notification
		OnLevelLoadedLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName);
	}
}

