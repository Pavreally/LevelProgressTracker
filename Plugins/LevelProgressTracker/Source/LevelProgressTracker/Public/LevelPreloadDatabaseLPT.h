// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AssetCollectionDataLPT.h"
#include "AssetFilterSettingsLPT.h"

#include "LevelPreloadDatabaseLPT.generated.h"

class UWorld;

USTRUCT(BlueprintType)
struct FLevelPreloadEntryLPT
{
	GENERATED_BODY()

public:
	/* Target level for this preload entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LPT")
	TSoftObjectPtr<UWorld> Level;

	/* UTC timestamp when this entry was generated or updated. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT")
	FDateTime GenerationTimestamp;

	/* Filter settings DataAsset used when generating preload collections for this level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LPT")
	TSoftObjectPtr<UAssetFilterSettingsLPT> FilterSettings;

	/* Collection assets used for runtime preload selection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LPT")
	TArray<TSoftObjectPtr<UAssetCollectionDataLPT>> Collections;

	/* Hash of level state and filter settings used to validate generated content. */
	UPROPERTY(VisibleAnywhere, Category = "LPT")
	uint32 LevelStateHash = 0;
};

UCLASS(BlueprintType)
class LEVELPROGRESSTRACKER_API ULevelPreloadDatabaseLPT : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LPT")
	TArray<FLevelPreloadEntryLPT> Levels;

	/** Finds a level entry by level soft pointer. Returns nullptr when no entry exists. */
	const FLevelPreloadEntryLPT* FindEntryByLevel(const TSoftObjectPtr<UWorld>& Level) const;

	/** Finds a mutable level entry by level soft pointer. Returns nullptr when no entry exists. */
	FLevelPreloadEntryLPT* FindEntryByLevel(const TSoftObjectPtr<UWorld>& Level);

	/** Finds existing entry for level or creates one. Never allows duplicate entries for a level. */
	FLevelPreloadEntryLPT* FindOrAddEntryByLevel(const TSoftObjectPtr<UWorld>& Level, bool& bWasAdded);

	/** Removes invalid and duplicate collection references while preserving original order. */
	static void DeduplicateCollections(FLevelPreloadEntryLPT& Entry);
};

