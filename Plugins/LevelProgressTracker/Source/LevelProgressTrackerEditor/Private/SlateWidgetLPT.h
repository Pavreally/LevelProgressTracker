// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class ULevelPreloadDatabase;
class UWorld;

namespace SlateWidgetLPT
{
	bool PromptCreateLevelRules(bool& bApplyGlobalDefaults);

	void OpenLevelRulesWindow(
		ULevelPreloadDatabase* DatabaseAsset,
		const TSoftObjectPtr<UWorld>& LevelSoftPtr,
		const FString& LevelDisplayName,
		bool bIsWorldPartition,
		const TFunction<bool(ULevelPreloadDatabase*)>& SaveDatabaseAssetFn
	);
}

