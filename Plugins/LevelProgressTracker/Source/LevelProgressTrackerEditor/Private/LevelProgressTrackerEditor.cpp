// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerEditor.h"
#include "LevelPreloadAssetFilter.h"
#include "LevelPreloadDatabase.h"
#include "LevelProgressTrackerSettings.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"


namespace LevelProgressTrackerEditorPrivate
{
	static bool IsEngineOrScriptPackage(const FString& LongPackageName)
	{
		return LongPackageName.StartsWith(TEXT("/Engine/")) || LongPackageName.StartsWith(TEXT("/Script/"));
	}

	static void AddFallbackAssetFromPackagePath(
		const FString& PackageLongPath,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageLongPath);
		if (AssetName.IsEmpty())
		{
			return;
		}

		const FSoftObjectPath FallbackPath(FString::Printf(TEXT("%s.%s"), *PackageLongPath, *AssetName));
		if (FallbackPath.IsValid() && !UniquePaths.Contains(FallbackPath))
		{
			UniquePaths.Add(FallbackPath);
			OutAssets.Add(FallbackPath);
		}
	}

	static void AppendAssetsFromPackage(
		IAssetRegistry& Registry,
		const FName PackageName,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	)
	{
		const FString PackageLongPath = PackageName.ToString();
		if (IsEngineOrScriptPackage(PackageLongPath))
		{
			return;
		}

		TArray<FAssetData> PackageAssets;
		Registry.GetAssetsByPackageName(PackageName, PackageAssets, true);

		if (PackageAssets.IsEmpty())
		{
			AddFallbackAssetFromPackagePath(PackageLongPath, UniquePaths, OutAssets);
			return;
		}

		for (const FAssetData& AssetData : PackageAssets)
		{
			if (!AssetData.IsValid() || AssetData.HasAnyPackageFlags(PKG_EditorOnly))
			{
				continue;
			}

			const FSoftObjectPath AssetPath = AssetData.GetSoftObjectPath();
			if (!AssetPath.IsValid())
			{
				continue;
			}

			const FString AssetLongPackageName = AssetPath.GetLongPackageName();
			if (IsEngineOrScriptPackage(AssetLongPackageName) || UniquePaths.Contains(AssetPath))
			{
				continue;
			}

			UniquePaths.Add(AssetPath);
			OutAssets.Add(AssetPath);
		}
	}

	static void AppendDirectDependenciesAssets(
		IAssetRegistry& Registry,
		const FName RootPackageName,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	)
	{
		TArray<FName> Dependencies;
		Registry.GetDependencies(
			RootPackageName,
			Dependencies,
			UE::AssetRegistry::EDependencyCategory::Package,
			UE::AssetRegistry::FDependencyQuery()
		);

		for (const FName DependencyPackageName : Dependencies)
		{
			AppendAssetsFromPackage(Registry, DependencyPackageName, UniquePaths, OutAssets);
		}
	}
}

void FLevelProgressTrackerEditorModule::StartupModule()
{
#if WITH_EDITOR
	UPackage::PackageSavedWithContextEvent.AddRaw(this, &FLevelProgressTrackerEditorModule::OnPackageSaved);
#endif
}

void FLevelProgressTrackerEditorModule::ShutdownModule()
{
#if WITH_EDITOR
	UPackage::PackageSavedWithContextEvent.RemoveAll(this);
#endif
}

#if WITH_EDITOR
void FLevelProgressTrackerEditorModule::OnPackageSaved(const FString& PackageFilename, UPackage* SavedPackage, FObjectPostSaveContext SaveContext)
{
	(void)PackageFilename;
	(void)SaveContext;

	const ULevelProgressTrackerSettings* Settings = GetDefault<ULevelProgressTrackerSettings>();
	if (Settings && !Settings->bAutoGenerateOnLevelSave)
	{
		return;
	}

	if (!SavedPackage)
	{
		return;
	}

	UWorld* SavedWorld = UWorld::FindWorldInPackage(SavedPackage);
	if (!SavedWorld)
	{
		return;
	}

	RebuildLevelDependencies(SavedWorld);
}

void FLevelProgressTrackerEditorModule::RebuildLevelDependencies(UWorld* SavedWorld)
{
	if (!SavedWorld)
	{
		return;
	}

	ULevelProgressTrackerSettings* Settings = GetMutableDefault<ULevelProgressTrackerSettings>();
	if (!Settings)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Project settings are not available. Skipping database generation."));
		return;
	}

	const FString LevelPackagePath = SavedWorld->GetOutermost()->GetName();
	const FString LevelAssetName = FPackageName::GetLongPackageAssetName(LevelPackagePath);
	const FSoftObjectPath LevelObjectPath(FString::Printf(TEXT("%s.%s"), *LevelPackagePath, *LevelAssetName));
	const TSoftObjectPtr<UWorld> LevelSoftPtr(LevelObjectPath);

	bool bWasRuleAdded = false;
	FLPTLevelRules* LevelRules = Settings->FindOrAddLevelRules(LevelSoftPtr, bWasRuleAdded);
	if (!LevelRules)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to resolve per-level rules for '%s'."), *LevelPackagePath);
		return;
	}

	if (bWasRuleAdded)
	{
		Settings->SaveConfig();
	}

	const bool bIsWorldPartition = SavedWorld->IsPartitionedWorld();

	if (bIsWorldPartition && !LevelRules->bAllowWorldPartitionAutoScan)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: World Partition auto scan is disabled. Skipping database generation for '%s'."),
			*SavedWorld->GetOutermost()->GetName()
		);
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = AssetRegistryModule.Get();

	TSet<FSoftObjectPath> UniqueCandidateAssets;
	TArray<FSoftObjectPath> CandidateAssets;

	if (!bIsWorldPartition)
	{
		const FName LevelPackageName = FName(*SavedWorld->GetOutermost()->GetName());
		LevelProgressTrackerEditorPrivate::AppendDirectDependenciesAssets(Registry, LevelPackageName, UniqueCandidateAssets, CandidateAssets);
	}
	else
	{
		TSet<FName> CandidateActorPackages;

		for (ULevel* LoadedLevel : SavedWorld->GetLevels())
		{
			if (!LoadedLevel)
			{
				continue;
			}

			for (AActor* Actor : LoadedLevel->Actors)
			{
				if (!IsValid(Actor))
				{
					continue;
				}

				UPackage* ActorPackage = Actor->GetPackage();
				if (!ActorPackage)
				{
					continue;
				}

				const FString ActorPackagePath = ActorPackage->GetName();
				const FString ActorAssetName = FPackageName::GetLongPackageAssetName(ActorPackagePath);
				const FString ActorObjectName = !ActorAssetName.IsEmpty() ? ActorAssetName : Actor->GetName();
				const FSoftObjectPath ActorObjectPath(FString::Printf(TEXT("%s.%s"), *ActorPackagePath, *ActorObjectName));

				const TArray<FName> ActorRegions = Actor->GetDataLayerInstanceNames();
				if (!ULevelPreloadAssetFilter::ShouldIncludeWorldPartitionActor(ActorObjectPath, ActorRegions, LevelRules))
				{
					continue;
				}

				CandidateActorPackages.Add(FName(*ActorPackagePath));
			}
		}

		// World Partition scan uses only packages of actors currently loaded in memory.
		// No forced loading of hidden cells or full-world traversal is performed.
		for (const FName ActorPackageName : CandidateActorPackages)
		{
			LevelProgressTrackerEditorPrivate::AppendAssetsFromPackage(Registry, ActorPackageName, UniqueCandidateAssets, CandidateAssets);
			LevelProgressTrackerEditorPrivate::AppendDirectDependenciesAssets(Registry, ActorPackageName, UniqueCandidateAssets, CandidateAssets);
		}
	}

	const TArray<FSoftObjectPath> FilteredAssets = ULevelPreloadAssetFilter::FilterAssets(CandidateAssets, LevelRules);

	ULevelPreloadDatabase* DatabaseAsset = GetOrCreateDatabaseAsset(Settings);
	if (!DatabaseAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to create or load LevelPreloadDatabase asset."));
		return;
	}

	DatabaseAsset->Modify();
	DatabaseAsset->UpdateEntry(LevelSoftPtr, FilteredAssets);
	DatabaseAsset->MarkPackageDirty();
	DatabaseAsset->GetOutermost()->MarkPackageDirty();

	if (!SaveDatabaseAsset(DatabaseAsset))
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to save LevelPreloadDatabase after updating '%s'."),
			*LevelPackagePath
		);
	}
}

ULevelPreloadDatabase* FLevelProgressTrackerEditorModule::GetOrCreateDatabaseAsset(const ULevelProgressTrackerSettings* Settings) const
{
	FString DatabasePackagePath;
	FSoftObjectPath DatabaseObjectPath;

	if (!ULevelPreloadAssetFilter::ResolveDatabaseAssetPath(Settings, DatabasePackagePath, DatabaseObjectPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Invalid database folder in project settings. Expected a valid path under /Game."));
		return nullptr;
	}

	UPackage* DatabasePackage = FindPackage(nullptr, *DatabasePackagePath);
	if (!DatabasePackage)
	{
		DatabasePackage = CreatePackage(*DatabasePackagePath);
	}

	if (!DatabasePackage)
	{
		return nullptr;
	}

	FString DatabaseAssetName = DatabaseObjectPath.GetAssetName();
	if (DatabaseAssetName.IsEmpty())
	{
		DatabaseAssetName = FPackageName::GetLongPackageAssetName(DatabasePackagePath);
	}

	ULevelPreloadDatabase* DatabaseAsset = FindObject<ULevelPreloadDatabase>(DatabasePackage, *DatabaseAssetName);
	if (DatabaseAsset)
	{
		return DatabaseAsset;
	}

	DatabaseAsset = NewObject<ULevelPreloadDatabase>(
		DatabasePackage,
		ULevelPreloadDatabase::StaticClass(),
		*DatabaseAssetName,
		RF_Public | RF_Standalone
	);

	if (!DatabaseAsset)
	{
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(DatabaseAsset);
	DatabaseAsset->MarkPackageDirty();
	DatabasePackage->MarkPackageDirty();

	SaveDatabaseAsset(DatabaseAsset);

	return DatabaseAsset;
}

bool FLevelProgressTrackerEditorModule::SaveDatabaseAsset(ULevelPreloadDatabase* DatabaseAsset) const
{
	if (!DatabaseAsset)
	{
		return false;
	}

	UPackage* Package = DatabaseAsset->GetOutermost();
	if (!Package)
	{
		return false;
	}

	const FString PackageName = Package->GetName();
	FString PackageFilename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}

	const FString PackageDirectory = FPaths::GetPath(PackageFilename);
	IFileManager::Get().MakeDirectory(*PackageDirectory, true);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	SaveArgs.Error = GError;

	return UPackage::SavePackage(Package, DatabaseAsset, *PackageFilename, SaveArgs);
}
#endif

IMPLEMENT_MODULE(FLevelProgressTrackerEditorModule, LevelProgressTrackerEditor)
