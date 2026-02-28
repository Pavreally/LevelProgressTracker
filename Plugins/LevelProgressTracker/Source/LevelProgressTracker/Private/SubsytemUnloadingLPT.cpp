// Pavel Gornostaev <https://github.com/Pavreally>

#include "SubsytemLPT.h"
#include "Engine/StreamableManager.h"


void ULevelProgressTrackerSubsytem::UnloadLevelInstanceLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, FName& LevelName)
{
	if (LevelSoftPtr.IsNull() && LevelLoadedMap.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (UnloadLevelInstanceLPT): Level unload failed."));

		return;
	}

	FName PackagePath = FName(*LevelSoftPtr.ToSoftObjectPath().GetLongPackageName());

	if (TSharedPtr<FLevelState> LevelState = LevelLoadedMap.FindRef(PackagePath))
	{
		if (!LevelState->LevelInstanceState.LevelReference)
		{
			UE_LOG(LogTemp, Warning, TEXT("LPT (UnloadLevelInstanceLPT): Invalid link. Failed to load stream level \"%s\"."), *LevelState->LevelName.ToString());

			return;
		}

		// Unloading streaming level
		LevelState->LevelInstanceState.LevelReference->SetIsRequestingUnloadAndRemoval(true);

		ReleaseLevelStateHandles(LevelState.ToSharedRef(), false);
		LevelName = LevelState->LevelName;

		LevelLoadedMap.Remove(PackagePath);
	}
}

void ULevelProgressTrackerSubsytem::UnloadAllLevelInstanceLPT()
{
	if (LevelLoadedMap.IsEmpty())
		return;

	for (TPair<FName, TSharedPtr<FLevelState>>& Level : LevelLoadedMap)
	{
		TSharedPtr<FLevelState>& LevelState = Level.Value;

		if (LevelState->LoadMethod == ELevelLoadMethod::LevelStreaming)
		{
			ReleaseLevelStateHandles(LevelState.ToSharedRef(), true);

			if (LevelState->LevelInstanceState.LevelReference)
			{
				ULevelStreamingDynamic* StreamingLevel = LevelState->LevelInstanceState.LevelReference;
				StreamingLevel->SetShouldBeVisible(false);
				StreamingLevel->SetShouldBeLoaded(false);
				// Unloading streaming level
				StreamingLevel->SetIsRequestingUnloadAndRemoval(true);
			}
		}
	}

	LevelLoadedMap.Empty();
}
