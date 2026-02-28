// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "SettingsLPT.h"

struct FAssetData;
class UDataLayerAsset;
class ULevelProgressTrackerSettings;

namespace AssetFilterLPT
{
	bool ShouldIncludeAssetByClass(const FAssetData& AssetData, const FLPTLevelRules* Rules);

	TArray<FSoftObjectPath> MergeSoftObjectPaths(
		const TArray<FSoftObjectPath>& LevelPaths,
		const TArray<FSoftObjectPath>& GlobalPaths
	);

	TArray<FDirectoryPath> MergeFolderPaths(
		const TArray<FDirectoryPath>& LevelPaths,
		const TArray<FDirectoryPath>& GlobalPaths
	);

	TArray<FName> MergeNameRules(const TArray<FName>& LevelRules, const TArray<FName>& GlobalRules);

	TArray<TSoftObjectPtr<UDataLayerAsset>> MergeDataLayerAssetRules(
		const TArray<TSoftObjectPtr<UDataLayerAsset>>& LevelRules,
		const TArray<TSoftObjectPtr<UDataLayerAsset>>& GlobalRules
	);

	TArray<FString> MergeStringRules(const TArray<FString>& LevelRules, const TArray<FString>& GlobalRules);

	FLPTLevelRules BuildMergedRulesWithGlobalDominance(const FLPTLevelRules& LevelRules, const ULevelProgressTrackerSettings* Settings);
}

