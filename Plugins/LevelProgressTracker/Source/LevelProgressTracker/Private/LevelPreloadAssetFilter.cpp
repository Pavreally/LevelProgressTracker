// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelPreloadAssetFilter.h"
#include "LevelProgressTrackerSettings.h"


namespace LevelPreloadAssetFilterPrivate
{
	static FString NormalizeFolderRule(const FString& InFolderPath)
	{
		FString FolderPath = InFolderPath;
		FolderPath.TrimStartAndEndInline();
		FolderPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (FolderPath.IsEmpty())
		{
			return FString();
		}

		while (FolderPath.EndsWith(TEXT("/")))
		{
			FolderPath.LeftChopInline(1, EAllowShrinking::No);
		}

		if (FolderPath.IsEmpty())
		{
			return FString();
		}

		if (FolderPath.StartsWith(TEXT("/")))
		{
			return FolderPath;
		}

		if (FolderPath.StartsWith(TEXT("Game/")))
		{
			return FString::Printf(TEXT("/%s"), *FolderPath);
		}

		return FString::Printf(TEXT("/Game/%s"), *FolderPath);
	}

	static bool MatchesAnyFolderRule(const FString& LongPackageName, const TArray<FString>& FolderRulePrefixes)
	{
		for (const FString& FolderPrefix : FolderRulePrefixes)
		{
			if (!FolderPrefix.IsEmpty() && LongPackageName.StartsWith(FolderPrefix))
			{
				return true;
			}
		}

		return false;
	}

	static bool MatchesAnyContainsRule(const FString& LongPackageName, const TArray<FString>& Rules)
	{
		for (const FString& Rule : Rules)
		{
			if (!Rule.IsEmpty() && LongPackageName.Contains(Rule))
			{
				return true;
			}
		}

		return false;
	}
}

TArray<FSoftObjectPath> ULevelPreloadAssetFilter::FilterAssets(const TArray<FSoftObjectPath>& InAssets, const FLPTLevelRules* Rules)
{
	TArray<FSoftObjectPath> Result;
	TSet<FSoftObjectPath> UniqueResult;

	if (InAssets.IsEmpty())
	{
		return Result;
	}

	if (!Rules)
	{
		Result.Reserve(InAssets.Num());
		for (const FSoftObjectPath& AssetPath : InAssets)
		{
			if (AssetPath.IsValid() && !UniqueResult.Contains(AssetPath))
			{
				UniqueResult.Add(AssetPath);
				Result.Add(AssetPath);
			}
		}
		return Result;
	}

	TSet<FString> AssetRuleLongPackageNames;
	AssetRuleLongPackageNames.Reserve(Rules->AssetRules.Num());

	for (const FSoftObjectPath& AssetRule : Rules->AssetRules)
	{
		const FString RuleLongPackageName = AssetRule.GetLongPackageName();
		if (!RuleLongPackageName.IsEmpty())
		{
			AssetRuleLongPackageNames.Add(RuleLongPackageName);
		}
	}

	TArray<FString> FolderRulePrefixes;
	FolderRulePrefixes.Reserve(Rules->FolderRules.Num());

	for (const FDirectoryPath& FolderRule : Rules->FolderRules)
	{
		const FString NormalizedRule = LevelPreloadAssetFilterPrivate::NormalizeFolderRule(FolderRule.Path);
		if (!NormalizedRule.IsEmpty())
		{
			FolderRulePrefixes.Add(NormalizedRule);
		}
	}

	TArray<FString> CellRules;
	CellRules.Reserve(Rules->WorldPartitionCells.Num());

	for (const FString& CellRule : Rules->WorldPartitionCells)
	{
		if (!CellRule.IsEmpty())
		{
			CellRules.Add(CellRule);
		}
	}

	TArray<FString> RegionRules;
	RegionRules.Reserve(Rules->WorldPartitionRegions.Num());

	for (const FName RegionRule : Rules->WorldPartitionRegions)
	{
		if (!RegionRule.IsNone())
		{
			RegionRules.Add(RegionRule.ToString());
		}
	}

	const bool bHasAssetOrFolderRules = AssetRuleLongPackageNames.Num() > 0 || FolderRulePrefixes.Num() > 0;

	if (!Rules->bUseExclusionMode && !bHasAssetOrFolderRules)
	{
		// In inclusion mode with no asset/folder rules, keep all incoming candidates.
		// For World Partition, actor/cell/region scoping can already be applied before this call.
		Result.Reserve(InAssets.Num());
		for (const FSoftObjectPath& AssetPath : InAssets)
		{
			if (AssetPath.IsValid() && !UniqueResult.Contains(AssetPath))
			{
				UniqueResult.Add(AssetPath);
				Result.Add(AssetPath);
			}
		}
		return Result;
	}

	Result.Reserve(InAssets.Num());

	for (const FSoftObjectPath& AssetPath : InAssets)
	{
		if (!AssetPath.IsValid())
		{
			continue;
		}

		const FString AssetLongPackageName = AssetPath.GetLongPackageName();
		if (AssetLongPackageName.IsEmpty())
		{
			continue;
		}

		const bool bMatchesAssetRule = AssetRuleLongPackageNames.Contains(AssetLongPackageName);
		const bool bMatchesFolderRule = LevelPreloadAssetFilterPrivate::MatchesAnyFolderRule(AssetLongPackageName, FolderRulePrefixes);
		const bool bMatchesCellRule = LevelPreloadAssetFilterPrivate::MatchesAnyContainsRule(AssetLongPackageName, CellRules);
		const bool bMatchesRegionRule = LevelPreloadAssetFilterPrivate::MatchesAnyContainsRule(AssetLongPackageName, RegionRules);
		const bool bMatchesAnyRule = bMatchesAssetRule || bMatchesFolderRule || bMatchesCellRule || bMatchesRegionRule;

		const bool bShouldInclude = Rules->bUseExclusionMode ? !bMatchesAnyRule : bMatchesAnyRule;
		if (!bShouldInclude || UniqueResult.Contains(AssetPath))
		{
			continue;
		}

		UniqueResult.Add(AssetPath);
		Result.Add(AssetPath);
	}

	return Result;
}

bool ULevelPreloadAssetFilter::ShouldIncludeWorldPartitionActor(
	const FSoftObjectPath& ActorPath,
	const TArray<FName>& ActorRegionNames,
	const FLPTLevelRules* Rules
)
{
	if (!ActorPath.IsValid())
	{
		return false;
	}

	if (!Rules)
	{
		return true;
	}

	const FString ActorLongPackageName = ActorPath.GetLongPackageName();
	if (ActorLongPackageName.IsEmpty())
	{
		return false;
	}

	bool bIsIncluded = true;

	if (Rules->WorldPartitionRegions.Num() > 0)
	{
		bool bRegionMatched = false;

		for (const FName RegionRule : Rules->WorldPartitionRegions)
		{
			if (RegionRule.IsNone())
			{
				continue;
			}

			if (ActorRegionNames.Contains(RegionRule) || ActorLongPackageName.Contains(RegionRule.ToString()))
			{
				bRegionMatched = true;
				break;
			}
		}

		bIsIncluded = Rules->bUseExclusionMode ? !bRegionMatched : bRegionMatched;
	}

	if (!bIsIncluded)
	{
		return false;
	}

	if (Rules->WorldPartitionCells.Num() > 0)
	{
		bool bCellMatched = false;

		for (const FString& CellRule : Rules->WorldPartitionCells)
		{
			if (!CellRule.IsEmpty() && ActorLongPackageName.Contains(CellRule))
			{
				bCellMatched = true;
				break;
			}
		}

		bIsIncluded = Rules->bUseExclusionMode ? !bCellMatched : bCellMatched;
	}

	return bIsIncluded;
}

bool ULevelPreloadAssetFilter::HasAnyAssetOrFolderRule(const FLPTLevelRules* Rules)
{
	return Rules ? (Rules->AssetRules.Num() > 0 || Rules->FolderRules.Num() > 0) : false;
}

bool ULevelPreloadAssetFilter::HasAnyRule(const FLPTLevelRules* Rules)
{
	return Rules ? (
		Rules->AssetRules.Num() > 0 ||
		Rules->FolderRules.Num() > 0 ||
		Rules->WorldPartitionCells.Num() > 0 ||
		Rules->WorldPartitionRegions.Num() > 0
	) : false;
}

bool ULevelPreloadAssetFilter::ResolveDatabaseAssetPath(
	const ULevelProgressTrackerSettings* Settings,
	FString& OutDatabasePackagePath,
	FSoftObjectPath& OutDatabaseObjectPath
)
{
	if (!Settings)
	{
		return false;
	}

	FString DatabaseFolderLongPath;
	return Settings->ResolveDatabaseAssetPaths(DatabaseFolderLongPath, OutDatabasePackagePath, OutDatabaseObjectPath);
}
