// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerSubsytem.h"
#include "Engine/Level.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"


void ULevelProgressTrackerSubsytem::GetFilteredWhiteList(TArray<FName>& WhiteListDir, TArray<FName>& Dependencies, TArray<FSoftObjectPath> &Paths)
{
	TArray<FString> WhiteListDirToString;

	WhiteListDirToString.Reserve(WhiteListDir.Num());
	for (const FName& Dir : WhiteListDir)
	{
		WhiteListDirToString.Add(Dir.ToString());
	}
	// Whitelist filtering
	if (WhiteListDir.IsEmpty())
	{
		for (const FName& Dependence : Dependencies)
		{
			Paths.Add(FSoftObjectPath(Dependence.ToString()));
		}
	}
	else
	{
		for (const FName& Dependence : Dependencies)
		{
			FString DependencePath = Dependence.ToString();
			// Selecting dependencies by whitelist
			for (const FString& Keyword : WhiteListDirToString)
			{
				if (!Keyword.IsEmpty() && DependencePath.Contains(Keyword))
				{
					// Asserting dependencies for loading
					Paths.Add(FSoftObjectPath(DependencePath));

					break;
				}
			}
		}
	}
}

void ULevelProgressTrackerSubsytem::HandleAssetLoaded(TSharedRef<FStreamableHandle> Handle, FName PackagePath, TSharedRef<FLevelState> LevelState)
{
	LevelState->LoadedAssets = FMath::Clamp(LevelState->LoadedAssets + 1, 0, LevelState->TotalAssets);

	// Check if there are no loaded assets, then display the loading progress
	float Progress = LevelState->TotalAssets > 0 ? (float)LevelState->LoadedAssets / (float)LevelState->TotalAssets : 1.f;

	OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, Progress, LevelState->LoadedAssets, LevelState->TotalAssets);
}

bool ULevelProgressTrackerSubsytem::CheckWorldPartition(const TSoftObjectPtr<UWorld>& LevelSoftPtr, IAssetRegistry& Registry)
{
	FAssetData AssetData = Registry.GetAssetByObjectPath(
		LevelSoftPtr.ToSoftObjectPath(),
		false,
		false
	);

	if (AssetData.IsValid())
	{
		FAssetTagValueRef PartitioneValue = AssetData.TagsAndValues.FindTag(TEXT("LevelIsPartitioned"));

		if (PartitioneValue.Equals(TEXT("1")))
		{
			return true;
		}
	}

	return false;
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
