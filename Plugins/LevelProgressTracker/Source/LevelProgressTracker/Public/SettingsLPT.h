// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPath.h"

#include "SettingsLPT.generated.h"

class UDataLayerAsset;

/**
 * Class-category filter used for automatically collected preload candidates.
 * Explicit asset rules are not affected by this filter.
 */
USTRUCT(BlueprintType)
struct FLPTAssetClassFilter
{
	GENERATED_BODY()

	/* Includes static mesh assets in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeStaticMeshes = true;

	/* Includes skeletal mesh assets in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeSkeletalMeshes = true;

	/* Includes material-related assets (materials, material instances/functions/collections, textures) in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeMaterials = true;

	/* Includes Niagara assets in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeNiagara = true;

	/* Includes sound assets in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeSounds = true;

	/* Includes Widget Blueprint assets in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeWidgets = true;

	/* Includes data asset types in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeDataAssets = true;
};

/**
 * Filtering and World Partition generation rules used by a single level entry.
 */
USTRUCT(BlueprintType)
struct FLPTFilterSettings
{
	GENERATED_BODY()

	/* Class-category filter used for automatically collected preload candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filtering")
	FLPTAssetClassFilter AssetClassFilter;

	/* Exclusion mode: true removes matching assets, false keeps only matching assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	bool bUseExclusionMode = false;

	/* Asset path rules evaluated by exact long package name match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<FSoftObjectPath> AssetRules;

	/* Folder rules evaluated by long package name prefix match. Use Content Browser paths such as '/Game/Folder' or '/PluginName/Folder'. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering", meta = (ContentDir, LongPackageName, ForceShowPluginContent))
	TArray<FDirectoryPath> FolderRules;

	/* If true, preload assets are requested in chunks. If false, all assets are requested as one aggregated batch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preload Progress", meta = (ToolTip = "If true, preload assets are requested in chunks. If false, all assets are requested as one aggregated batch."))
	bool bUseChunkedPreload = true;

	/* Number of assets per preload chunk. 1 means per-asset loading; larger values batch assets to reduce overhead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preload Progress", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Number of assets per preload chunk. 1 means per-asset loading; larger values batch assets into groups for better performance."))
	int32 PreloadChunkSize = 32;

	/* Enables safe World Partition actor scan using only currently loaded actors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition")
	bool bAllowWorldPartitionAutoScan = false;

	/* Allows full World Partition actor scan with no Data Layer/Cell scope. Disabled by default to avoid accidental heavy generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition")
	bool bAllowWorldPartitionUnscopedAutoScan = false;

	/* World Partition Data Layer assets used for actor filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition", meta = (DisplayName = "Data Layer Assets"))
	TArray<TSoftObjectPtr<UDataLayerAsset>> WorldPartitionDataLayerAssets;

	/* World Partition Data Layer names used for actor filtering when no asset reference is available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition", meta = (DisplayName = "Data Layer Names", AdvancedDisplay))
	TArray<FName> WorldPartitionRegions;

	/* World Partition cell tokens evaluated by long package name substring match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition", meta = (DisplayName = "Cell Rules"))
	TArray<FString> WorldPartitionCells;
};

/**
 * Project settings for Level Progress Tracker.
 * These settings are used during editor-time preload database generation.
 * Runtime loading reads only preload database and does not query AssetRegistry.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Level Progress Tracker"))
class LEVELPROGRESSTRACKER_API ULevelProgressTrackerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULevelProgressTrackerSettings();

	// UDeveloperSettings
	virtual FName GetCategoryName() const override;
	// End UDeveloperSettings

	/**
	 * Resolves validated package/object paths for preload database asset.
	 * DatabaseFolder is a full long package path (for example: "/Game/_DataLPT" or "/PluginName/Data").
	 */
	bool ResolveDatabaseAssetPaths(FString& OutDatabaseFolderLongPath, FString& OutDatabasePackagePath, FSoftObjectPath& OutDatabaseObjectPath) const;

	/** Resolves validated long package folder path for collection DataAssets. */
	bool ResolveAssetCollectionFolderPath(FString& OutCollectionFolderLongPath) const;

	/** Resolves validated long package folder path for filter settings DataAssets. */
	bool ResolveFilterSettingsFolderPath(FString& OutFilterSettingsFolderLongPath) const;

	/** Copies project defaults used for newly created filter settings assets. */
	void BuildGlobalDefaultRules(FLPTFilterSettings& OutRules) const;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpenLevelRulesEditorRequested, ULevelProgressTrackerSettings *);
	static FOnOpenLevelRulesEditorRequested OnOpenLevelRulesEditorRequested;
#endif

	/** Opens the per-level rules editor for the currently opened level. */
	void OpenLevelRulesEditorForCurrentLevel();

	/* Folder for LevelPreloadDatabaseLPT asset. Use Content Browser paths such as '/Game/_DataLPT' or '/PluginName/Data'. */
	UPROPERTY(EditAnywhere, Config, Category = "Database", meta = (ToolTip = "Folder for LevelPreloadDatabaseLPT asset. Use Content Browser paths such as '/Game/_DataLPT' or '/PluginName/Data'.", ContentDir, LongPackageName, ForceShowPluginContent))
	FDirectoryPath DatabaseFolder;

	/* Folder for AssetCollectionDataLPT assets. If empty, defaults to '<Database Folder>/AssetList'. */
	UPROPERTY(EditAnywhere, Config, Category = "Database", meta = (ToolTip = "Folder for AssetCollectionDataLPT assets. If empty, defaults to '<Database Folder>/AssetList'.", ContentDir, LongPackageName, ForceShowPluginContent))
	FDirectoryPath AssetCollectionFolder;

	/* Folder for AssetFilterSettingsLPT assets. If empty, defaults to '<Database Folder>/AssetFilterSettings'. */
	UPROPERTY(EditAnywhere, Config, Category = "Database", meta = (ToolTip = "Folder for AssetFilterSettingsLPT assets. If empty, defaults to '<Database Folder>/AssetFilterSettings'.", ContentDir, LongPackageName, ForceShowPluginContent))
	FDirectoryPath AssetFilterSettingsFolder;

	/* Enables automatic database generation when a level package is saved. */
	UPROPERTY(EditAnywhere, Config, Category = "Generation")
	bool bAutoGenerateOnLevelSave = true;

	/* Default class-category filter used when creating new AssetFilterSettingsLPT assets. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults - Class Filter", meta = (ToolTip = "Class-category filter used for automatically collected preload candidates. Explicit asset rules are not affected by this filter."))
	FLPTAssetClassFilter AssetClassFilter;

	/* Default preload mode used when creating new AssetFilterSettingsLPT assets. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults - Preload Progress", meta = (ToolTip = "If true, preload assets are requested in chunks. If false, all assets are requested as one aggregated batch."))
	bool bUseChunkedPreload = true;

	/* Default number of assets per preload chunk used when creating new AssetFilterSettingsLPT assets. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults - Preload Progress", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Number of assets per preload chunk. 1 means per-asset loading; larger values batch assets into groups for better performance."))
	int32 PreloadChunkSize = 32;
};

