// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPath.h"

#include "LevelProgressTrackerSettings.generated.h"

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

	/* Includes material and material instance assets in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeMaterials = true;

	/* Includes Niagara assets in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeNiagara = true;

	/* Includes sound assets in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeSounds = true;

	/* Includes data asset types in auto-collected candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filter")
	bool bIncludeDataAssets = true;
};

/**
 * Filtering and World Partition generation rules used by a single level entry.
 */
USTRUCT(BlueprintType)
struct FLPTLevelRules
{
	GENERATED_BODY()

	/* Enables merge with global defaults during generation. Level rules are merged first, then global defaults are applied and override conflicting options. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Global Filtering")
	bool bRulesInitializedFromGlobalDefaults = false;

	/* Exclusion mode: true removes matching assets, false keeps only matching assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	bool bUseExclusionMode = false;

	/* Asset path rules evaluated by exact long package name match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<FSoftObjectPath> AssetRules;

	/* Folder rules evaluated by long package name prefix match. Use Content Browser paths such as '/Game/Folder' or '/PluginName/Folder'. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering", meta = (ContentDir, LongPackageName, ForceShowPluginContent))
	TArray<FDirectoryPath> FolderRules;

	/* Class-category filter used for automatically collected preload candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	FLPTAssetClassFilter AssetClassFilter;

	/* Enables safe World Partition actor scan using only currently loaded actors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition")
	bool bAllowWorldPartitionAutoScan = false;

	/* World Partition Data Layer assets used for actor filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition", meta = (DisplayName = "Data Layer Assets"))
	TArray<TSoftObjectPtr<UDataLayerAsset>> WorldPartitionDataLayerAssets;

	/* Legacy World Partition Data Layer names used for actor filtering when no asset reference is available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition", meta = (DisplayName = "Data Layer Names (Legacy)", AdvancedDisplay))
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

	/** Copies global defaults into per-level rules when a new level entry is created. */
	void BuildGlobalDefaultRules(FLPTLevelRules& OutRules) const;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpenLevelRulesEditorRequested, ULevelProgressTrackerSettings*);
	static FOnOpenLevelRulesEditorRequested OnOpenLevelRulesEditorRequested;
#endif

	/** Opens the per-level rules editor for the currently opened level. */
	void OpenLevelRulesEditorForCurrentLevel();

	/* Full long package path for LevelPreloadDatabase folder. '/Game' is the project content root. */
	UPROPERTY(EditAnywhere, Config, Category = "Database", meta = (ToolTip = "Full long package path for LevelPreloadDatabase folder. '/Game' is the project content root."))
	FDirectoryPath DatabaseFolder;

	/* Enables automatic database generation when a level package is saved. */
	UPROPERTY(EditAnywhere, Config, Category = "Generation")
	bool bAutoGenerateOnLevelSave = true;

	/* These settings are used as default values when creating rules for a new level. They do not affect existing level entries. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults", meta = (ToolTip = "These settings are used as default values when creating rules for a new level. They do not affect existing level entries."))
	bool bUseExclusionMode = false;

	/* These settings are used as default values when creating rules for a new level. They do not affect existing level entries. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults", meta = (ToolTip = "These settings are used as default values when creating rules for a new level. They do not affect existing level entries."))
	TArray<FSoftObjectPath> AssetRules;

	/* These settings are used as default values when creating rules for a new level. They do not affect existing level entries. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults", meta = (ToolTip = "These settings are used as default values when creating rules for a new level. They do not affect existing level entries.", ContentDir, LongPackageName, ForceShowPluginContent))
	TArray<FDirectoryPath> FolderRules;

	/* These settings are used as default values when creating rules for a new level. They do not affect existing level entries. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults - Class Filter", meta = (ToolTip = "These settings are used as default values when creating rules for a new level. They do not affect existing level entries."))
	FLPTAssetClassFilter AssetClassFilter;

	/* These settings are used as default values when creating rules for a new level. They do not affect existing level entries. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults - WorldPartition", meta = (ToolTip = "These settings are used as default values when creating rules for a new level. They do not affect existing level entries."))
	bool bAllowWorldPartitionAutoScan = false;

	/* These settings are used as default values when creating rules for a new level. They do not affect existing level entries. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults - WorldPartition", meta = (DisplayName = "Data Layer Assets", ToolTip = "These settings are used as default values when creating rules for a new level. They do not affect existing level entries."))
	TArray<TSoftObjectPtr<UDataLayerAsset>> WorldPartitionDataLayerAssets;

	/* These settings are used as default values when creating rules for a new level. They do not affect existing level entries. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults - WorldPartition", meta = (DisplayName = "Data Layer Names (Legacy)", AdvancedDisplay, ToolTip = "These settings are used as default values when creating rules for a new level. They do not affect existing level entries."))
	TArray<FName> WorldPartitionRegions;

	/* These settings are used as default values when creating rules for a new level. They do not affect existing level entries. */
	UPROPERTY(EditAnywhere, Config, Category = "Global Rule Defaults - WorldPartition", meta = (DisplayName = "Cell Rules", ToolTip = "These settings are used as default values when creating rules for a new level. They do not affect existing level entries."))
	TArray<FString> WorldPartitionCells;
	
};
