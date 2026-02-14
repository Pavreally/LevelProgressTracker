// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerSubsytem.h"
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
	// Ensure LoadedAssets equals TotalAssets for accurate 100% reporting
	LevelState->LoadedAssets = LevelState->TotalAssets;

	// Broadcast final progress and loaded events
	OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, 1.f, LevelState->LoadedAssets, LevelState->TotalAssets);

	StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
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

		// Release handle
		if (LevelState->Handle.IsValid())
		{
			LevelState->Handle->ReleaseHandle();
			LevelState->Handle.Reset();
		}

		// Mark as loaded
		LevelState->LevelInstanceState.IsLoaded = true;

		// Notification
		OnLevelLoadedLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName);
	}
}
