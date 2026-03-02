// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"

class IAssetRegistry;
class UObject;
class UAssetCollectionDataLPT;
class UAssetFilterSettingsLPT;
class UDataLayerAsset;
class ULevelProgressTrackerSettings;
class UWorld;
struct FLPTFilterSettings;
struct FLevelPreloadEntryLPT;

namespace EditorModuleLPTPrivate
{
	extern const FName StyleSetName;
	extern const FName ToolbarIconName;
	extern const FName DefaultCollectionKey;

	uint32 ComputeCollectionContentHash(const UAssetCollectionDataLPT* CollectionAsset, const FLPTFilterSettings& EffectiveFilterSettings);
	uint32 ComputeLevelStateHash(UWorld* SavedWorld, const FLPTFilterSettings& EffectiveFilterSettings);

	UAssetCollectionDataLPT* GetOrCreateCollectionAsset(const ULevelProgressTrackerSettings* Settings, const FString& LevelAssetName, FName CollectionKey);
	UAssetFilterSettingsLPT* GetOrCreateFilterSettingsAsset(const ULevelProgressTrackerSettings* Settings, const FString& LevelAssetName);
	bool SaveAssetObject(UObject* AssetObject);

	bool DeduplicateCollectionAssetData(UAssetCollectionDataLPT* CollectionAsset);
	bool MaterializeCollectionPresets(
		const ULevelProgressTrackerSettings* Settings,
		const FString& LevelAssetName,
		const UAssetFilterSettingsLPT* FilterSettingsAsset,
		FLevelPreloadEntryLPT& InOutEntry);
	bool ResolveCollectionTargetDataLayerAssetsFromNames(UWorld* SavedWorld, UAssetCollectionDataLPT* CollectionAsset);

	FLPTFilterSettings BuildCollectionEffectiveRules(const FLPTFilterSettings& BaseRules, const UAssetCollectionDataLPT* CollectionAsset, bool bIsWorldPartition);
	TArray<FSoftObjectPath> BuildFilteredAssetsForRules(UWorld* SavedWorld, IAssetRegistry& Registry, const FLPTFilterSettings& EffectiveRules);
}
