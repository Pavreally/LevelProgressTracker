// Pavel Gornostaev <https://github.com/Pavreally>

#include "AssetCollectorLPT.h"

#include "AssetFilterLPT.h"
#include "AssetUtilsLPT.h"
#include "DataLayerResolverLPT.h"
#include "LevelPreloadAssetFilter.h"
#include "LogLPTEditor.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/ActorComponent.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"

namespace AssetCollectorLPT
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

		void AddReferencedObjectPackage(
			const UObject* ReferencedObject,
			const TSet<FName>& ExcludedLevelPackageNames,
			TSet<FName>& OutReferencedPackageNames
		)
		{
			if (!IsValid(ReferencedObject))
			{
				return;
			}

			const UPackage* Package = ReferencedObject->GetOutermost();
			if (!Package)
			{
				return;
			}

			const FString PackageLongName = UWorld::RemovePIEPrefix(Package->GetName());
			if (PackageLongName.IsEmpty() ||
				AssetUtilsLPT::IsEngineOrScriptPackage(PackageLongName) ||
				AssetUtilsLPT::IsWorldPartitionExternalPackage(PackageLongName))
			{
				return;
			}

			const FName PackageName(*PackageLongName);
			if (ExcludedLevelPackageNames.Contains(PackageName))
			{
				return;
			}

			OutReferencedPackageNames.Add(PackageName);
		}

		void CollectDirectReferencePackages(
			UObject* Referencer,
			const TSet<FName>& ExcludedLevelPackageNames,
			TSet<FName>& OutReferencedPackageNames
		)
		{
			if (!IsValid(Referencer))
			{
				return;
			}

			// Inspect serialized instance/component properties only. Adding the owning
			// Blueprint class package here would traverse graph-only references such as
			// casts to the player class and pull in that class's unrelated dependencies.
			TArray<UObject*> ReferencedObjects;
			FReferenceFinder ReferenceFinder(ReferencedObjects, nullptr, false, true, false, true);
			ReferenceFinder.FindReferences(Referencer);

			for (UObject* ReferencedObject : ReferencedObjects)
			{
				AddReferencedObjectPackage(ReferencedObject, ExcludedLevelPackageNames, OutReferencedPackageNames);
			}
		}

		void AddFallbackAssetFromPackagePath(
			const FString& PackageLongPath,
			TSet<FSoftObjectPath>& UniquePaths,
			TArray<FSoftObjectPath>& OutAssets
		)
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(PackageLongPath);
			if (AssetName.IsEmpty())
			{
				return;
			}

			const FSoftObjectPath FallbackPath(FString::Printf(TEXT("%s.%s"), *PackageLongPath, *AssetName));
			if (FallbackPath.IsValid() && !UniquePaths.Contains(FallbackPath))
			{
				UniquePaths.Add(FallbackPath);
				OutAssets.Add(FallbackPath);
			}
		}

		bool AppendAssetsFromPackage(
			IAssetRegistry& Registry,
			const FName PackageName,
			TSet<FSoftObjectPath>& UniquePaths,
			TArray<FSoftObjectPath>& OutAssets,
			const FLPTFilterSettings* Rules = nullptr
		)
		{
			const FString PackageLongPath = PackageName.ToString();
			if (AssetUtilsLPT::IsEngineOrScriptPackage(PackageLongPath) ||
				AssetUtilsLPT::IsWorldPartitionExternalPackage(PackageLongPath))
			{
				// World Partition External Actor/Object packages are traversal roots,
				// not preload content assets. Their dependencies are still traversed
				// by AppendHardDependencyClosureAssets().
				return false;
			}

			TArray<FAssetData> PackageAssets;
			Registry.GetAssetsByPackageName(PackageName, PackageAssets, true);

			if (PackageAssets.IsEmpty())
			{
				// Fallback object paths have no class metadata. Keep historical behavior only
				// when class filter is fully open; otherwise strict class filtering must win.
				if (!Rules || IsClassFilterPassThrough(Rules->AssetClassFilter))
				{
					AddFallbackAssetFromPackagePath(PackageLongPath, UniquePaths, OutAssets);
				}
				return false;
			}

			bool bContainsWorldAsset = false;
			for (const FAssetData& AssetData : PackageAssets)
			{
				if (!AssetData.IsValid() || AssetData.HasAnyPackageFlags(PKG_EditorOnly))
				{
					continue;
				}

				// World assets are level containers and should not be part of preload content candidates.
				// Detect map packages before class filtering so they remain traversal boundaries
				// even when the user's class filter excludes UWorld assets.
				if (AssetData.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
				{
					bContainsWorldAsset = true;
					continue;
				}

				if (!AssetFilterLPT::ShouldIncludeAssetByClass(AssetData, Rules))
				{
					continue;
				}

				const FSoftObjectPath AssetPath = AssetData.GetSoftObjectPath();
				if (!AssetPath.IsValid())
				{
					continue;
				}

				const FString AssetLongPackageName = AssetPath.GetLongPackageName();
				if (AssetUtilsLPT::IsEngineOrScriptPackage(AssetLongPackageName) || UniquePaths.Contains(AssetPath))
				{
					continue;
				}

				UniquePaths.Add(AssetPath);
				OutAssets.Add(AssetPath);
			}
			return bContainsWorldAsset;
		}

		void AppendDirectDependenciesAssets(
			IAssetRegistry& Registry,
			const FName RootPackageName,
			TSet<FSoftObjectPath>& UniquePaths,
			TArray<FSoftObjectPath>& OutAssets,
			const FLPTFilterSettings* Rules = nullptr
		)
		{
			TArray<FName> Dependencies;
			Registry.GetDependencies(
				RootPackageName,
				Dependencies,
				UE::AssetRegistry::EDependencyCategory::Package,
				UE::AssetRegistry::FDependencyQuery()
			);

			for (const FName DependencyPackageName : Dependencies)
			{
				AppendAssetsFromPackage(Registry, DependencyPackageName, UniquePaths, OutAssets, Rules);
			}
		}

		void AppendHardDependenciesAssets(
			IAssetRegistry& Registry,
			const FName RootPackageName,
			TSet<FSoftObjectPath>& UniquePaths,
			TArray<FSoftObjectPath>& OutAssets
		)
		{
			TArray<FName> Dependencies;
			Registry.GetDependencies(
				RootPackageName,
				Dependencies,
				UE::AssetRegistry::EDependencyCategory::Package,
				UE::AssetRegistry::EDependencyQuery::Hard
			);

			for (const FName DependencyPackageName : Dependencies)
			{
				AppendAssetsFromPackage(Registry, DependencyPackageName, UniquePaths, OutAssets);
			}
		}
	}

	void CollectLevelReferencedPackages(
		ULevel* Level,
		const TSet<FName>& ExcludedLevelPackageNames,
		TSet<FName>& OutReferencedPackageNames,
		int32& OutActorCount
	)
	{
		OutActorCount = 0;
		if (!Level)
		{
			return;
		}

		TSet<const AActor*> VisitedActors;
		auto CollectActorReferences = [&](AActor* Actor)
		{
			if (!IsValid(Actor) || Actor->IsTemplate() || Actor->IsA<AWorldSettings>() || VisitedActors.Contains(Actor))
			{
				return;
			}

			VisitedActors.Add(Actor);
			++OutActorCount;
			CollectDirectReferencePackages(Actor, ExcludedLevelPackageNames, OutReferencedPackageNames);

			TInlineComponentArray<UActorComponent*> Components(Actor);
			for (UActorComponent* Component : Components)
			{
				CollectDirectReferencePackages(Component, ExcludedLevelPackageNames, OutReferencedPackageNames);
			}
		};

		for (AActor* Actor : Level->Actors)
		{
			CollectActorReferences(Actor);
		}

		CollectActorReferences(Level->GetLevelScriptActor());
	}

	void AppendFolderRuleCandidates(
		IAssetRegistry& Registry,
		const FLPTFilterSettings& Rules,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	)
	{
		for (const FDirectoryPath& FolderRule : Rules.FolderRules)
		{
			const FString NormalizedFolderPath = AssetUtilsLPT::NormalizeFolderRuleForMerge(FolderRule.Path);
			if (NormalizedFolderPath.IsEmpty() || AssetUtilsLPT::IsEngineOrScriptPackage(NormalizedFolderPath))
			{
				continue;
			}

			TArray<FAssetData> FolderAssets;
			Registry.GetAssetsByPath(FName(*NormalizedFolderPath), FolderAssets, true, true);

			for (const FAssetData& AssetData : FolderAssets)
			{
				if (!AssetData.IsValid() || AssetData.HasAnyPackageFlags(PKG_EditorOnly))
				{
					continue;
				}

				if (!AssetFilterLPT::ShouldIncludeAssetByClass(AssetData, &Rules))
				{
					continue;
				}

				if (AssetData.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
				{
					continue;
				}

				const FSoftObjectPath AssetPath = AssetData.GetSoftObjectPath();
				if (!AssetPath.IsValid())
				{
					continue;
				}

				const FString AssetLongPackageName = AssetPath.GetLongPackageName();
				if (AssetLongPackageName.IsEmpty() ||
					AssetUtilsLPT::IsEngineOrScriptPackage(AssetLongPackageName) ||
					AssetUtilsLPT::IsWorldPartitionExternalPackage(AssetLongPackageName) ||
					UniquePaths.Contains(AssetPath))
				{
					continue;
				}

				UniquePaths.Add(AssetPath);
				OutAssets.Add(AssetPath);
			}
		}
	}

	void AppendHardDependencyClosureAssets(
		IAssetRegistry& Registry,
		const TArray<FName>& RootPackageNames,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets,
		const FLPTFilterSettings* Rules,
		const TSet<FName>* ExcludedPackageNames,
		const bool bStopAtNestedWorldPackages
	)
	{
		TSet<FName> VisitedPackages;
		TSet<FName> RootPackages;
		RootPackages.Reserve(RootPackageNames.Num());
		for (const FName RootPackageName : RootPackageNames)
		{
			RootPackages.Add(RootPackageName);
		}

		TArray<FName> PendingPackages = RootPackageNames;

		while (PendingPackages.Num() > 0)
		{
			const FName CurrentPackageName = PendingPackages.Pop(EAllowShrinking::No);
			if (CurrentPackageName.IsNone() || VisitedPackages.Contains(CurrentPackageName) ||
				(ExcludedPackageNames && ExcludedPackageNames->Contains(CurrentPackageName)))
			{
				continue;
			}

			VisitedPackages.Add(CurrentPackageName);
			const bool bIsWorldPackage = AppendAssetsFromPackage(Registry, CurrentPackageName, UniquePaths, OutAssets, Rules);
			if (bStopAtNestedWorldPackages && bIsWorldPackage && !RootPackages.Contains(CurrentPackageName))
			{
				continue;
			}

			TArray<FName> Dependencies;
			Registry.GetDependencies(
				CurrentPackageName,
				Dependencies,
				UE::AssetRegistry::EDependencyCategory::Package,
				UE::AssetRegistry::EDependencyQuery::Hard
			);

			for (const FName DependencyPackageName : Dependencies)
			{
				if (!DependencyPackageName.IsNone() && !VisitedPackages.Contains(DependencyPackageName) &&
					(!ExcludedPackageNames || !ExcludedPackageNames->Contains(DependencyPackageName)))
				{
					PendingPackages.Add(DependencyPackageName);
				}
			}
		}
	}

	void AppendExplicitAssetRuleCandidates(
		const FLPTFilterSettings& Rules,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	)
	{
		for (const FSoftObjectPath& RuleAssetPath : Rules.AssetRules)
		{
			if (!RuleAssetPath.IsValid())
			{
				continue;
			}

			const FString RuleLongPackageName = RuleAssetPath.GetLongPackageName();
			if (RuleLongPackageName.IsEmpty() || AssetUtilsLPT::IsEngineOrScriptPackage(RuleLongPackageName) || UniquePaths.Contains(RuleAssetPath))
			{
				continue;
			}

			UniquePaths.Add(RuleAssetPath);
			OutAssets.Add(RuleAssetPath);
		}
	}

	void CollectWorldPartitionActorPackages(UWorld* World, const FLPTFilterSettings& Rules, TSet<FName>& InOutCandidateActorPackages)
	{
		if (!World)
		{
			return;
		}

		UWorldPartition* WorldPartition = World->GetWorldPartition();
		if (!WorldPartition)
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("World Partition is unavailable for '%s'. ActorDesc scan skipped."),
				*World->GetOutermost()->GetName()
			);
			return;
		}

		FLPTFilterSettings NormalizedRules = Rules;
		if (NormalizedRules.WorldPartitionRegions.Num() > 0)
		{
			TArray<FName> ExpandedRegionRules;
			ExpandedRegionRules.Reserve(NormalizedRules.WorldPartitionRegions.Num() * 4);
			for (const FName RegionRule : NormalizedRules.WorldPartitionRegions)
			{
				DataLayerResolverLPT::AddDataLayerNameWithVariants(RegionRule, ExpandedRegionRules);
			}
			NormalizedRules.WorldPartitionRegions = MoveTemp(ExpandedRegionRules);
		}

		// Data Layer/Cell rules define actor scan scope for WP regardless of asset include/exclude mode.
		// Exclusion mode is applied later only to asset/folder rules on collected candidates.
		FLPTFilterSettings ActorScopeRules = NormalizedRules;
		ActorScopeRules.bUseExclusionMode = false;

		FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, [&InOutCandidateActorPackages, &ActorScopeRules](const FWorldPartitionActorDescInstance* ActorDescInstance)
		{
			if (!ActorDescInstance)
			{
				return true;
			}

			TArray<FName> ActorDataLayerNamesForFilter;
			const TArray<FName> ResolvedInstanceNames = ActorDescInstance->GetDataLayerInstanceNames().ToArray();
			ActorDataLayerNamesForFilter.Reserve(ResolvedInstanceNames.Num() * 4);
			for (const FName InstanceName : ResolvedInstanceNames)
			{
				DataLayerResolverLPT::AddDataLayerNameWithVariants(InstanceName, ActorDataLayerNamesForFilter);
			}

			// Fallback when resolved Data Layer instance names are unavailable in current editor state.
			const TArray<FName> RawActorDataLayers = ActorDescInstance->GetDataLayers();
			for (const FName RawDataLayerName : RawActorDataLayers)
			{
				DataLayerResolverLPT::AddDataLayerNameWithVariants(RawDataLayerName, ActorDataLayerNamesForFilter);
			}

			const FName ActorPackageName = ActorDescInstance->GetActorPackage();
			const FString ActorPackagePath = ActorPackageName.ToString();
			FSoftObjectPath ActorObjectPath = ActorDescInstance->GetActorSoftPath();
			if (!ActorObjectPath.IsValid())
			{
				// Use a stable synthetic object name to keep package-based filtering functional
				// even when ActorSoftPath is unresolved in World Partition metadata.
				if (!ActorPackagePath.IsEmpty())
				{
					ActorObjectPath = FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *ActorPackagePath, TEXT("LPT_Actor")));
				}
			}

			if (!ULevelPreloadAssetFilter::ShouldIncludeWorldPartitionActor(ActorObjectPath, ActorDataLayerNamesForFilter, &ActorScopeRules))
			{
				return true;
			}

			if (!ActorPackageName.IsNone())
			{
				InOutCandidateActorPackages.Add(ActorPackageName);
			}

			return true;
		});
	}
}

