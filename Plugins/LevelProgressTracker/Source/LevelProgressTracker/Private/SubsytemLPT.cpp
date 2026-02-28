// Pavel Gornostaev <https://github.com/Pavreally>

#include "SubsytemLPT.h"
#include "LevelPreloadAssetFilter.h"
#include "LevelPreloadDatabase.h"
#include "SettingsLPT.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StreamableManager.h"
#include "UObject/Package.h"
#include "SlateWidgetWrapLPT.h"


#pragma region SUBSYSTEM
ULevelProgressTrackerSubsytem::ULevelProgressTrackerSubsytem()
{
}

void ULevelProgressTrackerSubsytem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const ULevelProgressTrackerSettings* Settings = GetDefault<ULevelProgressTrackerSettings>();

	FString DatabasePackagePath;
	FSoftObjectPath DatabaseObjectPath;

	if (ULevelPreloadAssetFilter::ResolveDatabaseAssetPath(Settings, DatabasePackagePath, DatabaseObjectPath))
	{
		PreloadDatabaseAsset = TSoftObjectPtr<ULevelPreloadDatabase>(DatabaseObjectPath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT: Invalid database folder in project settings. Preload database path could not be resolved. Candidate package path: '%s'."),
			*DatabasePackagePath
		);
		PreloadDatabaseAsset.Reset();
	}

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

	// Clearing widgets
	RemoveSlateWidgetLPT();

	// Clearing resources
	UnloadAllLevelInstanceLPT();

	Super::Deinitialize();
}

#pragma endregion SUBSYSTEM

void ULevelProgressTrackerSubsytem::OnPostLoadMapWithWorld(UWorld* LoadedWorld)
{
	if (LoadedWorld && LoadedWorld == GetWorld())
	{
		FString OriginalPackageName = LoadedWorld->GetOutermost()->GetName();
		// Defining the method for forming the package name path
		FName PackageName = CheckingPIE() ? FName(*UWorld::RemovePIEPrefix(OriginalPackageName)) : FName(*OriginalPackageName);
		TSharedPtr<FLevelState> LevelState = LevelLoadedMap.FindRef(PackageName);
		// Reset handler if level is not streaming
		if (LevelState && LevelState->LoadMethod != ELevelLoadMethod::LevelStreaming)
		{
			// Releasing resource preload handles and finishing tracking.
			ReleaseLevelStateHandles(LevelState.ToSharedRef(), false);

			// Streaming level loading notification
			OnLevelLoadedLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName);
			// Clear memory from unnecessary data
			LevelLoadedMap.Remove(PackageName);
		}
	}
}

void ULevelProgressTrackerSubsytem::CreateSlateWidgetLPT(TSubclassOf<UUserWidget> UserWidgetClass, int32 ZOrder)
{
	SWidgetWrap = SNew(SWidgetWrapLPT);
	// Add Slate widget to viewort
	if (GEngine)
	{
		GEngine->GameViewport->AddViewportWidgetContent(SWidgetWrap.ToSharedRef(), ZOrder);
	}
	// Add UMG to Slate widget
	if (SWidgetWrap.IsValid())
	{
		SWidgetWrap->LoadEmbeddedUWidgetLPT(UserWidgetClass);
	}
}

void ULevelProgressTrackerSubsytem::RemoveSlateWidgetLPT()
{
	if (SWidgetWrap.IsValid())
	{
		SWidgetWrap->UnloadSWidgetLPT();
		SWidgetWrap.Reset();
	}
}

bool ULevelProgressTrackerSubsytem::CheckingPIE()
{
	UWorld* World = GetWorld();
	
	return World && World->IsPlayInEditor();
}


