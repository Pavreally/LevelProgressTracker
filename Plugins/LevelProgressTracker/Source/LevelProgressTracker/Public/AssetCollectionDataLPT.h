// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

#include "AssetCollectionDataLPT.generated.h"

class UWorld;

UCLASS(BlueprintType)
class LEVELPROGRESSTRACKER_API UAssetCollectionDataLPT : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Data")
	FName CollectionKey = FName(TEXT("Default"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Data")
	bool bAutoGenerate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Data")
	FGameplayTagContainer GroupTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Data", meta = (DisplayName = "Target Data Layers"))
	TArray<TSoftObjectPtr<UDataLayerAsset>> TargetDataLayers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Data", meta = (DisplayName = "Target Data Layer Names", AdvancedDisplay))
	TArray<FName> TargetDataLayerNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Data", meta = (DisplayName = "Target Cell Rules"))
	TArray<FString> TargetCellRules;

	UPROPERTY(VisibleAnywhere, Category = "Collection Data", meta = (DisplayName = "Target Sublevel"))
	TSoftObjectPtr<UWorld> TargetSublevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collection Data", meta = (DisplayName = "Resource References", ToolTip = "Soft object paths to resources collected for this level or collection."))
	TArray<FSoftObjectPath> AssetList;

	UPROPERTY(VisibleAnywhere, Category = "Collection Data", meta = (ToolTip = "Auto-generated checksum of collection content. Used to detect outdated preload lists. Not editable."))
	uint32 CollectionContentHash = 0;
};
