// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerSubsytem.h"
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

		if (LevelState->Handle.IsValid())
		{
			LevelState->Handle->ReleaseHandle();
			LevelState->Handle.Reset();
			LevelName = LevelState->LevelName;
		}

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
			if (LevelState->Handle.IsValid())
			{
				LevelState->Handle->ReleaseHandle();
				LevelState->Handle.Reset();
			}

			if (LevelState->LevelInstanceState.LevelReference)
			{
				// Unloading streaming level
				LevelState->LevelInstanceState.LevelReference->SetIsRequestingUnloadAndRemoval(true);
			}
		}
	}

	LevelLoadedMap.Empty();
}