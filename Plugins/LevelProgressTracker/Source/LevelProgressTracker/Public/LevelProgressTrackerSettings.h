// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPath.h"

#include "LevelProgressTrackerSettings.generated.h"

class UWorld;

/**
 * Per-level filtering and World Partition generation rules.
 * Rules are matched by TargetLevel.
 */
USTRUCT(BlueprintType)
struct FLPTLevelRules
{
	GENERATED_BODY()

	/* Target level for this rule set. */
	UPROPERTY(EditAnywhere, Config, Category = "Level")
	TSoftObjectPtr<UWorld> TargetLevel;

	/* Exclusion mode: true means remove matching rules, false means keep only matching rules. */
	UPROPERTY(EditAnywhere, Config, Category = "Filtering")
	bool bUseExclusionMode = false;

	/* Asset path rules evaluated by exact long package name match. */
	UPROPERTY(EditAnywhere, Config, Category = "Filtering")
	TArray<FSoftObjectPath> AssetRules;

	/* Folder rules evaluated by long package name prefix match. */
	UPROPERTY(EditAnywhere, Config, Category = "Filtering")
	TArray<FDirectoryPath> FolderRules;

	/* Enables safe World Partition actor scan using only currently loaded actors. */
	UPROPERTY(EditAnywhere, Config, Category = "World Partition")
	bool bAllowWorldPartitionAutoScan = false;

	/* World Partition cell tokens evaluated by long package name substring match. */
	UPROPERTY(EditAnywhere, Config, Category = "World Partition")
	TArray<FString> WorldPartitionCells;

	/* World Partition region names (Data Layer or named region) used for actor filtering. */
	UPROPERTY(EditAnywhere, Config, Category = "World Partition")
	TArray<FName> WorldPartitionRegions;
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
	 * DatabaseFolder is interpreted as "/Game/<DatabaseFolder>".
	 */
	bool ResolveDatabaseAssetPaths(FString& OutDatabaseFolderLongPath, FString& OutDatabasePackagePath, FSoftObjectPath& OutDatabaseObjectPath) const;

	// Finds level rules by target level soft path.
	const FLPTLevelRules* FindLevelRules(const TSoftObjectPtr<UWorld>& Level) const;

	// Finds existing rules or appends a new default rule for the target level.
	FLPTLevelRules* FindOrAddLevelRules(const TSoftObjectPtr<UWorld>& Level, bool& bWasAdded);

	/* Folder name under /Game used to store LevelPreloadDatabase asset. */
	UPROPERTY(EditAnywhere, Config, Category = "Database")
	FDirectoryPath DatabaseFolder;

	/* Enables automatic database generation when a level package is saved. */
	UPROPERTY(EditAnywhere, Config, Category = "Generation")
	bool bAutoGenerateOnLevelSave = true;

	/* Per-level rules for filtering and World Partition behavior. */
	UPROPERTY(EditAnywhere, Config, Category = "Rules")
	TArray<FLPTLevelRules> LevelRules;
};
