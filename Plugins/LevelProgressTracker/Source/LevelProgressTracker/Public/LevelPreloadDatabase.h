// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

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
	TArray<TSoftObjectPtr<UObject>> Assets;
};

UCLASS(BlueprintType)
class LEVELPROGRESSTRACKER_API ULevelPreloadDatabase : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LPT")
	TArray<FLevelPreloadEntry> Levels;

	const FLevelPreloadEntry* FindEntry(const TSoftObjectPtr<UWorld>& Level) const;
	void UpdateEntry(const TSoftObjectPtr<UWorld>& Level, const TArray<FSoftObjectPath>& AssetPaths);
};
