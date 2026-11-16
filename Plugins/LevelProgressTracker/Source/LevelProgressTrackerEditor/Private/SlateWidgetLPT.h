// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UWorld;
class ULevelPreloadDatabaseLPT;
class UAssetFilterSettingsLPT;

namespace SlateWidgetLPT
{
	bool TryActivateOpenLevelRulesWindow();

	void OpenLevelRulesWindow(
		ULevelPreloadDatabaseLPT* DatabaseAsset,
		UAssetFilterSettingsLPT* FilterSettingsAsset,
		const FString& LevelDisplayName,
		bool bIsWorldPartition,
		bool bHasSublevels,
		const TFunction<bool(ULevelPreloadDatabaseLPT*, UAssetFilterSettingsLPT*)>& SaveAndGenerateFn,
		const TFunction<bool(UAssetFilterSettingsLPT*)>& AddAllDataLayersFn,
		const TFunction<bool(UAssetFilterSettingsLPT*)>& ConfigureSublevelsFn
	);
}
