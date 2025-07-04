// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerSubsytem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StreamableManager.h"
#include "SLoadingWidgetWrapLPT.h"


#pragma region SUBSYSTEM
void ULevelProgressTrackerSubsytem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subscribe to be notified when the global level load is complete
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this,
		&ULevelProgressTrackerSubsytem::OnPostLoadMapWithWorld
	);
}

void ULevelProgressTrackerSubsytem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	// Clearing delegates
	OnLevelLoadProgressLPT.Clear();
	OnLevelLoadedLPT.Clear();

	RemoveLoadingScreenLPT();
	UnloadAllLevelInstanceLPT();

	Super::Deinitialize();
}

#pragma endregion SUBSYSTEM

void ULevelProgressTrackerSubsytem::OnPostLoadMapWithWorld(UWorld* LoadedWorld)
{
	if (LoadedWorld && LoadedWorld == GetWorld())
	{
		FName PackageName = FName(*LoadedWorld->GetOutermost()->GetName());
		TSharedPtr<FLevelState> LevelState = LevelLoadedMap.FindRef(PackageName);

		// Reset handler if level is not streaming
		if (LevelState && LevelState->LoadMethod != ELevelLoadMethod::LevelStreaming)
		{
			if (LevelState->Handle.IsValid())
			{
				// Releasing the resource level handler and finishing tracking him
				LevelState->Handle->ReleaseHandle();
				LevelState->Handle.Reset();
			}
			// Streaming level loading notification
			OnLevelLoadedLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName);
			// Clear memory from unnecessary data
			LevelLoadedMap.Remove(PackageName);
		}
	}
}

void ULevelProgressTrackerSubsytem::CreateLoadingScreenLPT(TSoftClassPtr<UUserWidget> WidgetClass, int32 ZOrder)
{
	SLoadingWidgetWrap = SNew(SLoadingWidgetWrapLPT);
	// Add Slate widget to viewort
	if (GEngine)
	{
		GEngine->GameViewport->AddViewportWidgetContent(SLoadingWidgetWrap.ToSharedRef(), ZOrder);
	}
	// Add UMG to Slate widget
	if (SLoadingWidgetWrap.IsValid())
	{
		SLoadingWidgetWrap->LoadEmbeddedUWidgetLPT(WidgetClass);
	}
}

void ULevelProgressTrackerSubsytem::RemoveLoadingScreenLPT()
{
	if (SLoadingWidgetWrap.IsValid())
	{
		SLoadingWidgetWrap->UnloadSWidgetLPT();
		SLoadingWidgetWrap.Reset();
	}
}
