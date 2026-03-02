// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

#include "AssetCollectionDataLPT.generated.h"

UCLASS(BlueprintType)
class LEVELPROGRESSTRACKER_API UAssetCollectionDataLPT : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset List and Layers")
	FName CollectionKey = FName(TEXT("Default"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset List and Layers")
	bool bAutoGenerate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset List and Layers")
	FGameplayTagContainer GroupTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset List and Layers", meta = (DisplayName = "Target Data Layers"))
	TArray<TSoftObjectPtr<UDataLayerAsset>> TargetDataLayers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset List and Layers", meta = (DisplayName = "Target Data Layer Names", AdvancedDisplay))
	TArray<FName> TargetDataLayerNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset List and Layers", meta = (DisplayName = "Target Cell Rules"))
	TArray<FString> TargetCellRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset List and Layers")
	TArray<FSoftObjectPath> AssetList;

	UPROPERTY(VisibleAnywhere, Category = "Asset List and Layers", meta = (ToolTip = "Auto-generated checksum of collection content. Used to detect outdated preload lists. Not editable."))
	uint32 CollectionContentHash = 0;
};
