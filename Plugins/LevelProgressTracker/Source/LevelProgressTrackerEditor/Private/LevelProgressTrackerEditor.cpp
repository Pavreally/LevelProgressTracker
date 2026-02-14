// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerEditor.h"
#include "LevelPreloadAssetFilter.h"
#include "LevelPreloadDatabase.h"
#include "LevelProgressTrackerSettings.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "Editor.h"
#include "Engine/DataAsset.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "IStructureDetailsView.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Sound/SoundBase.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"


namespace LevelProgressTrackerEditorPrivate
{
	static const FName StyleSetName(TEXT("LevelProgressTrackerStyle"));
	static const FName ToolbarIconName(TEXT("LevelProgressTracker.LPTRules"));

	static bool IsEngineOrScriptPackage(const FString& LongPackageName)
	{
		return LongPackageName.StartsWith(TEXT("/Engine/")) || LongPackageName.StartsWith(TEXT("/Script/"));
	}

	static FString NormalizeFolderRuleForMerge(const FString& InFolderPath);

	static bool IsNiagaraAssetClass(const FTopLevelAssetPath& AssetClassPath)
	{
		return AssetClassPath.GetPackageName() == FName(TEXT("/Script/Niagara"));
	}

	static bool ShouldIncludeAssetByClass(const FAssetData& AssetData, const FLPTLevelRules* Rules)
	{
		if (!Rules)
		{
			return true;
		}

		const FLPTAssetClassFilter& ClassFilter = Rules->AssetClassFilter;
		if (ClassFilter.bIncludeStaticMeshes &&
			ClassFilter.bIncludeSkeletalMeshes &&
			ClassFilter.bIncludeMaterials &&
			ClassFilter.bIncludeNiagara &&
			ClassFilter.bIncludeSounds &&
			ClassFilter.bIncludeDataAssets)
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

		// Unknown classes are not force-filtered to preserve backward-compatible behavior.
		return !bMatchesTrackedCategory || bAllowed;
	}

	static FString BuildWorldPartitionExternalPackagePrefix(const FString& WorldPackagePath, const TCHAR* ExternalFolderName)
	{
		if (!WorldPackagePath.StartsWith(TEXT("/")) || !ExternalFolderName || !*ExternalFolderName)
		{
			return FString();
		}

		const int32 MountRootEnd = WorldPackagePath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 1);
		if (MountRootEnd == INDEX_NONE)
		{
			return FString();
		}

		const FString MountRoot = WorldPackagePath.Left(MountRootEnd + 1); // "/PluginOrGame/"
		const FString RelativeWorldPath = WorldPackagePath.RightChop(MountRootEnd + 1);
		if (RelativeWorldPath.IsEmpty())
		{
			return FString();
		}

		return FString::Printf(TEXT("%s%s/%s/"), *MountRoot, ExternalFolderName, *RelativeWorldPath);
	}

	static bool IsExternalPackageOfWorldPartitionLevel(const FString& SavedPackageName, const UWorld* EditorWorld)
	{
		if (!EditorWorld)
		{
			return false;
		}

		const UPackage* WorldPackage = EditorWorld->GetOutermost();
		if (!WorldPackage)
		{
			return false;
		}

		const FString WorldPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());
		const FString NormalizedSavedPackageName = UWorld::RemovePIEPrefix(SavedPackageName);
		if (WorldPackagePath.IsEmpty() || NormalizedSavedPackageName.IsEmpty())
		{
			return false;
		}

		const FString ExternalActorsPrefix = BuildWorldPartitionExternalPackagePrefix(WorldPackagePath, TEXT("__ExternalActors__"));
		if (!ExternalActorsPrefix.IsEmpty() && NormalizedSavedPackageName.StartsWith(ExternalActorsPrefix))
		{
			return true;
		}

		const FString ExternalObjectsPrefix = BuildWorldPartitionExternalPackagePrefix(WorldPackagePath, TEXT("__ExternalObjects__"));
		if (!ExternalObjectsPrefix.IsEmpty() && NormalizedSavedPackageName.StartsWith(ExternalObjectsPrefix))
		{
			return true;
		}

		return false;
	}

	static void AddDataLayerNameWithVariants(const FName InName, TArray<FName>& InOutNames)
	{
		if (InName.IsNone())
		{
			return;
		}

		auto AddUniqueName = [&InOutNames](const FName NameToAdd)
		{
			if (!NameToAdd.IsNone())
			{
				InOutNames.AddUnique(NameToAdd);
			}
		};

		AddUniqueName(InName);

		FString NameString = InName.ToString();
		if (NameString.IsEmpty())
		{
			return;
		}

		// Keep both full and short forms because Data Layer names can be represented
		// differently between actor descriptors and rule sources.
		auto AddShortVariant = [&AddUniqueName](const FString& Candidate)
		{
			if (!Candidate.IsEmpty())
			{
				AddUniqueName(FName(*Candidate));
			}
		};

		AddShortVariant(NameString);

		const int32 LastSlash = NameString.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastSlash != INDEX_NONE)
		{
			AddShortVariant(NameString.RightChop(LastSlash + 1));
		}

		const int32 LastDot = NameString.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastDot != INDEX_NONE)
		{
			AddShortVariant(NameString.RightChop(LastDot + 1));
		}

		const int32 LastColon = NameString.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastColon != INDEX_NONE)
		{
			AddShortVariant(NameString.RightChop(LastColon + 1));
		}
	}

	static void AddFallbackAssetFromPackagePath(
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

	static void AppendAssetsFromPackage(
		IAssetRegistry& Registry,
		const FName PackageName,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets,
		const FLPTLevelRules* Rules = nullptr
	)
	{
		const FString PackageLongPath = PackageName.ToString();
		if (IsEngineOrScriptPackage(PackageLongPath))
		{
			return;
		}

		TArray<FAssetData> PackageAssets;
		Registry.GetAssetsByPackageName(PackageName, PackageAssets, true);

		if (PackageAssets.IsEmpty())
		{
			AddFallbackAssetFromPackagePath(PackageLongPath, UniquePaths, OutAssets);
			return;
		}

		for (const FAssetData& AssetData : PackageAssets)
		{
			if (!AssetData.IsValid() || AssetData.HasAnyPackageFlags(PKG_EditorOnly))
			{
				continue;
			}

			if (!ShouldIncludeAssetByClass(AssetData, Rules))
			{
				continue;
			}

			// World assets are level containers and should not be part of preload content candidates.
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
			if (IsEngineOrScriptPackage(AssetLongPackageName) || UniquePaths.Contains(AssetPath))
			{
				continue;
			}

			UniquePaths.Add(AssetPath);
			OutAssets.Add(AssetPath);
		}
	}

	static void AppendFolderRuleCandidates(
		IAssetRegistry& Registry,
		const FLPTLevelRules& Rules,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets
	)
	{
		for (const FDirectoryPath& FolderRule : Rules.FolderRules)
		{
			const FString NormalizedFolderPath = NormalizeFolderRuleForMerge(FolderRule.Path);
			if (NormalizedFolderPath.IsEmpty() || IsEngineOrScriptPackage(NormalizedFolderPath))
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

				if (!ShouldIncludeAssetByClass(AssetData, &Rules))
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
				if (AssetLongPackageName.IsEmpty() || IsEngineOrScriptPackage(AssetLongPackageName) || UniquePaths.Contains(AssetPath))
				{
					continue;
				}

				UniquePaths.Add(AssetPath);
				OutAssets.Add(AssetPath);
			}
		}
	}

	static void AppendDirectDependenciesAssets(
		IAssetRegistry& Registry,
		const FName RootPackageName,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets,
		const FLPTLevelRules* Rules = nullptr
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

	static void AppendHardDependencyClosureAssets(
		IAssetRegistry& Registry,
		const TArray<FName>& RootPackageNames,
		TSet<FSoftObjectPath>& UniquePaths,
		TArray<FSoftObjectPath>& OutAssets,
		const FLPTLevelRules* Rules = nullptr
	)
	{
		TSet<FName> VisitedPackages;
		TArray<FName> PendingPackages = RootPackageNames;

		while (PendingPackages.Num() > 0)
		{
			const FName CurrentPackageName = PendingPackages.Pop(EAllowShrinking::No);
			if (CurrentPackageName.IsNone() || VisitedPackages.Contains(CurrentPackageName))
			{
				continue;
			}

			VisitedPackages.Add(CurrentPackageName);
			AppendAssetsFromPackage(Registry, CurrentPackageName, UniquePaths, OutAssets, Rules);

			TArray<FName> Dependencies;
			Registry.GetDependencies(
				CurrentPackageName,
				Dependencies,
				UE::AssetRegistry::EDependencyCategory::Package,
				UE::AssetRegistry::EDependencyQuery::Hard
			);

			for (const FName DependencyPackageName : Dependencies)
			{
				if (!DependencyPackageName.IsNone() && !VisitedPackages.Contains(DependencyPackageName))
				{
					PendingPackages.Add(DependencyPackageName);
				}
			}
		}
	}

	static void AppendHardDependenciesAssets(
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

	static void AppendExplicitAssetRuleCandidates(
		const FLPTLevelRules& Rules,
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
			if (RuleLongPackageName.IsEmpty() || IsEngineOrScriptPackage(RuleLongPackageName) || UniquePaths.Contains(RuleAssetPath))
			{
				continue;
			}

			UniquePaths.Add(RuleAssetPath);
			OutAssets.Add(RuleAssetPath);
		}
	}

	static FString NormalizeFolderRuleForMerge(const FString& InFolderPath)
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

	static TArray<FSoftObjectPath> MergeSoftObjectPaths(
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

	static TArray<FDirectoryPath> MergeFolderPaths(
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
				const FString NormalizedPath = NormalizeFolderRuleForMerge(Path.Path);
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

	static TArray<FName> MergeNameRules(const TArray<FName>& LevelRules, const TArray<FName>& GlobalRules)
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

	static TArray<TSoftObjectPtr<UDataLayerAsset>> MergeDataLayerAssetRules(
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

	static TArray<FString> MergeStringRules(const TArray<FString>& LevelRules, const TArray<FString>& GlobalRules)
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

	static const UDataLayerInstance* ResolveDataLayerInstanceByRuleName(const UDataLayerManager* DataLayerManager, const FName RuleName)
	{
		if (!DataLayerManager || RuleName.IsNone())
		{
			return nullptr;
		}

		if (const UDataLayerInstance* DataLayerInstance = DataLayerManager->GetDataLayerInstanceFromName(RuleName))
		{
			return DataLayerInstance;
		}

		FString RuleNameString = RuleName.ToString();
		RuleNameString.TrimStartAndEndInline();
		if (RuleNameString.IsEmpty())
		{
			return nullptr;
		}

		for (const UDataLayerInstance* DataLayerInstance : DataLayerManager->GetDataLayerInstances())
		{
			if (!DataLayerInstance)
			{
				continue;
			}

			if (RuleNameString.Equals(DataLayerInstance->GetDataLayerShortName(), ESearchCase::IgnoreCase) ||
				RuleNameString.Equals(DataLayerInstance->GetDataLayerFullName(), ESearchCase::IgnoreCase))
			{
				return DataLayerInstance;
			}
		}

		return nullptr;
	}

	static void ResolveWorldPartitionRegionRulesAsDataLayers(UWorld* World, FLPTLevelRules& InOutRules)
	{
		if (!World || (InOutRules.WorldPartitionDataLayerAssets.IsEmpty() && InOutRules.WorldPartitionRegions.IsEmpty()))
		{
			return;
		}

		UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
		if (!WorldPartitionSubsystem)
		{
			UE_LOG(LogTemp, Warning, TEXT("LPT Editor: UWorldPartitionSubsystem is unavailable for '%s'. Continuing with best-effort Data Layer rule resolution."),
				*World->GetOutermost()->GetName()
			);
		}

		UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>();
		if (!DataLayerSubsystem)
		{
			UE_LOG(LogTemp, Warning, TEXT("LPT Editor: UDataLayerSubsystem is unavailable for '%s'. Continuing with best-effort Data Layer rule resolution."),
				*World->GetOutermost()->GetName()
			);
		}

		UDataLayerManager* DataLayerManager = World->GetDataLayerManager();
		if (!DataLayerManager)
		{
			UE_LOG(LogTemp, Warning, TEXT("LPT Editor: UDataLayerManager is unavailable for '%s'. Keeping unresolved Data Layer name rules as-is."),
				*World->GetOutermost()->GetName()
			);
			return;
		}

		TArray<FName> ResolvedDataLayers;
		TSet<FName> UniqueResolvedDataLayers;
		ResolvedDataLayers.Reserve(InOutRules.WorldPartitionRegions.Num() + InOutRules.WorldPartitionDataLayerAssets.Num() * 2);

		auto AddResolvedDataLayerName = [&ResolvedDataLayers, &UniqueResolvedDataLayers](const FName DataLayerName)
		{
			if (DataLayerName.IsNone() || UniqueResolvedDataLayers.Contains(DataLayerName))
			{
				return;
			}

			UniqueResolvedDataLayers.Add(DataLayerName);
			ResolvedDataLayers.Add(DataLayerName);
		};

		for (const FName RegionRuleName : InOutRules.WorldPartitionRegions)
		{
			if (RegionRuleName.IsNone())
			{
				continue;
			}

			const UDataLayerInstance* DataLayerInstance = ResolveDataLayerInstanceByRuleName(DataLayerManager, RegionRuleName);
			if (!DataLayerInstance)
			{
				UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Data Layer '%s' was not found in world '%s'. Falling back to raw name match."),
					*RegionRuleName.ToString(),
					*World->GetOutermost()->GetName()
				);
				AddResolvedDataLayerName(RegionRuleName);
				continue;
			}

			AddResolvedDataLayerName(DataLayerInstance->GetDataLayerFName());
			AddResolvedDataLayerName(FName(*DataLayerInstance->GetDataLayerShortName()));
		}

		for (const TSoftObjectPtr<UDataLayerAsset>& DataLayerAssetRule : InOutRules.WorldPartitionDataLayerAssets)
		{
			const FSoftObjectPath DataLayerAssetPath = DataLayerAssetRule.ToSoftObjectPath();
			if (!DataLayerAssetPath.IsValid())
			{
				continue;
			}

			const UDataLayerAsset* DataLayerAsset = DataLayerAssetRule.LoadSynchronous();
			if (!DataLayerAsset)
			{
				UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to load Data Layer asset '%s' in world '%s'. Rule will be ignored."),
					*DataLayerAssetPath.ToString(),
					*World->GetOutermost()->GetName()
				);
				continue;
			}

			const UDataLayerInstance* DataLayerInstance = DataLayerManager->GetDataLayerInstanceFromAsset(DataLayerAsset);
			if (!DataLayerInstance)
			{
				UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Data Layer asset '%s' has no instance in world '%s'. Rule will be ignored."),
					*DataLayerAssetPath.ToString(),
					*World->GetOutermost()->GetName()
				);
				continue;
			}

			AddResolvedDataLayerName(DataLayerInstance->GetDataLayerFName());
			AddResolvedDataLayerName(FName(*DataLayerInstance->GetDataLayerShortName()));
		}

		InOutRules.WorldPartitionRegions = MoveTemp(ResolvedDataLayers);
	}

	static void CollectWorldPartitionActorPackages(UWorld* World, const FLPTLevelRules& Rules, TSet<FName>& InOutCandidateActorPackages)
	{
		if (!World)
		{
			return;
		}

		UWorldPartition* WorldPartition = World->GetWorldPartition();
		if (!WorldPartition)
		{
			UE_LOG(LogTemp, Warning, TEXT("LPT Editor: World Partition is unavailable for '%s'. ActorDesc scan skipped."),
				*World->GetOutermost()->GetName()
			);
			return;
		}

		FLPTLevelRules NormalizedRules = Rules;
		if (NormalizedRules.WorldPartitionRegions.Num() > 0)
		{
			TArray<FName> ExpandedRegionRules;
			ExpandedRegionRules.Reserve(NormalizedRules.WorldPartitionRegions.Num() * 4);
			for (const FName RegionRule : NormalizedRules.WorldPartitionRegions)
			{
				AddDataLayerNameWithVariants(RegionRule, ExpandedRegionRules);
			}
			NormalizedRules.WorldPartitionRegions = MoveTemp(ExpandedRegionRules);
		}

		FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, [&InOutCandidateActorPackages, &NormalizedRules](const FWorldPartitionActorDescInstance* ActorDescInstance)
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
				AddDataLayerNameWithVariants(InstanceName, ActorDataLayerNamesForFilter);
			}

			// Fallback when resolved Data Layer instance names are unavailable in current editor state.
			const TArray<FName> RawActorDataLayers = ActorDescInstance->GetDataLayers();
			for (const FName RawDataLayerName : RawActorDataLayers)
			{
				AddDataLayerNameWithVariants(RawDataLayerName, ActorDataLayerNamesForFilter);
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

			if (!ULevelPreloadAssetFilter::ShouldIncludeWorldPartitionActor(ActorObjectPath, ActorDataLayerNamesForFilter, &NormalizedRules))
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

	static bool IsGlobalDefaultsEnabled(const FLevelPreloadEntry& Entry)
	{
		// Rules flag is the source of truth; legacy mirror is synchronized separately.
		return Entry.Rules.bRulesInitializedFromGlobalDefaults;
	}

	static FLPTLevelRules BuildMergedRulesWithGlobalDominance(const FLPTLevelRules& LevelRules, const ULevelProgressTrackerSettings* Settings)
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
		Merged.AssetClassFilter = GlobalRules.AssetClassFilter;
		Merged.WorldPartitionDataLayerAssets = MergeDataLayerAssetRules(LevelRules.WorldPartitionDataLayerAssets, GlobalRules.WorldPartitionDataLayerAssets);
		Merged.WorldPartitionRegions = MergeNameRules(LevelRules.WorldPartitionRegions, GlobalRules.WorldPartitionRegions);
		Merged.WorldPartitionCells = MergeStringRules(LevelRules.WorldPartitionCells, GlobalRules.WorldPartitionCells);
		Merged.bUseExclusionMode = GlobalRules.bUseExclusionMode;
		Merged.bAllowWorldPartitionAutoScan = GlobalRules.bAllowWorldPartitionAutoScan;
		return Merged;
	}
}

void FLevelProgressTrackerEditorModule::StartupModule()
{
#if WITH_EDITOR
	UE_LOG(LogTemp, Log, TEXT("LPT Editor: StartupModule."));
	RegisterStyle();
	UPackage::PackageSavedWithContextEvent.AddRaw(this, &FLevelProgressTrackerEditorModule::OnPackageSaved);
	ULevelProgressTrackerSettings::OnOpenLevelRulesEditorRequested.AddRaw(this, &FLevelProgressTrackerEditorModule::HandleOpenLevelRulesEditorRequested);
	RegisterMenus();
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLevelProgressTrackerEditorModule::RegisterMenus));
#endif
}

void FLevelProgressTrackerEditorModule::ShutdownModule()
{
#if WITH_EDITOR
	UE_LOG(LogTemp, Log, TEXT("LPT Editor: ShutdownModule."));
	UPackage::PackageSavedWithContextEvent.RemoveAll(this);
	ULevelProgressTrackerSettings::OnOpenLevelRulesEditorRequested.RemoveAll(this);
	if (UToolMenus::TryGet())
	{
		UToolMenus::Get()->RemoveEntry(TEXT("LevelEditor.LevelEditorToolBar.AssetsToolBar"), TEXT("Content"), TEXT("LPT_OpenLevelRules"));
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
	UnregisterStyle();
#endif
}

#if WITH_EDITOR
void FLevelProgressTrackerEditorModule::RegisterStyle()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LevelProgressTracker"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to find plugin directory for style registration."));
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(LevelProgressTrackerEditorPrivate::StyleSetName);
	StyleSet->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

	const FVector2f Icon20(20.f, 20.f);
	const FVector2f Icon40(40.f, 40.f);
	const FString IconPath = StyleSet->RootToContentDir(TEXT("IconLPT"), TEXT(".svg"));

	StyleSet->Set(
		LevelProgressTrackerEditorPrivate::ToolbarIconName,
		new FSlateVectorImageBrush(IconPath, Icon40)
	);

	StyleSet->Set(
		TEXT("LevelProgressTracker.LPTRules.Small"),
		new FSlateVectorImageBrush(IconPath, Icon20)
	);

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FLevelProgressTrackerEditorModule::UnregisterStyle()
{
	if (!StyleSet.IsValid())
	{
		return;
	}

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
	StyleSet.Reset();
}

void FLevelProgressTrackerEditorModule::RegisterMenus()
{
	if (!UToolMenus::IsToolMenuUIEnabled())
	{
		UE_LOG(LogTemp, Verbose, TEXT("LPT Editor: Tool menu UI is disabled. Skipping menu registration."));
		return;
	}

	UToolMenus* ToolMenus = UToolMenus::Get();

	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolbarMenu = ToolMenus->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.AssetsToolBar"));
	if (!ToolbarMenu)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to extend LevelEditor toolbar menu."));
		return;
	}

	ToolMenus->RemoveEntry(TEXT("LevelEditor.LevelEditorToolBar.AssetsToolBar"), TEXT("Content"), TEXT("LPT_OpenLevelRules"));

	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection(TEXT("Content"));
	FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
		TEXT("LPT_OpenLevelRules"),
		FUIAction(FExecuteAction::CreateRaw(this, &FLevelProgressTrackerEditorModule::HandleToolbarOpenLevelRulesClicked)),
		FText::FromString(TEXT("LPT Rules")),
		FText::FromString(TEXT("Open per-level rules for the currently opened level. Rules are stored per level in LevelPreloadDatabase.")),
		FSlateIcon(
			LevelProgressTrackerEditorPrivate::StyleSetName,
			LevelProgressTrackerEditorPrivate::ToolbarIconName,
			TEXT("LevelProgressTracker.LPTRules.Small")
		)
	);
	Entry.InsertPosition = FToolMenuInsert(TEXT("EditCinematics"), EToolMenuInsertType::After);
	Section.AddEntry(Entry);

	UE_LOG(LogTemp, Log, TEXT("LPT Editor: Registered toolbar button 'LPT Rules'."));
	ToolMenus->RefreshAllWidgets();
}

void FLevelProgressTrackerEditorModule::HandleToolbarOpenLevelRulesClicked()
{
	UE_LOG(LogTemp, Log, TEXT("LPT Editor: Toolbar button clicked."));
	HandleOpenLevelRulesEditorRequested(GetMutableDefault<ULevelProgressTrackerSettings>());
}

void FLevelProgressTrackerEditorModule::OnPackageSaved(const FString& PackageFilename, UPackage* SavedPackage, FObjectPostSaveContext SaveContext)
{
	(void)PackageFilename;
	(void)SaveContext;

	const ULevelProgressTrackerSettings* Settings = GetDefault<ULevelProgressTrackerSettings>();
	if (Settings && !Settings->bAutoGenerateOnLevelSave)
	{
		return;
	}

	if (!SavedPackage)
	{
		return;
	}

	const FString SavedPackageName = UWorld::RemovePIEPrefix(SavedPackage->GetName());

	UWorld* SavedWorld = UWorld::FindWorldInPackage(SavedPackage);
	if (!SavedWorld)
	{
		// World Partition usually saves external actor/object packages, not the map package itself.
		// In that case, rebuild for the currently edited partitioned world if the package belongs to it.
		if (!GEditor)
		{
			return;
		}

		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (!EditorWorld || !EditorWorld->IsPartitionedWorld())
		{
			return;
		}

		if (!LevelProgressTrackerEditorPrivate::IsExternalPackageOfWorldPartitionLevel(SavedPackageName, EditorWorld))
		{
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("LPT Editor: Detected WP external package save '%s'. Rebuilding for '%s'."),
			*SavedPackageName,
			*EditorWorld->GetOutermost()->GetName()
		);

		RebuildLevelDependencies(EditorWorld);
		return;
	}

	RebuildLevelDependencies(SavedWorld);
}

bool FLevelProgressTrackerEditorModule::TryGetCurrentEditorLevel(TSoftObjectPtr<UWorld>& OutLevelSoftPtr, FString& OutLevelPackagePath, FString& OutLevelDisplayName, bool& bIsWorldPartition) const
{
	OutLevelSoftPtr.Reset();
	OutLevelPackagePath.Reset();
	OutLevelDisplayName.Reset();
	bIsWorldPartition = false;

	if (!GEditor)
	{
		return false;
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!EditorWorld)
	{
		return false;
	}

	const UPackage* WorldPackage = EditorWorld->GetOutermost();
	if (!WorldPackage)
	{
		return false;
	}

	OutLevelPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());
	if (OutLevelPackagePath.IsEmpty())
	{
		OutLevelPackagePath = WorldPackage->GetName();
	}

	const FString LevelAssetName = FPackageName::GetLongPackageAssetName(OutLevelPackagePath);
	if (LevelAssetName.IsEmpty())
	{
		return false;
	}

	const FSoftObjectPath LevelObjectPath(FString::Printf(TEXT("%s.%s"), *OutLevelPackagePath, *LevelAssetName));
	if (!LevelObjectPath.IsValid())
	{
		return false;
	}

	OutLevelSoftPtr = TSoftObjectPtr<UWorld>(LevelObjectPath);
	OutLevelDisplayName = LevelAssetName;
	bIsWorldPartition = EditorWorld->IsPartitionedWorld();
	return true;
}

void FLevelProgressTrackerEditorModule::RebuildLevelDependencies(UWorld* SavedWorld)
{
	if (!SavedWorld)
	{
		return;
	}

	const ULevelProgressTrackerSettings* Settings = GetDefault<ULevelProgressTrackerSettings>();
	if (!Settings)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Project settings are not available. Skipping database generation."));
		return;
	}

	const FString LevelPackagePath = SavedWorld->GetOutermost()->GetName();
	const FString LevelAssetName = FPackageName::GetLongPackageAssetName(LevelPackagePath);
	const FSoftObjectPath LevelObjectPath(FString::Printf(TEXT("%s.%s"), *LevelPackagePath, *LevelAssetName));
	const TSoftObjectPtr<UWorld> LevelSoftPtr(LevelObjectPath);

	ULevelPreloadDatabase* DatabaseAsset = GetOrCreateDatabaseAsset(Settings);
	if (!DatabaseAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to create or load LevelPreloadDatabase asset."));
		return;
	}

	bool bWasEntryAdded = false;
	FLevelPreloadEntry* LevelEntry = DatabaseAsset->FindOrAddEntryByLevel(LevelSoftPtr, bWasEntryAdded);
	if (!LevelEntry)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to create or resolve database entry for '%s'."), *LevelPackagePath);
		return;
	}

	if (bWasEntryAdded)
	{
		LevelEntry->Rules = FLPTLevelRules();
		LevelEntry->Rules.bRulesInitializedFromGlobalDefaults = false;
		LevelEntry->bRulesInitializedFromGlobalDefaults = false;
	}

	const bool bUseGlobalDefaults = LevelProgressTrackerEditorPrivate::IsGlobalDefaultsEnabled(*LevelEntry);
	// Keep legacy and current flags synchronized in both directions of the toggle.
	LevelEntry->Rules.bRulesInitializedFromGlobalDefaults = bUseGlobalDefaults;
	LevelEntry->bRulesInitializedFromGlobalDefaults = bUseGlobalDefaults;

	const FLPTLevelRules EffectiveRules = bUseGlobalDefaults
		? LevelProgressTrackerEditorPrivate::BuildMergedRulesWithGlobalDominance(LevelEntry->Rules, Settings)
		: LevelEntry->Rules;

	const bool bIsWorldPartition = SavedWorld->IsPartitionedWorld();
	if (bIsWorldPartition && !EffectiveRules.bAllowWorldPartitionAutoScan)
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: World Partition auto scan is disabled for this level. Skipping database generation for '%s'."),
			*SavedWorld->GetOutermost()->GetName()
		);

		if (bWasEntryAdded)
		{
			DatabaseAsset->Modify();
			DatabaseAsset->MarkPackageDirty();
			DatabaseAsset->GetOutermost()->MarkPackageDirty();
			SaveDatabaseAsset(DatabaseAsset);
		}
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = AssetRegistryModule.Get();

	TSet<FSoftObjectPath> UniqueCandidateAssets;
	TArray<FSoftObjectPath> CandidateAssets;

	if (!bIsWorldPartition)
	{
		const FName LevelPackageName = FName(*SavedWorld->GetOutermost()->GetName());
		const TArray<FName> RootPackages = { LevelPackageName };
		LevelProgressTrackerEditorPrivate::AppendHardDependencyClosureAssets(Registry, RootPackages, UniqueCandidateAssets, CandidateAssets, &EffectiveRules);
	}
	else
	{
		FLPTLevelRules WorldPartitionScanRules = EffectiveRules;
		LevelProgressTrackerEditorPrivate::ResolveWorldPartitionRegionRulesAsDataLayers(SavedWorld, WorldPartitionScanRules);

		TSet<FName> CandidateActorPackages;
		LevelProgressTrackerEditorPrivate::CollectWorldPartitionActorPackages(SavedWorld, WorldPartitionScanRules, CandidateActorPackages);

		// World Partition scan uses ActorDesc metadata and AssetRegistry package dependencies only.
		// It is independent from currently loaded editor actors.
		const TArray<FName> RootPackages = CandidateActorPackages.Array();
		LevelProgressTrackerEditorPrivate::AppendHardDependencyClosureAssets(Registry, RootPackages, UniqueCandidateAssets, CandidateAssets, &EffectiveRules);

		UE_LOG(LogTemp, Log, TEXT("LPT Editor: WP Candidates: %d"), CandidateAssets.Num());

		// Keep explicit asset rules discoverable in inclusion mode even when they were not reached by package traversal.
		LevelProgressTrackerEditorPrivate::AppendExplicitAssetRuleCandidates(EffectiveRules, UniqueCandidateAssets, CandidateAssets);
	}

	FLPTLevelRules FinalFilterRules = EffectiveRules;
	if (bIsWorldPartition)
	{
		// For World Partition, Data Layer and Cell rules are evaluated during actor/package collection.
		// Final asset filtering should only apply asset/folder include/exclude rules.
		FinalFilterRules.WorldPartitionDataLayerAssets.Empty();
		FinalFilterRules.WorldPartitionRegions.Empty();
		FinalFilterRules.WorldPartitionCells.Empty();
	}

	TArray<FSoftObjectPath> FilteredAssets;
	if (bIsWorldPartition && !FinalFilterRules.bUseExclusionMode)
	{
		// In WP inclusion mode, keep auto-scanned actor assets and merge additional includes from folder rules.
		// This prevents global/default include rules from unintentionally replacing Data Layer scan results.
		LevelProgressTrackerEditorPrivate::AppendFolderRuleCandidates(Registry, FinalFilterRules, UniqueCandidateAssets, CandidateAssets);
		FilteredAssets = CandidateAssets;
	}
	else
	{
		FilteredAssets = ULevelPreloadAssetFilter::FilterAssets(CandidateAssets, &FinalFilterRules);
	}

	DatabaseAsset->Modify();
	if (!DatabaseAsset->UpdateEntryAssetsByLevel(LevelSoftPtr, FilteredAssets))
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to update preload assets for level '%s'."), *LevelPackagePath);
		return;
	}

	DatabaseAsset->MarkPackageDirty();
	DatabaseAsset->GetOutermost()->MarkPackageDirty();

	if (!SaveDatabaseAsset(DatabaseAsset))
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to save LevelPreloadDatabase after updating '%s'."),
			*LevelPackagePath
		);
	}
}

bool FLevelProgressTrackerEditorModule::PromptCreateLevelRules(bool& bApplyGlobalDefaults) const
{
	bApplyGlobalDefaults = false;

	bool bCreateConfirmed = false;
	TSharedPtr<SWindow> DialogWindow;
	TSharedPtr<SCheckBox> ApplyDefaultsCheckBox;

	DialogWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("Create LPT Rules")))
		.ClientSize(FVector2D(520.f, 190.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true);

	DialogWindow->SetContent(
		SNew(SBorder)
		.Padding(12.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("No per-level rules exist for the currently opened level.")))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 12.f)
			[
				SAssignNew(ApplyDefaultsCheckBox, SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Apply Global Default Rules")))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SUniformGridPanel)
				.SlotPadding(6.f)

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Create")))
					.OnClicked_Lambda([&DialogWindow, &ApplyDefaultsCheckBox, &bCreateConfirmed, &bApplyGlobalDefaults]()
					{
						bCreateConfirmed = true;
						bApplyGlobalDefaults = !ApplyDefaultsCheckBox.IsValid() || ApplyDefaultsCheckBox->IsChecked();

						if (DialogWindow.IsValid())
						{
							DialogWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel")))
					.OnClicked_Lambda([&DialogWindow]()
					{
						if (DialogWindow.IsValid())
						{
							DialogWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			]
		]
	);

	if (GEditor && DialogWindow.IsValid())
	{
		GEditor->EditorAddModalWindow(DialogWindow.ToSharedRef());
	}

	return bCreateConfirmed;
}

void FLevelProgressTrackerEditorModule::OpenLevelRulesWindow(ULevelPreloadDatabase* DatabaseAsset, const TSoftObjectPtr<UWorld>& LevelSoftPtr, const FString& LevelDisplayName, bool bIsWorldPartition)
{
	if (!DatabaseAsset)
	{
		return;
	}

	const FLevelPreloadEntry* ExistingEntry = DatabaseAsset->FindEntryByLevel(LevelSoftPtr);
	if (!ExistingEntry)
	{
		return;
	}

	TSharedPtr<FStructOnScope> RulesStructOnScope = MakeShared<FStructOnScope>(FLPTLevelRules::StaticStruct());
	if (FLPTLevelRules* WorkingRules = reinterpret_cast<FLPTLevelRules*>(RulesStructOnScope->GetStructMemory()))
	{
		*WorkingRules = ExistingEntry->Rules;
		WorkingRules->bRulesInitializedFromGlobalDefaults = LevelProgressTrackerEditorPrivate::IsGlobalDefaultsEnabled(*ExistingEntry);
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	FStructureDetailsViewArgs StructureDetailsViewArgs;
	StructureDetailsViewArgs.bShowObjects = true;
	StructureDetailsViewArgs.bShowAssets = true;
	StructureDetailsViewArgs.bShowClasses = true;
	StructureDetailsViewArgs.bShowInterfaces = false;

	const TSharedRef<IStructureDetailsView> StructureDetailsView = PropertyEditorModule.CreateStructureDetailView(
		DetailsViewArgs,
		StructureDetailsViewArgs,
		RulesStructOnScope,
		FText::FromString(TEXT("Level Rules"))
	);

	const TSharedRef<SWindow> RulesWindow = SNew(SWindow)
		.Title(FText::FromString(FString::Printf(TEXT("LPT Rules - %s"), *LevelDisplayName)))
		.ClientSize(FVector2D(760.f, 640.f))
		.SupportsMaximize(true)
		.SupportsMinimize(true);

	RulesWindow->SetContent(
		SNew(SBorder)
		.Padding(12.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Level: %s"), *LevelDisplayName)))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("These settings affect only the currently opened level.")))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Save the level to regenerate the asset preload list.")))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
				.Visibility_Lambda([RulesStructOnScope, bIsWorldPartition]()
				{
					if (!bIsWorldPartition)
					{
						return EVisibility::Collapsed;
					}

					const FLPTLevelRules* WorkingRules = reinterpret_cast<const FLPTLevelRules*>(RulesStructOnScope->GetStructMemory());
					return (WorkingRules && !WorkingRules->bAllowWorldPartitionAutoScan) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text(FText::FromString(TEXT("World Partition auto scan is disabled for this level.")))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(0.f, 8.f, 0.f, 8.f)
			[
				StructureDetailsView->GetWidget().ToSharedRef()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SUniformGridPanel)
				.SlotPadding(6.f)

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Save Rules")))
					.OnClicked_Lambda([this, DatabaseAsset, LevelSoftPtr, RulesStructOnScope, RulesWindow]()
					{
						if (!DatabaseAsset)
						{
							return FReply::Handled();
						}

						const FLPTLevelRules* WorkingRules = reinterpret_cast<const FLPTLevelRules*>(RulesStructOnScope->GetStructMemory());
						if (!WorkingRules)
						{
							return FReply::Handled();
						}

						FLevelPreloadEntry* MutableEntry = DatabaseAsset->FindEntryByLevel(LevelSoftPtr);
						if (!MutableEntry)
						{
							return FReply::Handled();
						}

						DatabaseAsset->Modify();
						MutableEntry->Rules = *WorkingRules;
						MutableEntry->bRulesInitializedFromGlobalDefaults = MutableEntry->Rules.bRulesInitializedFromGlobalDefaults;
						DatabaseAsset->MarkPackageDirty();
						DatabaseAsset->GetOutermost()->MarkPackageDirty();
						SaveDatabaseAsset(DatabaseAsset);

						RulesWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel")))
					.OnClicked_Lambda([RulesWindow]()
					{
						RulesWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]
	);

	if (GEditor)
	{
		GEditor->EditorAddModalWindow(RulesWindow);
	}
	else
	{
		FSlateApplication::Get().AddWindow(RulesWindow);
	}
}

void FLevelProgressTrackerEditorModule::HandleOpenLevelRulesEditorRequested(ULevelProgressTrackerSettings* Settings)
{
	const auto ShowWarningDialog = [](const FString& Message)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
	};

	ULevelProgressTrackerSettings* EffectiveSettings = Settings ? Settings : GetMutableDefault<ULevelProgressTrackerSettings>();
	if (!EffectiveSettings)
	{
		ShowWarningDialog(TEXT("LPT Editor: Project settings are not available. Cannot open level rules editor."));
		return;
	}

	TSoftObjectPtr<UWorld> LevelSoftPtr;
	FString LevelPackagePath;
	FString LevelDisplayName;
	bool bIsWorldPartition = false;
	if (!TryGetCurrentEditorLevel(LevelSoftPtr, LevelPackagePath, LevelDisplayName, bIsWorldPartition))
	{
		ShowWarningDialog(TEXT("LPT Editor: Failed to resolve the currently opened level."));
		return;
	}

	ULevelPreloadDatabase* DatabaseAsset = GetOrCreateDatabaseAsset(EffectiveSettings);
	if (!DatabaseAsset)
	{
		ShowWarningDialog(TEXT("LPT Editor: Failed to create or load LevelPreloadDatabase asset."));
		return;
	}

	FLevelPreloadEntry* Entry = DatabaseAsset->FindEntryByLevel(LevelSoftPtr);
	if (!Entry)
	{
		bool bWasAdded = false;
		Entry = DatabaseAsset->FindOrAddEntryByLevel(LevelSoftPtr, bWasAdded);
		if (!Entry)
		{
			ShowWarningDialog(FString::Printf(TEXT("LPT Editor: Failed to create level rules entry for '%s'."), *LevelPackagePath));
			return;
		}

		// Auto-create an empty per-level rules entry and open the full rules editor immediately.
		// Global defaults can be enabled by the user via "Rules Initialized from Global Defaults".
		Entry->Rules = FLPTLevelRules();
		Entry->Rules.bRulesInitializedFromGlobalDefaults = false;
		Entry->bRulesInitializedFromGlobalDefaults = false;

		DatabaseAsset->Modify();
		DatabaseAsset->MarkPackageDirty();
		DatabaseAsset->GetOutermost()->MarkPackageDirty();
		SaveDatabaseAsset(DatabaseAsset);
	}

	OpenLevelRulesWindow(DatabaseAsset, LevelSoftPtr, LevelDisplayName, bIsWorldPartition);
}

ULevelPreloadDatabase* FLevelProgressTrackerEditorModule::GetOrCreateDatabaseAsset(const ULevelProgressTrackerSettings* Settings) const
{
	FString DatabasePackagePath;
	FSoftObjectPath DatabaseObjectPath;

	if (!ULevelPreloadAssetFilter::ResolveDatabaseAssetPath(Settings, DatabasePackagePath, DatabaseObjectPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Invalid database folder in project settings. Expected a valid long package path (for example '/Game/_DataLPT' or '/PluginName/Data')."));
		return nullptr;
	}

	UPackage* DatabasePackage = FindPackage(nullptr, *DatabasePackagePath);
	if (!DatabasePackage)
	{
		DatabasePackage = LoadPackage(nullptr, *DatabasePackagePath, LOAD_None);
	}
	if (!DatabasePackage)
	{
		DatabasePackage = CreatePackage(*DatabasePackagePath);
	}

	if (!DatabasePackage)
	{
		return nullptr;
	}

	FString DatabaseAssetName = DatabaseObjectPath.GetAssetName();
	if (DatabaseAssetName.IsEmpty())
	{
		DatabaseAssetName = FPackageName::GetLongPackageAssetName(DatabasePackagePath);
	}

	ULevelPreloadDatabase* DatabaseAsset = FindObject<ULevelPreloadDatabase>(DatabasePackage, *DatabaseAssetName);
	if (!DatabaseAsset)
	{
		DatabaseAsset = LoadObject<ULevelPreloadDatabase>(nullptr, *DatabaseObjectPath.ToString());
	}
	if (DatabaseAsset)
	{
		return DatabaseAsset;
	}

	DatabaseAsset = NewObject<ULevelPreloadDatabase>(
		DatabasePackage,
		ULevelPreloadDatabase::StaticClass(),
		*DatabaseAssetName,
		RF_Public | RF_Standalone
	);

	if (!DatabaseAsset)
	{
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(DatabaseAsset);
	DatabaseAsset->MarkPackageDirty();
	DatabasePackage->MarkPackageDirty();

	SaveDatabaseAsset(DatabaseAsset);

	return DatabaseAsset;
}

bool FLevelProgressTrackerEditorModule::SaveDatabaseAsset(ULevelPreloadDatabase* DatabaseAsset) const
{
	if (!DatabaseAsset)
	{
		return false;
	}

	UPackage* Package = DatabaseAsset->GetOutermost();
	if (!Package)
	{
		return false;
	}

	const FString PackageName = Package->GetName();
	FString PackageFilename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}

	const FString PackageDirectory = FPaths::GetPath(PackageFilename);
	IFileManager::Get().MakeDirectory(*PackageDirectory, true);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	SaveArgs.Error = GError;

	return UPackage::SavePackage(Package, DatabaseAsset, *PackageFilename, SaveArgs);
}
#endif

IMPLEMENT_MODULE(FLevelProgressTrackerEditorModule, LevelProgressTrackerEditor)
