// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "SettingsLPT.h"

class IAssetRegistry;
class UWorld;

namespace AssetCollectorLPT
{
	void AppendFolderRuleCandidates(
		IAssetRegistry& Registry,
		const FLPTLevelRules& Rules,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	);

	void AppendHardDependencyClosureAssets(
		IAssetRegistry& Registry,
		const TArray<FName>& RootPackageNames,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets,
		const FLPTLevelRules* Rules = nullptr
	);

	void AppendExplicitAssetRuleCandidates(
		const FLPTLevelRules& Rules,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	);

	void CollectWorldPartitionActorPackages(UWorld* World, const FLPTLevelRules& Rules, TSet<FName>& InOutCandidateActorPackages);
}

