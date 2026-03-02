// Pavel Gornostaev <https://github.com/Pavreally>

#include "AssetFilterSettingsLPT.h"

FLPTFilterSettings UAssetFilterSettingsLPT::ToFilterSettings() const
{
	FLPTFilterSettings Result;
	Result.AssetClassFilter = AssetClassFilter;
	Result.bUseExclusionMode = bUseExclusionMode;
	Result.AssetRules = AssetRules;
	Result.FolderRules = FolderRules;
	Result.bUseChunkedPreload = bUseChunkedPreload;
	Result.PreloadChunkSize = FMath::Max(1, PreloadChunkSize);
	Result.bAllowWorldPartitionAutoScan = bAllowWorldPartitionAutoScan;
	Result.bAllowWorldPartitionUnscopedAutoScan = bAllowWorldPartitionUnscopedAutoScan;
	return Result;
}

void UAssetFilterSettingsLPT::InitializeDefaultsFromProjectSettings(const ULevelProgressTrackerSettings* Settings)
{
	if (!Settings)
	{
		return;
	}

	FLPTFilterSettings Defaults;
	Settings->BuildGlobalDefaultRules(Defaults);

	AssetClassFilter = Defaults.AssetClassFilter;
	bUseChunkedPreload = Defaults.bUseChunkedPreload;
	PreloadChunkSize = Defaults.PreloadChunkSize;
	bAllowWorldPartitionUnscopedAutoScan = Defaults.bAllowWorldPartitionUnscopedAutoScan;
}
