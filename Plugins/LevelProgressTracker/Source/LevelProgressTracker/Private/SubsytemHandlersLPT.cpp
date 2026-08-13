// Pavel Gornostaev <https://github.com/Pavreally>

#include "SubsytemLPT.h"
#include "Engine/Level.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceLevelStreaming.h"
#include "Streaming/LevelStreamingDelegates.h"

FName ULevelProgressTrackerSubsytem::GetLevelStateKey(const ULevelStreaming* StreamingLevel) const
{
	return StreamingLevel ? FName(*StreamingLevel->GetPathName()) : NAME_None;
}

void ULevelProgressTrackerSubsytem::OnLevelStreamingTargetStateChanged(
	UWorld* World,
	const ULevelStreaming* StreamingLevel,
	ULevel* LevelIfLoaded,
	ELevelStreamingState CurrentState,
	ELevelStreamingTargetState PreviousTarget,
	ELevelStreamingTargetState NewTarget)
{
	(void)LevelIfLoaded;
	(void)CurrentState;
	(void)PreviousTarget;

	if (bIsDeinitializing || bCreatingLPTStreamingLevel || !World || World != GetWorld() || !StreamingLevel)
	{
		return;
	}

	if (NewTarget == ELevelStreamingTargetState::Unloaded ||
		NewTarget == ELevelStreamingTargetState::UnloadedAndRemoved)
	{
		return;
	}

	if (ULevelStreamingLevelInstance* LevelInstanceStreaming = Cast<ULevelStreamingLevelInstance>(const_cast<ULevelStreaming*>(StreamingLevel)))
	{
		TrackExternalLevelInstance(LevelInstanceStreaming);
	}
}

void ULevelProgressTrackerSubsytem::OnLevelStreamingStateChanged(
	UWorld* World,
	const ULevelStreaming* StreamingLevel,
	ULevel* LevelIfLoaded,
	ELevelStreamingState PreviousState,
	ELevelStreamingState NewState)
{
	(void)LevelIfLoaded;
	(void)PreviousState;

	if (bIsDeinitializing || !World || World != GetWorld() || !StreamingLevel)
	{
		return;
	}

	ULevelStreamingLevelInstance* LevelInstanceStreaming = Cast<ULevelStreamingLevelInstance>(const_cast<ULevelStreaming*>(StreamingLevel));
	if (!LevelInstanceStreaming)
	{
		return;
	}

	if (NewState == ELevelStreamingState::Removed || NewState == ELevelStreamingState::FailedToLoad)
	{
		RemoveExternalLevelInstance(LevelInstanceStreaming);
	}
}

void ULevelProgressTrackerSubsytem::TrackExternalLevelInstance(ULevelStreamingLevelInstance* StreamingLevel)
{
	if (bIsDeinitializing || !IsValid(StreamingLevel) || !StreamingLevel->GetWorldAsset().ToSoftObjectPath().IsValid())
	{
		return;
	}

	FName StateKey = GetLevelStateKey(StreamingLevel);
	if (StateKey.IsNone())
	{
		return;
	}

	const TSoftObjectPtr<UWorld> LevelSoftPtr = StreamingLevel->GetWorldAsset();
	TSharedPtr<FLevelState> ExistingState;
	if (ILevelInstanceInterface* LevelInstance = StreamingLevel->GetLevelInstance())
	{
		AActor* LevelInstanceActor = Cast<AActor>(LevelInstance);
		if (LevelInstanceActor)
		{
			for (const TPair<FName, TSharedPtr<FLevelState>>& Level : LevelLoadedMap)
			{
				if (Level.Value.IsValid() && Level.Value->ExistingLevelInstanceActor.Get() == LevelInstanceActor)
				{
					StateKey = Level.Key;
					ExistingState = Level.Value;
					break;
				}
			}
		}
	}

	if (ExistingState.IsValid())
	{
		ExistingState->LevelInstanceState.LevelReference = StreamingLevel;
		ExistingState->bExternallyManaged = true;
		ExistingState->bLoadExistingLevelInstance = false;

		StreamingLevel->OnLevelShown.AddDynamic(
			this,
			&ULevelProgressTrackerSubsytem::OnLevelShown
		);

		if (StreamingLevel->GetLevelStreamingState() == ELevelStreamingState::LoadedVisible)
		{
			OnLevelShown();
		}
		return;
	}

	if (LevelLoadedMap.Contains(StateKey))
	{
		return;
	}

	TSharedRef<FLevelState> LevelState = MakeShared<FLevelState>();
	LevelState->LevelSoftPtr = LevelSoftPtr;
	LevelState->LevelName = FName(LevelSoftPtr.ToSoftObjectPath().GetAssetName());
	LevelState->LoadMethod = ELevelLoadMethod::LevelStreaming;
	LevelState->LevelInstanceState.LevelReference = StreamingLevel;
	LevelState->bExternallyManaged = true;
	if (ILevelInstanceInterface* LevelInstance = StreamingLevel->GetLevelInstance())
	{
		LevelState->ExistingLevelInstanceActor = Cast<AActor>(LevelInstance);
	}
	LevelState->LoadOptions = FLPTLoadOptions();

	LevelLoadedMap.Add(StateKey, LevelState);

	// The engine owns this streaming object. LPT only observes it and retains
	// preload handles; it must never create a second streaming level here.
	StreamingLevel->OnLevelShown.AddDynamic(
		this,
		&ULevelProgressTrackerSubsytem::OnLevelShown
	);

	StartPreloadingResources(
		StateKey,
		LevelSoftPtr,
		LevelState,
		true,
		LevelState->LoadOptions
	);

	// This also covers a reused streaming object that was already visible before
	// the observer saw its target-state notification.
	if (StreamingLevel->GetLevelStreamingState() == ELevelStreamingState::LoadedVisible)
	{
		OnLevelShown();
	}
}

void ULevelProgressTrackerSubsytem::RemoveExternalLevelInstance(ULevelStreamingLevelInstance* StreamingLevel)
{
	if (!StreamingLevel)
	{
		return;
	}

	const FName StateKey = GetLevelStateKey(StreamingLevel);
	TSharedPtr<FLevelState> LevelState = LevelLoadedMap.FindRef(StateKey);
	FName MatchedStateKey = StateKey;
	if (!LevelState.IsValid())
	{
		for (const TPair<FName, TSharedPtr<FLevelState>>& Level : LevelLoadedMap)
		{
			if (Level.Value.IsValid() && Level.Value->LevelInstanceState.LevelReference.Get() == StreamingLevel)
			{
				MatchedStateKey = Level.Key;
				LevelState = Level.Value;
				break;
			}
		}
	}
	if (!LevelState.IsValid() || !LevelState->bExternallyManaged)
	{
		return;
	}

	StreamingLevel->OnLevelShown.RemoveDynamic(
		this,
		&ULevelProgressTrackerSubsytem::OnLevelShown
	);
	ReleaseLevelStateHandles(LevelState.ToSharedRef(), true);
	LevelLoadedMap.Remove(MatchedStateKey);
}

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
	LevelState->bPreloadCompleted = true;

	// Ensure LoadedAssets equals TotalAssets for accurate 100% reporting
	LevelState->LoadedAssets = LevelState->TotalAssets;

	// Broadcast final progress and loaded events
	OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, 1.f, LevelState->LoadedAssets, LevelState->TotalAssets);

	if (LevelState->bExternallyManaged && LevelState->bLevelShown)
	{
		ReleaseLevelStateHandles(LevelState, false);
	}

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

		ULevelStreamingDynamic* StreamingLevel = LevelState.IsValid() ? LevelState->LevelInstanceState.LevelReference.Get() : nullptr;
		if (LevelState.IsValid() &&
				LevelState->LoadMethod == ELevelLoadMethod::LevelStreaming &&
				!LevelState->LevelInstanceState.IsLoaded &&
				IsValid(StreamingLevel) &&
				StreamingLevel->HasLoadedLevel() &&
				StreamingLevel->GetLoadedLevel() &&
				StreamingLevel->GetLoadedLevel()->bIsVisible)
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

		ULevelStreamingDynamic* StreamingLevel = LevelState->LevelInstanceState.LevelReference.Get();
		if (!IsValid(StreamingLevel))
		{
			continue;
		}

		// Unsubscribe the level display delegate
		StreamingLevel->OnLevelShown.RemoveDynamic(
				this,
				&ULevelProgressTrackerSubsytem::OnLevelShown
			);

		// Mark as loaded
		LevelState->LevelInstanceState.IsLoaded = true;
		LevelState->bLevelShown = true;

		if (!LevelState->bLoadedNotificationSent)
		{
			LevelState->bLoadedNotificationSent = true;
			OnLevelLoadedLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName);
		}

		// An externally managed Level Instance can become visible before LPT's
		// optional preload finishes. Keep those handles alive until completion.
		if (LevelState->bPreloadCompleted || !LevelState->bExternallyManaged)
		{
			ReleaseLevelStateHandles(LevelState.ToSharedRef(), false);
		}
	}
}

