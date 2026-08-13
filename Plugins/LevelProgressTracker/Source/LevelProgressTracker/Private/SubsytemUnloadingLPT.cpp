// Pavel Gornostaev <https://github.com/Pavreally>

#include "SubsytemLPT.h"
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

		ULevelStreamingDynamic* StreamingLevel = LevelState->LevelInstanceState.LevelReference.Get();
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
		ReleaseLevelStateHandles(LevelState.ToSharedRef(), false);
		LevelName = LevelState->LevelName;

		if (LevelState->bExternallyManaged)
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

void ULevelProgressTrackerSubsytem::UnloadExistingLevelInstanceLPT(AActor* LevelInstanceActor)
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

		if (ULevelStreamingDynamic* StreamingLevel = LevelState->LevelInstanceState.LevelReference.Get())
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

		ULevelStreamingDynamic* StreamingLevel = LevelState->LevelInstanceState.LevelReference.Get();
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
			if (LevelState->bExternallyManaged)
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

	LevelLoadedMap.Empty();
}
