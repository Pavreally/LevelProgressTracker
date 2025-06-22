// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerSubsytem.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"


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
	OnInstanceLevelLoadedLPT.Clear();
	OnGlobalLevelLoadedLPT.Clear();

	UnloadAllLPT();

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

	FName PackageName = FName(*LevelSoftPtr.ToSoftObjectPath().GetLongPackageName());

	if (FLevelState* LevelState = LevelLoadedMap.Find(PackageName))
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

		LevelLoadedMap.Remove(PackageName);
	}
}

void ULevelProgressTrackerSubsytem::UnloadAllLPT()
{
	if (LevelLoadedMap.IsEmpty())
		return;

	for (TPair<FName, FLevelState>& Level : LevelLoadedMap)
	{
		FLevelState& LevelState = Level.Value;

		if (LevelState.bIsStreamingLevel == true)
		{
			if (LevelState.Handle.IsValid())
			{
				LevelState.Handle->ReleaseHandle();
				LevelState.Handle.Reset();
			}

			if (LevelState.LevelInstanceState.LevelReference)
			{
				LevelState.LevelInstanceState.LevelReference->SetIsRequestingUnloadAndRemoval(true);
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
	FName PackageName = FName(*LevelSoftPtr.ToSoftObjectPath().GetLongPackageName());
	FString TargetLevelName = LevelSoftPtr.ToSoftObjectPath().GetAssetName();

	if (LevelLoadedMap.Find(PackageName))
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (AsyncLoadAssetsLPT): The requested level \"%s\" is currently loading or has loaded."), *TargetLevelName);

		return;
	}

	// Gather dependencies
	TArray<FName> Dependencies;

	Registry.GetDependencies(
		PackageName,
		Dependencies,
		UE::AssetRegistry::EDependencyCategory::Package,
		UE::AssetRegistry::FDependencyQuery()
	);

	if (Dependencies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT (AsyncLoadAssetsLPT): No assets found to load for level '%s'."), *PackageName.ToString());
		
		return;
	}

	// Softlink conversion and optional whitelist filtering
	TArray<FSoftObjectPath> Paths;
	TArray<FString> SWhiteListDir;
	SWhiteListDir.Reserve(WhiteListDir.Num());
	for (const FName& Dir : WhiteListDir)
	{
		SWhiteListDir.Add(Dir.ToString());
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
			for (const FString& Keyword : SWhiteListDir)
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

	// Setup load stat
	FLevelState LevelState;
	LevelState.LevelSoftPtr = LevelSoftPtr;
	LevelState.LevelName = FName(TargetLevelName);
	LevelState.TotalAssets = Paths.Num();
	LevelState.LoadedAssets = 0;
	LevelState.bIsStreamingLevel = bIsStreamingLevel;
	LevelState.LevelInstanceState = LevelInstanceState;

	// Request async load
	FStreamableManager& StreamableManager = UAssetManager::Get().GetStreamableManager();
	TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(
		Paths,
		FStreamableDelegate::CreateUObject(
			this,
			&ULevelProgressTrackerSubsytem::OnAllAssetsLoaded,
			PackageName,
			bIsStreamingLevel),
		FStreamableManager::AsyncLoadHighPriority
	);

	if (Handle.IsValid())
	{
		Handle->BindUpdateDelegate(FStreamableUpdateDelegate::CreateUObject(
			this,
			&ULevelProgressTrackerSubsytem::HandleAssetLoaded,
			PackageName
		));
		LevelState.Handle = Handle;
	}

	LevelLoadedMap.Add(PackageName, MoveTemp(LevelState));
}

void ULevelProgressTrackerSubsytem::OnAllAssetsLoaded(FName PackageName, bool bIsStreamingLevel)
{
	if (FLevelState* LevelState = LevelLoadedMap.Find(PackageName))
	{
		// Ensure LoadedAssets equals TotalAssets for accurate 100% reporting
		LevelState->LoadedAssets = LevelState->TotalAssets;

		// Broadcast final progress and loaded events
		OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, PackageName, 1.f);

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
				// Streaming level loading notification
				OnInstanceLevelLoadedLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName);
			}
		}
		else
		{
			// Open Level
			UGameplayStatics::OpenLevel(this, PackageName);
		}
	}
}

void ULevelProgressTrackerSubsytem::HandleAssetLoaded(TSharedRef<FStreamableHandle> Handle, FName PackageName)
{
	if (FLevelState* LevelState = LevelLoadedMap.Find(PackageName))
	{
		LevelState->LoadedAssets = FMath::Clamp(LevelState->LoadedAssets + 1, 0, LevelState->TotalAssets);
		
		// Check if there are no loaded assets, then display the loading progress
		float Progress = LevelState->TotalAssets > 0 ? (float)LevelState->LoadedAssets / (float)LevelState->TotalAssets : 1.f;

		OnLevelLoadProgressLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName, PackageName, Progress);
	}
}

void ULevelProgressTrackerSubsytem::OnPostLoadMapWithWorld(UWorld* LoadedWorld)
{
	if (LoadedWorld && LoadedWorld == GetWorld())
	{
		FName PackageName = FName(*LoadedWorld->GetOutermost()->GetName());
		FLevelState* LevelState = LevelLoadedMap.Find(PackageName);

		// Reset handler if level is not streaming
		if (LevelState && LevelState->Handle.IsValid())
		{
			if (LevelState->bIsStreamingLevel == false)
			{
				// Releasing the resource level handler and finishing tracking him
				LevelState->Handle.Reset();

				OnGlobalLevelLoadedLPT.Broadcast(LevelState->LevelSoftPtr, LevelState->LevelName);

				// Clear memory from unnecessary data
				LevelLoadedMap.Remove(PackageName);
			}
		}
	}
}
