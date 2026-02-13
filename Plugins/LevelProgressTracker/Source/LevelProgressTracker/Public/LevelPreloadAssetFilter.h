// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LevelProgressTrackerSettings.h"

#include "LevelPreloadAssetFilter.generated.h"

/**
 * Runtime filter utility used by editor workflows to keep include/exclude logic in one place.
 */
UCLASS()
class LEVELPROGRESSTRACKER_API ULevelPreloadAssetFilter : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Filters input asset paths using project settings rules and mode.
	 * In exclusion mode matching assets are removed.
	 * In inclusion mode only matching assets are kept.
	 */
	static TArray<FSoftObjectPath> FilterAssets(const TArray<FSoftObjectPath>& InAssets, const FLPTLevelRules* Rules);

	/**
	 * Filters a World Partition actor by region and cell rules.
	 * Region rules are applied first, then cell rules.
	 */
	static bool ShouldIncludeWorldPartitionActor(
		const FSoftObjectPath& ActorPath,
		const TArray<FName>& ActorRegionNames,
		const FLPTLevelRules* Rules
	);

	// Returns true when at least one asset or folder rule exists.
	static bool HasAnyAssetOrFolderRule(const FLPTLevelRules* Rules);

	// Returns true when any rule list contains at least one item.
	static bool HasAnyRule(const FLPTLevelRules* Rules);

	// Resolves validated package/object paths for preload database.
	static bool ResolveDatabaseAssetPath(
		const ULevelProgressTrackerSettings* Settings,
		FString& OutDatabasePackagePath,
		FSoftObjectPath& OutDatabaseObjectPath
	);
};
