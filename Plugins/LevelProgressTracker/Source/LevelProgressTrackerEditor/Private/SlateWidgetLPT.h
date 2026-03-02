// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UWorld;
class ULevelPreloadDatabaseLPT;

namespace SlateWidgetLPT
{
	void OpenLevelRulesWindow(
		ULevelPreloadDatabaseLPT* DatabaseAsset,
		const TSoftObjectPtr<UWorld>& LevelSoftPtr,
		const FString& LevelDisplayName,
		bool bIsWorldPartition,
		const TFunction<bool(ULevelPreloadDatabaseLPT*)>& SaveDatabaseAssetFn
	);
}

