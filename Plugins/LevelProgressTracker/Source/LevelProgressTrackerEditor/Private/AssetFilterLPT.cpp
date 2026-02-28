// Pavel Gornostaev <https://github.com/Pavreally>

#include "AssetFilterLPT.h"

#include "AssetRegistry/AssetData.h"
#include "AssetUtilsLPT.h"
#include "Engine/DataAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Sound/SoundBase.h"

namespace AssetFilterLPT
{
	namespace
	{
		bool IsClassFilterPassThrough(const FLPTAssetClassFilter& ClassFilter)
		{
			return ClassFilter.bIncludeStaticMeshes &&
				ClassFilter.bIncludeSkeletalMeshes &&
				ClassFilter.bIncludeMaterials &&
				ClassFilter.bIncludeNiagara &&
				ClassFilter.bIncludeSounds &&
				ClassFilter.bIncludeWidgets &&
				ClassFilter.bIncludeDataAssets;
		}

		bool IsNiagaraAssetClass(const FTopLevelAssetPath& AssetClassPath)
		{
			return AssetClassPath.GetPackageName() == FName(TEXT("/Script/Niagara"));
		}

		bool IsWidgetAssetClass(const FTopLevelAssetPath& AssetClassPath)
		{
			if (AssetClassPath.GetPackageName() != FName(TEXT("/Script/UMGEditor")))
			{
				return false;
			}

			const FName ClassName = AssetClassPath.GetAssetName();
			return ClassName == FName(TEXT("WidgetBlueprint")) ||
				ClassName == FName(TEXT("WidgetBlueprintGeneratedClass")) ||
				ClassName == FName(TEXT("EditorUtilityWidgetBlueprint"));
		}

		bool IsMaterialRelatedAssetClass(const FTopLevelAssetPath& AssetClassPath)
		{
			if (AssetClassPath.GetPackageName() != FName(TEXT("/Script/Engine")))
			{
				return false;
			}

			const FName ClassName = AssetClassPath.GetAssetName();
			if (ClassName == FName(TEXT("Material")) ||
				ClassName == FName(TEXT("MaterialInstance")) ||
				ClassName == FName(TEXT("MaterialInstanceConstant")) ||
				ClassName == FName(TEXT("MaterialInstanceDynamic")) ||
				ClassName == FName(TEXT("MaterialFunction")) ||
				ClassName == FName(TEXT("MaterialFunctionInstance")) ||
				ClassName == FName(TEXT("MaterialFunctionMaterialLayer")) ||
				ClassName == FName(TEXT("MaterialFunctionMaterialLayerBlend")) ||
				ClassName == FName(TEXT("MaterialParameterCollection")))
			{
				return true;
			}

			return AssetClassPath.ToString().StartsWith(TEXT("/Script/Engine.Texture"));
		}
	}

	bool ShouldIncludeAssetByClass(const FAssetData& AssetData, const FLPTLevelRules* Rules)
	{
		if (!Rules)
		{
			return true;
		}

		const FLPTAssetClassFilter& ClassFilter = Rules->AssetClassFilter;
		if (IsClassFilterPassThrough(ClassFilter))
		{
			return true;
		}

		bool bMatchesTrackedCategory = false;
		bool bAllowed = true;

		if (UClass* AssetClass = AssetData.GetClass(EResolveClass::Yes))
		{
			if (AssetClass->IsChildOf(UStaticMesh::StaticClass()))
			{
				bMatchesTrackedCategory = true;
				bAllowed = ClassFilter.bIncludeStaticMeshes;
			}
			else if (AssetClass->IsChildOf(USkeletalMesh::StaticClass()))
			{
				bMatchesTrackedCategory = true;
				bAllowed = ClassFilter.bIncludeSkeletalMeshes;
			}
			else if (AssetClass->IsChildOf(UMaterialInterface::StaticClass()))
			{
				bMatchesTrackedCategory = true;
				bAllowed = ClassFilter.bIncludeMaterials;
			}
			else if (AssetClass->IsChildOf(UMaterialFunctionInterface::StaticClass()))
			{
				bMatchesTrackedCategory = true;
				bAllowed = ClassFilter.bIncludeMaterials;
			}
			else if (AssetClass->IsChildOf(UMaterialParameterCollection::StaticClass()))
			{
				bMatchesTrackedCategory = true;
				bAllowed = ClassFilter.bIncludeMaterials;
			}
			else if (AssetClass->IsChildOf(UTexture::StaticClass()))
			{
				bMatchesTrackedCategory = true;
				bAllowed = ClassFilter.bIncludeMaterials;
			}
			else if (AssetClass->IsChildOf(USoundBase::StaticClass()))
			{
				bMatchesTrackedCategory = true;
				bAllowed = ClassFilter.bIncludeSounds;
			}
			else if (AssetClass->IsChildOf(UDataAsset::StaticClass()))
			{
				bMatchesTrackedCategory = true;
				bAllowed = ClassFilter.bIncludeDataAssets;
			}
		}

		if (!bMatchesTrackedCategory && IsNiagaraAssetClass(AssetData.AssetClassPath))
		{
			bMatchesTrackedCategory = true;
			bAllowed = ClassFilter.bIncludeNiagara;
		}

		if (!bMatchesTrackedCategory && IsWidgetAssetClass(AssetData.AssetClassPath))
		{
			bMatchesTrackedCategory = true;
			bAllowed = ClassFilter.bIncludeWidgets;
		}

		if (!bMatchesTrackedCategory && IsMaterialRelatedAssetClass(AssetData.AssetClassPath))
		{
			bMatchesTrackedCategory = true;
			bAllowed = ClassFilter.bIncludeMaterials;
		}

		// When class filter is customized, treat it as a strict allow-list.
		return bMatchesTrackedCategory ? bAllowed : false;
	}

	TArray<FSoftObjectPath> MergeSoftObjectPaths(
		const TArray<FSoftObjectPath>& LevelPaths,
		const TArray<FSoftObjectPath>& GlobalPaths
	)
	{
		TArray<FSoftObjectPath> Merged;
		TSet<FSoftObjectPath> Unique;
		Merged.Reserve(LevelPaths.Num() + GlobalPaths.Num());

		auto AppendUnique = [&Merged, &Unique](const TArray<FSoftObjectPath>& Paths)
		{
			for (const FSoftObjectPath& Path : Paths)
			{
				if (!Path.IsValid() || Unique.Contains(Path))
				{
					continue;
				}

				Unique.Add(Path);
				Merged.Add(Path);
			}
		};

		AppendUnique(LevelPaths);
		AppendUnique(GlobalPaths);
		return Merged;
	}

	TArray<FDirectoryPath> MergeFolderPaths(
		const TArray<FDirectoryPath>& LevelPaths,
		const TArray<FDirectoryPath>& GlobalPaths
	)
	{
		TArray<FDirectoryPath> Merged;
		TSet<FString> Unique;
		Merged.Reserve(LevelPaths.Num() + GlobalPaths.Num());

		auto AppendUnique = [&Merged, &Unique](const TArray<FDirectoryPath>& Paths)
		{
			for (const FDirectoryPath& Path : Paths)
			{
				const FString NormalizedPath = AssetUtilsLPT::NormalizeFolderRuleForMerge(Path.Path);
				if (NormalizedPath.IsEmpty() || Unique.Contains(NormalizedPath))
				{
					continue;
				}

				Unique.Add(NormalizedPath);

				FDirectoryPath StoredPath;
				StoredPath.Path = NormalizedPath;
				Merged.Add(StoredPath);
			}
		};

		AppendUnique(LevelPaths);
		AppendUnique(GlobalPaths);
		return Merged;
	}

	TArray<FName> MergeNameRules(const TArray<FName>& LevelRules, const TArray<FName>& GlobalRules)
	{
		TArray<FName> Merged;
		TSet<FName> Unique;
		Merged.Reserve(LevelRules.Num() + GlobalRules.Num());

		auto AppendUnique = [&Merged, &Unique](const TArray<FName>& Rules)
		{
			for (const FName Rule : Rules)
			{
				if (Rule.IsNone() || Unique.Contains(Rule))
				{
					continue;
				}

				Unique.Add(Rule);
				Merged.Add(Rule);
			}
		};

		AppendUnique(LevelRules);
		AppendUnique(GlobalRules);
		return Merged;
	}

	TArray<TSoftObjectPtr<UDataLayerAsset>> MergeDataLayerAssetRules(
		const TArray<TSoftObjectPtr<UDataLayerAsset>>& LevelRules,
		const TArray<TSoftObjectPtr<UDataLayerAsset>>& GlobalRules
	)
	{
		TArray<TSoftObjectPtr<UDataLayerAsset>> Merged;
		TSet<FSoftObjectPath> Unique;
		Merged.Reserve(LevelRules.Num() + GlobalRules.Num());

		auto AppendUnique = [&Merged, &Unique](const TArray<TSoftObjectPtr<UDataLayerAsset>>& Rules)
		{
			for (const TSoftObjectPtr<UDataLayerAsset>& Rule : Rules)
			{
				const FSoftObjectPath RulePath = Rule.ToSoftObjectPath();
				if (!RulePath.IsValid() || Unique.Contains(RulePath))
				{
					continue;
				}

				Unique.Add(RulePath);
				Merged.Add(Rule);
			}
		};

		AppendUnique(LevelRules);
		AppendUnique(GlobalRules);
		return Merged;
	}

	TArray<FString> MergeStringRules(const TArray<FString>& LevelRules, const TArray<FString>& GlobalRules)
	{
		TArray<FString> Merged;
		TSet<FString> Unique;
		Merged.Reserve(LevelRules.Num() + GlobalRules.Num());

		auto AppendUnique = [&Merged, &Unique](const TArray<FString>& Rules)
		{
			for (const FString& Rule : Rules)
			{
				FString NormalizedRule = Rule;
				NormalizedRule.TrimStartAndEndInline();

				if (NormalizedRule.IsEmpty() || Unique.Contains(NormalizedRule))
				{
					continue;
				}

				Unique.Add(NormalizedRule);
				Merged.Add(NormalizedRule);
			}
		};

		AppendUnique(LevelRules);
		AppendUnique(GlobalRules);
		return Merged;
	}

	FLPTLevelRules BuildMergedRulesWithGlobalDominance(const FLPTLevelRules& LevelRules, const ULevelProgressTrackerSettings* Settings)
	{
		if (!Settings)
		{
			return LevelRules;
		}

		FLPTLevelRules GlobalRules;
		Settings->BuildGlobalDefaultRules(GlobalRules);

		FLPTLevelRules Merged = LevelRules;
		Merged.bRulesInitializedFromGlobalDefaults = true;

		// Conflict order is Level first, then Global. Global values dominate on conflicting options.
		Merged.AssetRules = MergeSoftObjectPaths(LevelRules.AssetRules, GlobalRules.AssetRules);
		Merged.FolderRules = MergeFolderPaths(LevelRules.FolderRules, GlobalRules.FolderRules);
		Merged.bUseChunkedPreload = GlobalRules.bUseChunkedPreload;
		Merged.PreloadChunkSize = FMath::Max(1, GlobalRules.PreloadChunkSize);
		Merged.AssetClassFilter = GlobalRules.AssetClassFilter;
		Merged.WorldPartitionDataLayerAssets = MergeDataLayerAssetRules(LevelRules.WorldPartitionDataLayerAssets, GlobalRules.WorldPartitionDataLayerAssets);
		Merged.WorldPartitionRegions = MergeNameRules(LevelRules.WorldPartitionRegions, GlobalRules.WorldPartitionRegions);
		Merged.WorldPartitionCells = MergeStringRules(LevelRules.WorldPartitionCells, GlobalRules.WorldPartitionCells);
		Merged.bUseExclusionMode = GlobalRules.bUseExclusionMode;
		Merged.bAllowWorldPartitionAutoScan = GlobalRules.bAllowWorldPartitionAutoScan;
		return Merged;
	}
}
