// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "SettingsLPT.h"

class UWorld;

namespace DataLayerResolverLPT
{
	void AddDataLayerNameWithVariants(FName InName, TArray<FName>& InOutNames);
	void ResolveWorldPartitionRegionRulesAsDataLayers(UWorld* World, FLPTLevelRules& InOutRules);
}

