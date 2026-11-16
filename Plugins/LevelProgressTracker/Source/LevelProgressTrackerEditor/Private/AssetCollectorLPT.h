// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "SettingsLPT.h"

class IAssetRegistry;
class ULevel;
class UWorld;

namespace AssetCollectorLPT
{
	void AppendFolderRuleCandidates(
		IAssetRegistry& Registry,
		const FLPTFilterSettings& Rules,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	);

	void AppendHardDependencyClosureAssets(
		IAssetRegistry& Registry,
		const TArray<FName>& RootPackageNames,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets,
		const FLPTFilterSettings* Rules = nullptr,
		const TSet<FName>* ExcludedPackageNames = nullptr,
		bool bStopAtNestedWorldPackages = false
	);

	void AppendExplicitAssetRuleCandidates(
		const FLPTFilterSettings& Rules,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	);

	void CollectLevelReferencedPackages(
		ULevel* Level,
		const TSet<FName>& ExcludedLevelPackageNames,
		TSet<FName>& OutReferencedPackageNames,
		int32& OutActorCount
	);

	void CollectWorldPartitionActorPackages(UWorld* World, const FLPTFilterSettings& Rules, TSet<FName>& InOutCandidateActorPackages);
}
