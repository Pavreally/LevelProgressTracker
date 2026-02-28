// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SettingsLPT.h"

#include "LevelPreloadDatabase.generated.h"

class UWorld;

USTRUCT(BlueprintType)
struct FLevelPreloadEntry
{
	GENERATED_BODY()

public:
	/* Target level for this preload entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LPT")
	TSoftObjectPtr<UWorld> Level;

	/* UTC timestamp when this entry was generated or updated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LPT")
	FDateTime GenerationTimestamp;

	/* Assets that will be preloaded before opening the level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LPT")
	TArray<FSoftObjectPath> Assets;

	/* Rules used when generating preload assets for this level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LPT")
	FLPTLevelRules Rules;

	/* Legacy mirror of Rules.bRulesInitializedFromGlobalDefaults kept for backward compatibility. */
	UPROPERTY()
	bool bRulesInitializedFromGlobalDefaults = false;
};

UCLASS(BlueprintType)
class LEVELPROGRESSTRACKER_API ULevelPreloadDatabase : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LPT")
	TArray<FLevelPreloadEntry> Levels;

	/** Finds a level entry by level soft pointer. Returns nullptr when no entry exists. */
	const FLevelPreloadEntry* FindEntryByLevel(const TSoftObjectPtr<UWorld>& Level) const;

	/** Finds a mutable level entry by level soft pointer. Returns nullptr when no entry exists. */
	FLevelPreloadEntry* FindEntryByLevel(const TSoftObjectPtr<UWorld>& Level);

	/** Finds existing entry for level or creates one. Never allows duplicate entries for a level. */
	FLevelPreloadEntry* FindOrAddEntryByLevel(const TSoftObjectPtr<UWorld>& Level, bool& bWasAdded);

	/** Updates level entry assets with deduplication and refreshes generation timestamp. */
	bool UpdateEntryAssetsByLevel(const TSoftObjectPtr<UWorld>& Level, const TArray<FSoftObjectPath>& AssetPaths);
};

