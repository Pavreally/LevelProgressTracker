// Pavel Gornostaev <https://github.com/Pavreally>

#include "SubsytemLPT.h"
#include "LevelPreloadDatabaseLPT.h"
#include "Engine/LevelStreaming.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceLevelStreaming.h"


void ULevelProgressTrackerSubsytem::UnloadLevelInstanceLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, FName& LevelName)
{
	if (bIsDeinitializing || LevelSoftPtr.IsNull() || LevelLoadedMap.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (UnloadLevelInstanceLPT): Level unload failed."));

		return;
	}

	const FString RequestedPackagePath = LevelSoftPtr.ToSoftObjectPath().GetLongPackageName();
	for (auto It = LevelLoadedMap.CreateIterator(); It; ++It)
	{
		TSharedPtr<FLevelState> LevelState = It.Value();
		if (!LevelState.IsValid() ||
			LevelState->LevelSoftPtr.ToSoftObjectPath().GetLongPackageName() != RequestedPackagePath)
		{
			continue;
		}

		ULevelStreaming* StreamingLevel = LevelState->GetStreamingLevelReference();
		if (!IsValid(StreamingLevel))
		{
			UE_LOG(LogTemp, Warning, TEXT("LPT (UnloadLevelInstanceLPT): Invalid link. Failed to load stream level \"%s\"."), *LevelState->LevelName.ToString());
			ReleaseLevelStateHandles(LevelState.ToSharedRef(), true);
			if (LevelState->bExternallyManaged)
			{
				if (AActor* LevelInstanceActor = LevelState->ExistingLevelInstanceActor.Get())
				{
					if (ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(LevelInstanceActor))
					{
						LevelInstance->UnloadLevelInstance();
					}
				}
			}
			It.RemoveCurrent();
			return;
		}

		StreamingLevel->OnLevelShown.RemoveDynamic(
			this,
			&ULevelProgressTrackerSubsytem::OnLevelShown
		);
		ReleaseLevelStateHandles(LevelState.ToSharedRef(), !LevelState->bPreloadCompleted);
		LevelName = LevelState->LevelName;

		if (LevelState->bLoadExistingSublevel)
		{
			bool bUnloadedThroughLevelInstance = false;
			if (ULevelStreamingLevelInstance* LevelInstanceStreaming = Cast<ULevelStreamingLevelInstance>(StreamingLevel))
			{
				if (ILevelInstanceInterface* LevelInstance = LevelInstanceStreaming->GetLevelInstance())
				{
					LevelInstance->UnloadLevelInstance();
					bUnloadedThroughLevelInstance = true;
				}
			}

			if (!bUnloadedThroughLevelInstance)
			{
				StreamingLevel->SetShouldBeVisible(false);
				StreamingLevel->SetShouldBeLoaded(false);
			}
		}
		else if (LevelState->bExternallyManaged)
		{
			if (ULevelStreamingLevelInstance* LevelInstanceStreaming = Cast<ULevelStreamingLevelInstance>(StreamingLevel))
			{
				if (ILevelInstanceInterface* LevelInstance = LevelInstanceStreaming->GetLevelInstance())
				{
					LevelInstance->UnloadLevelInstance();
				}
			}
		}
		else
		{
			StreamingLevel->SetIsRequestingUnloadAndRemoval(true);
		}

		It.RemoveCurrent();
		return;
	}
}

void ULevelProgressTrackerSubsytem::UnloadSubLevelLPT(FName CollectionKey)
{
	if (bIsDeinitializing)
	{
		return;
	}

	const FLevelPreloadEntryLPT* ParentEntry = nullptr;
	UAssetCollectionDataLPT* Collection = nullptr;
	ULevelStreaming* StreamingLevel = nullptr;
	if (!ResolveSublevelCollection(
		GetWorld(),
		PreloadDatabaseAsset.LoadSynchronous(),
		CollectionKey,
		TEXT("UnloadSubLevelLPT"),
		ParentEntry,
		Collection,
		StreamingLevel))
	{
		return;
	}

	(void)ParentEntry;
	(void)Collection;

	const FName StateKey = GetLevelStateKey(StreamingLevel);
	if (TSharedPtr<FLevelState> LevelState = LevelLoadedMap.FindRef(StateKey))
	{
		StreamingLevel->OnLevelShown.RemoveDynamic(
			this,
			&ULevelProgressTrackerSubsytem::OnLevelShown
		);
		ReleaseLevelStateHandles(LevelState.ToSharedRef(), !LevelState->bPreloadCompleted);
		LevelLoadedMap.Remove(StateKey);
	}

	if (ULevelStreamingLevelInstance* LevelInstanceStreaming = Cast<ULevelStreamingLevelInstance>(StreamingLevel))
	{
		if (ILevelInstanceInterface* LevelInstance = LevelInstanceStreaming->GetLevelInstance())
		{
			LevelInstance->UnloadLevelInstance();
			return;
		}
	}

	// A regular sublevel stays configured in the parent world; only request its unload.
	StreamingLevel->SetShouldBeVisible(false);
	StreamingLevel->SetShouldBeLoaded(false);
}

void ULevelProgressTrackerSubsytem::UnloadPlacedLevelInstanceLPT(AActor* LevelInstanceActor)
{
	if (!IsValid(LevelInstanceActor))
	{
		return;
	}

	for (auto It = LevelLoadedMap.CreateIterator(); It; ++It)
	{
		TSharedPtr<FLevelState> LevelState = It.Value();
		if (!LevelState.IsValid() || LevelState->ExistingLevelInstanceActor.Get() != LevelInstanceActor)
		{
			continue;
		}

		if (ULevelStreaming* StreamingLevel = LevelState->GetStreamingLevelReference())
		{
			if (IsValid(StreamingLevel))
			{
				StreamingLevel->OnLevelShown.RemoveDynamic(
					this,
					&ULevelProgressTrackerSubsytem::OnLevelShown
				);
			}
		}

		ReleaseLevelStateHandles(LevelState.ToSharedRef(), true);
		if (ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(LevelInstanceActor))
		{
			LevelInstance->UnloadLevelInstance();
		}

		It.RemoveCurrent();
		return;
	}

	if (ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(LevelInstanceActor))
	{
		LevelInstance->UnloadLevelInstance();
	}
}

void ULevelProgressTrackerSubsytem::UnloadAllLevelInstanceLPT()
{
	if (LevelLoadedMap.IsEmpty())
		return;

	for (TPair<FName, TSharedPtr<FLevelState>>& Level : LevelLoadedMap)
	{
		TSharedPtr<FLevelState>& LevelState = Level.Value;
		if (!LevelState.IsValid())
		{
			continue;
		}

		// Handles must be released for every state, including a standard map load.
		ReleaseLevelStateHandles(LevelState.ToSharedRef(), true);

		if (bIsDeinitializing)
		{
			continue;
		}

		ULevelStreaming* StreamingLevel = LevelState->GetStreamingLevelReference();
		if (!IsValid(StreamingLevel))
		{
			continue;
		}

		StreamingLevel->OnLevelShown.RemoveDynamic(
			this,
			&ULevelProgressTrackerSubsytem::OnLevelShown
		);

		if (LevelState->LoadMethod == ELevelLoadMethod::LevelStreaming)
		{
			if (LevelState->bLoadExistingSublevel)
			{
				bool bUnloadedThroughLevelInstance = false;
				if (ULevelStreamingLevelInstance* LevelInstanceStreaming = Cast<ULevelStreamingLevelInstance>(StreamingLevel))
				{
					if (ILevelInstanceInterface* LevelInstance = LevelInstanceStreaming->GetLevelInstance())
					{
						LevelInstance->UnloadLevelInstance();
						bUnloadedThroughLevelInstance = true;
					}
				}

				if (!bUnloadedThroughLevelInstance)
				{
					StreamingLevel->SetShouldBeVisible(false);
					StreamingLevel->SetShouldBeLoaded(false);
				}
			}
			else if (LevelState->bExternallyManaged)
			{
				if (ULevelStreamingLevelInstance* LevelInstanceStreaming = Cast<ULevelStreamingLevelInstance>(StreamingLevel))
				{
					if (ILevelInstanceInterface* LevelInstance = LevelInstanceStreaming->GetLevelInstance())
					{
						LevelInstance->UnloadLevelInstance();
					}
				}
			}
			else
			{
				StreamingLevel->SetShouldBeVisible(false);
				StreamingLevel->SetShouldBeLoaded(false);
				StreamingLevel->SetIsRequestingUnloadAndRemoval(true);
			}
		}
	}

	// A placed Level Instance may still be waiting for UE to create its streaming object.
	TArray<TWeakObjectPtr<AActor>> PendingLevelInstanceActors;
	for (const TPair<FName, TSharedPtr<FLevelState>>& Level : LevelLoadedMap)
	{
		if (!Level.Value.IsValid() || !Level.Value->bLoadPlacedLevelInstance)
		{
			continue;
		}

		if (AActor* LevelInstanceActor = Level.Value->ExistingLevelInstanceActor.Get())
		{
			PendingLevelInstanceActors.Add(LevelInstanceActor);
		}
	}

	for (const TWeakObjectPtr<AActor>& WeakActor : PendingLevelInstanceActors)
	{
		if (AActor* LevelInstanceActor = WeakActor.Get())
		{
			if (ILevelInstanceInterface* LevelInstance = Cast<ILevelInstanceInterface>(LevelInstanceActor))
			{
				LevelInstance->UnloadLevelInstance();
			}
		}
	}

	LevelLoadedMap.Empty();
}
