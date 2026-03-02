// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "SettingsLPT.h"

#include "AssetFilterSettingsLPT.generated.h"

class UDataLayerAsset;

USTRUCT(BlueprintType)
struct FLPTCollectionPresetLPT
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Presets")
	FName CollectionKey = FName(TEXT("Default"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Presets")
	bool bAutoGenerate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Presets")
	FGameplayTagContainer GroupTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Presets", meta = (DisplayName = "Data Layer Assets"))
	TArray<TSoftObjectPtr<UDataLayerAsset>> TargetDataLayers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Presets", meta = (DisplayName = "Data Layer Names", AdvancedDisplay))
	TArray<FName> TargetDataLayerNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Presets", meta = (DisplayName = "Cell Rules"))
	TArray<FString> TargetCellRules;
};

UCLASS(BlueprintType)
class LEVELPROGRESSTRACKER_API UAssetFilterSettingsLPT : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Filtering")
	FLPTAssetClassFilter AssetClassFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	bool bUseExclusionMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering")
	TArray<FSoftObjectPath> AssetRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filtering", meta = (ContentDir, LongPackageName, ForceShowPluginContent))
	TArray<FDirectoryPath> FolderRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preload Progress", meta = (ToolTip = "If true, preload assets are requested in chunks. If false, all assets are requested as one aggregated batch."))
	bool bUseChunkedPreload = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preload Progress", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Number of assets per preload chunk. 1 means per-asset loading; larger values batch assets into groups for better performance."))
	int32 PreloadChunkSize = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition")
	bool bAllowWorldPartitionAutoScan = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Partition", meta = (ToolTip = "If true and no Data Layer/Cell scope is set in Collection Presets, generation may scan the full World Partition level. Disabled by default to avoid heavy generation on large maps."))
	bool bAllowWorldPartitionUnscopedAutoScan = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Presets", meta = (ToolTip = "Optional collection presets that can be materialized into AssetCollectionDataLPT assets."))
	TArray<FLPTCollectionPresetLPT> CollectionPresets;

	FLPTFilterSettings ToFilterSettings() const;
	void InitializeDefaultsFromProjectSettings(const ULevelProgressTrackerSettings* Settings);
};
