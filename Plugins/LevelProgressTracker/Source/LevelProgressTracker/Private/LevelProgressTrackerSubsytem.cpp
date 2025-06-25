// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerSubsytem.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet/GameplayStatics.h"


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

	UnloadAllLevelInstanceLPT();

	Super::Deinitialize();
}

#pragma endregion SUBSYSTEM

void ULevelProgressTrackerSubsytem::OpenLevelLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, TArray<FName> WhiteListDir)
{
	if (LevelSoftPtr.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (OpenLevelLPT): Invalid level pointer."));

		return;
	}

	// Preloads the target level's resources, waits for all resources to load, and starts the level itself.
	AsyncLoadAssetsLPT(LevelSoftPtr, WhiteListDir);
}

void ULevelProgressTrackerSubsytem::LoadLevelInstanceLPT(TSoftObjectPtr<UWorld> LevelSoftPtr, TArray<FName> WhiteListDir, const FTransform Transform, TSubclassOf<ULevelStreamingDynamic> OptionalLevelStreamingClass, bool bLoadAsTempPackage)
{
	if (LevelSoftPtr.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (LoadLevelInstanceLPT): Invalid level pointer."));

		return;
	}

	FLevelInstanceState LevelInstanceState;
	LevelInstanceState.Transform = Transform;
	LevelInstanceState.OptionalLevelStreamingClass = OptionalLevelStreamingClass;
	LevelInstanceState.bLoadAsTempPackage = bLoadAsTempPackage;

	// Preloads the target level's resources, waits for all resources to load, and starts the level itself.
	AsyncLoadAssetsLPT(LevelSoftPtr, WhiteListDir, true, LevelInstanceState);
}

void ULevelProgressTrackerSubsytem::UnloadLevelInstanceLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr)
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

void ULevelProgressTrackerSubsytem::AsyncLoadAssetsLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, TArray<FName>& WhiteListDir, bool bIsStreamingLevel, FLevelInstanceState LevelInstanceState)
{
	if (LevelSoftPtr.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (AsyncLoadAssetsLPT): Invalid level pointer."));

		return;
	}

	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = RegistryModule.Get();
	FName PackagePath = FName(*LevelSoftPtr.ToSoftObjectPath().GetLongPackageName());
	FString TargetLevelName = LevelSoftPtr.ToSoftObjectPath().GetAssetName();

	if (LevelLoadedMap.Contains(PackagePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (AsyncLoadAssetsLPT): The requested level \"%s\" is currently loading or has loaded."), *TargetLevelName);

		return;
	}

	// Gather dependencies
	TArray<FName> Dependencies;

	Registry.GetDependencies(
		PackagePath,
		Dependencies,
		UE::AssetRegistry::EDependencyCategory::Package,
		UE::AssetRegistry::FDependencyQuery()
	);

	if (Dependencies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (AsyncLoadAssetsLPT): No assets found to load for level '%s'."), *PackagePath.ToString());
		
		return;
	}

	// Softlink conversion and optional whitelist filtering
	TArray<FSoftObjectPath> Paths;
	GetFilteredWhiteList(WhiteListDir, Dependencies, Paths);

	// Setup load stat
	TSharedRef<FLevelState> LevelState = MakeShared<FLevelState>();
	LevelState->LevelSoftPtr = LevelSoftPtr;
	LevelState->LevelName = FName(TargetLevelName);
	LevelState->TotalAssets = Paths.Num();
	LevelState->LoadedAssets = 0;
	LevelState->LevelInstanceState = LevelInstanceState;

	// Determining the level type (World Partition)
	if (!bIsStreamingLevel && CheckWorldPartition(LevelSoftPtr, Registry))
	{
		LevelState->LoadMethod = ELevelLoadMethod::WorldPartition;
	}
	else if (bIsStreamingLevel)
	{
		LevelState->LoadMethod = ELevelLoadMethod::LevelStreaming;
	}

	// Request async load
	FStreamableManager& StreamableManager = UAssetManager::Get().GetStreamableManager();
	TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(
		Paths,
		FStreamableDelegate::CreateUObject(
			this,
			&ULevelProgressTrackerSubsytem::OnAllAssetsLoaded,
			PackagePath,
			bIsStreamingLevel,
			LevelState),
		FStreamableManager::AsyncLoadHighPriority
	);

	if (Handle.IsValid())
	{
		Handle->BindUpdateDelegate(FStreamableUpdateDelegate::CreateUObject(
			this,
			&ULevelProgressTrackerSubsytem::HandleAssetLoaded,
			PackagePath,
			LevelState
		));
		LevelState->Handle = Handle;
	}

	LevelLoadedMap.Add(PackagePath, LevelState);
}

void ULevelProgressTrackerSubsytem::OnAllAssetsLoaded(FName PackagePath, bool bIsStreamingLevel, TSharedRef<FLevelState> LevelState)
{
	// Ensure LoadedAssets equals TotalAssets for accurate 100% reporting
	LevelState->LoadedAssets = LevelState->TotalAssets;

	// Broadcast final progress and loaded events
	OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, 1.f, LevelState->LoadedAssets, LevelState->TotalAssets);

	StartLevelLPT(PackagePath, bIsStreamingLevel, LevelState);
}

void ULevelProgressTrackerSubsytem::StartLevelLPT(FName PackagePath, bool bIsStreamingLevel, TSharedRef<FLevelState> LevelState)
{
	if (bIsStreamingLevel)
	{
		// Load Level Instance
		bool bOutSuccess = false;
		const FString OptionalLevelNameOverride = TEXT("");

		ULevelStreamingDynamic* StreamingLevel = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
			this,
			LevelState->LevelSoftPtr,
			LevelState->LevelInstanceState.Transform,
			bOutSuccess,
			OptionalLevelNameOverride,
			LevelState->LevelInstanceState.OptionalLevelStreamingClass,
			LevelState->LevelInstanceState.bLoadAsTempPackage
		);

		if (StreamingLevel)
		{
			LevelState->LevelInstanceState.LevelReference = StreamingLevel;

			// Subscribe to the event when the streaming level is fully opened and loaded
			StreamingLevel->OnLevelShown.AddDynamic(
				this,
				&ULevelProgressTrackerSubsytem::OnLevelShown
			);
		}
	}
	else
	{
		// Open Level
		UGameplayStatics::OpenLevel(this, PackagePath);
	}
}

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

void ULevelProgressTrackerSubsytem::OnPostLoadMapWithWorld(UWorld* LoadedWorld)
{
	if (LoadedWorld && LoadedWorld == GetWorld())
	{
		FName PackageName = FName(*LoadedWorld->GetOutermost()->GetName());
		TSharedPtr<FLevelState> LevelState = LevelLoadedMap.FindRef(PackageName);

		// Reset handler if level is not streaming
		if (LevelState && LevelState->Handle.IsValid())
		{
			if (LevelState->LoadMethod != ELevelLoadMethod::LevelStreaming)
			{
				// Releasing the resource level handler and finishing tracking him
				LevelState->Handle->ReleaseHandle();
				LevelState->Handle.Reset();
				// Streaming level loading notification
				OnLevelLoadedLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName);
				// Clear memory from unnecessary data
				LevelLoadedMap.Remove(PackageName);
			}
		}
	}
}

void ULevelProgressTrackerSubsytem::OnLevelShown()
{
	for (TPair<FName, TSharedPtr<FLevelState>>&  Level : LevelLoadedMap)
	{
		TSharedPtr<FLevelState>& LevelState = Level.Value;

		if (LevelState->LoadMethod == ELevelLoadMethod::LevelStreaming && LevelState->LevelInstanceState.IsLoaded == true)
		{
			continue;
		}

		if (LevelState && LevelState->LevelInstanceState.LevelReference && 
				LevelState->LevelInstanceState.LevelReference->HasLoadedLevel() &&
				LevelState->LevelInstanceState.LevelReference->GetLoadedLevel()->bIsVisible)
		{
			LevelState->LevelInstanceState.IsLoaded = true;

			// Remove delegate tracking for the streaming level
			LevelState->LevelInstanceState.LevelReference->OnLevelShown.RemoveDynamic(
				this,
				&ULevelProgressTrackerSubsytem::OnLevelShown
			);
			// Releasing the resource level handler and finishing tracking him
			LevelState->Handle->ReleaseHandle();
			LevelState->Handle.Reset();
			// Streaming level loading notification
			OnLevelLoadedLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName);
		}
	}
}
