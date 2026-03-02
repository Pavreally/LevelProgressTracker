// Pavel Gornostaev <https://github.com/Pavreally>

#include "EditorModuleGenerationLPT.h"

#include "AssetCollectorLPT.h"
#include "AssetFilterLPT.h"
#include "AssetUtilsLPT.h"
#include "DataLayerResolverLPT.h"
#include "LevelPreloadAssetFilter.h"
#include "LevelPreloadDatabaseLPT.h"
#include "AssetCollectionDataLPT.h"
#include "AssetFilterSettingsLPT.h"
#include "LogLPTEditor.h"
#include "SettingsLPT.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

namespace EditorModuleLPTPrivate
{
	const FName StyleSetName(TEXT("LevelProgressTrackerStyle"));
	const FName ToolbarIconName(TEXT("LevelProgressTracker.LPTRules"));
	const FName DefaultCollectionKey(TEXT("Default"));

	template <typename TAssetClass>
	TAssetClass* LoadOrCreateDataAsset(const FString& PackagePath, const FString& AssetName, bool& bOutCreated)
	{
		bOutCreated = false;

		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
		if (TAssetClass* ExistingAsset = LoadObject<TAssetClass>(nullptr, *ObjectPath))
		{
			return ExistingAsset;
		}

		UPackage* AssetPackage = FindPackage(nullptr, *PackagePath);
		if (!AssetPackage)
		{
			AssetPackage = LoadPackage(nullptr, *PackagePath, LOAD_None);
		}
		if (!AssetPackage)
		{
			AssetPackage = CreatePackage(*PackagePath);
		}

		if (!AssetPackage)
		{
			return nullptr;
		}

		if (TAssetClass* ExistingObject = FindObject<TAssetClass>(AssetPackage, *AssetName))
		{
			return ExistingObject;
		}

		TAssetClass* NewAsset = NewObject<TAssetClass>(
			AssetPackage,
			TAssetClass::StaticClass(),
			*AssetName,
			RF_Public | RF_Standalone
		);

		if (!NewAsset)
		{
			return nullptr;
		}

		bOutCreated = true;
		FAssetRegistryModule::AssetCreated(NewAsset);
		NewAsset->MarkPackageDirty();
		AssetPackage->MarkPackageDirty();
		return NewAsset;
	}

	FString SanitizeAssetToken(const FString& InValue, const FString& InFallback)
	{
		FString Token = InValue;
		Token.TrimStartAndEndInline();

		if (Token.IsEmpty())
		{
			Token = InFallback;
		}

		for (int32 Index = 0; Index < Token.Len(); ++Index)
		{
			TCHAR& CharRef = Token[Index];
			if (!FChar::IsAlnum(CharRef) && CharRef != TCHAR('_'))
			{
				CharRef = TCHAR('_');
			}
		}

		while (Token.Contains(TEXT("__")))
		{
			Token.ReplaceInline(TEXT("__"), TEXT("_"));
		}

		Token.TrimStartAndEndInline();
		while (Token.StartsWith(TEXT("_")))
		{
			Token.RightChopInline(1, EAllowShrinking::No);
		}
		while (Token.EndsWith(TEXT("_")))
		{
			Token.LeftChopInline(1, EAllowShrinking::No);
		}

		if (Token.IsEmpty())
		{
			Token = InFallback;
		}

		if (!Token.IsEmpty() && FChar::IsDigit(Token[0]))
		{
			Token = FString::Printf(TEXT("_%s"), *Token);
		}

		return Token;
	}

	void HashString(uint32& InOutHash, const FString& Value)
	{
		InOutHash = HashCombineFast(InOutHash, GetTypeHash(Value));
	}

	void HashName(uint32& InOutHash, const FName Value)
	{
		InOutHash = HashCombineFast(InOutHash, GetTypeHash(Value));
	}

	uint32 ComputeFilterSettingsHash(const FLPTFilterSettings& FilterSettings)
	{
		uint32 Hash = 0;

		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.bUseExclusionMode));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.bUseChunkedPreload));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.PreloadChunkSize));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.bAllowWorldPartitionAutoScan));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.bAllowWorldPartitionUnscopedAutoScan));

		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.AssetClassFilter.bIncludeStaticMeshes));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.AssetClassFilter.bIncludeSkeletalMeshes));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.AssetClassFilter.bIncludeMaterials));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.AssetClassFilter.bIncludeNiagara));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.AssetClassFilter.bIncludeSounds));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.AssetClassFilter.bIncludeWidgets));
		Hash = HashCombineFast(Hash, GetTypeHash(FilterSettings.AssetClassFilter.bIncludeDataAssets));

		for (const FSoftObjectPath& AssetRule : FilterSettings.AssetRules)
		{
			HashString(Hash, AssetRule.ToString());
		}

		for (const FDirectoryPath& FolderRule : FilterSettings.FolderRules)
		{
			HashString(Hash, AssetUtilsLPT::NormalizeFolderRuleForMerge(FolderRule.Path));
		}

		for (const TSoftObjectPtr<UDataLayerAsset>& DataLayerRule : FilterSettings.WorldPartitionDataLayerAssets)
		{
			HashString(Hash, DataLayerRule.ToSoftObjectPath().ToString());
		}

		for (const FName RegionRule : FilterSettings.WorldPartitionRegions)
		{
			HashName(Hash, RegionRule);
		}

		for (const FString& CellRule : FilterSettings.WorldPartitionCells)
		{
			HashString(Hash, CellRule);
		}

		return Hash;
	}

	bool HasAnyWorldPartitionScopeRule(const FLPTFilterSettings& FilterSettings)
	{
		return FilterSettings.WorldPartitionDataLayerAssets.Num() > 0 ||
			FilterSettings.WorldPartitionRegions.Num() > 0 ||
			FilterSettings.WorldPartitionCells.Num() > 0;
	}

	bool IsPackageExcludedByAssetOrFolderRules(
		const FString& PackageLongName,
		const TSet<FString>& ExcludedAssetPackages,
		const TArray<FString>& ExcludedFolderPrefixes)
	{
		if (PackageLongName.IsEmpty())
		{
			return false;
		}

		if (ExcludedAssetPackages.Contains(PackageLongName))
		{
			return true;
		}

		for (const FString& ExcludedFolderPrefix : ExcludedFolderPrefixes)
		{
			if (!ExcludedFolderPrefix.IsEmpty() && PackageLongName.StartsWith(ExcludedFolderPrefix))
			{
				return true;
			}
		}

		return false;
	}

	void PruneExcludedDependencyBranches(
		IAssetRegistry& Registry,
		const TArray<FName>& RootPackages,
		const FLPTFilterSettings& ExclusionRules,
		TArray<FSoftObjectPath>& InOutCandidateAssets)
	{
		if (RootPackages.IsEmpty() || InOutCandidateAssets.IsEmpty())
		{
			return;
		}

		TSet<FString> ExcludedAssetPackages;
		ExcludedAssetPackages.Reserve(ExclusionRules.AssetRules.Num());
		for (const FSoftObjectPath& AssetRulePath : ExclusionRules.AssetRules)
		{
			const FString ExcludedPackageName = AssetRulePath.GetLongPackageName();
			if (!ExcludedPackageName.IsEmpty())
			{
				ExcludedAssetPackages.Add(ExcludedPackageName);
			}
		}

		TArray<FString> ExcludedFolderPrefixes;
		ExcludedFolderPrefixes.Reserve(ExclusionRules.FolderRules.Num());
		for (const FDirectoryPath& FolderRule : ExclusionRules.FolderRules)
		{
			const FString NormalizedFolderPrefix = AssetUtilsLPT::NormalizeFolderRuleForMerge(FolderRule.Path);
			if (!NormalizedFolderPrefix.IsEmpty())
			{
				ExcludedFolderPrefixes.Add(NormalizedFolderPrefix);
			}
		}

		if (ExcludedAssetPackages.IsEmpty() && ExcludedFolderPrefixes.IsEmpty())
		{
			return;
		}

		TSet<FName> ReachablePackages;
		TSet<FName> VisitedPackages;
		TArray<FName> PendingPackages = RootPackages;

		while (PendingPackages.Num() > 0)
		{
			const FName CurrentPackageName = PendingPackages.Pop(EAllowShrinking::No);
			if (CurrentPackageName.IsNone() || VisitedPackages.Contains(CurrentPackageName))
			{
				continue;
			}

			VisitedPackages.Add(CurrentPackageName);

			const FString CurrentPackageLongName = CurrentPackageName.ToString();
			if (CurrentPackageLongName.IsEmpty() || AssetUtilsLPT::IsEngineOrScriptPackage(CurrentPackageLongName))
			{
				continue;
			}

			if (IsPackageExcludedByAssetOrFolderRules(CurrentPackageLongName, ExcludedAssetPackages, ExcludedFolderPrefixes))
			{
				continue;
			}

			ReachablePackages.Add(CurrentPackageName);

			TArray<FName> HardDependencies;
			Registry.GetDependencies(
				CurrentPackageName,
				HardDependencies,
				UE::AssetRegistry::EDependencyCategory::Package,
				UE::AssetRegistry::EDependencyQuery::Hard
			);

			for (const FName DependencyPackageName : HardDependencies)
			{
				if (!DependencyPackageName.IsNone() && !VisitedPackages.Contains(DependencyPackageName))
				{
					PendingPackages.Add(DependencyPackageName);
				}
			}
		}

		TArray<FSoftObjectPath> PrunedAssets;
		TSet<FSoftObjectPath> UniquePrunedAssets;
		PrunedAssets.Reserve(InOutCandidateAssets.Num());

		for (const FSoftObjectPath& AssetPath : InOutCandidateAssets)
		{
			if (!AssetPath.IsValid())
			{
				continue;
			}

			const FString AssetPackageLongName = AssetPath.GetLongPackageName();
			if (AssetPackageLongName.IsEmpty())
			{
				continue;
			}

			if (!ReachablePackages.Contains(FName(*AssetPackageLongName)) || UniquePrunedAssets.Contains(AssetPath))
			{
				continue;
			}

			UniquePrunedAssets.Add(AssetPath);
			PrunedAssets.Add(AssetPath);
		}

		InOutCandidateAssets = MoveTemp(PrunedAssets);
	}

	uint32 ComputeCollectionContentHash(const UAssetCollectionDataLPT* CollectionAsset, const FLPTFilterSettings& EffectiveFilterSettings)
	{
		if (!CollectionAsset)
		{
			return 0;
		}

		uint32 Hash = ComputeFilterSettingsHash(EffectiveFilterSettings);
		HashName(Hash, CollectionAsset->CollectionKey);
		Hash = HashCombineFast(Hash, GetTypeHash(CollectionAsset->bAutoGenerate));

		const FString GroupTagsAsString = CollectionAsset->GroupTags.ToStringSimple();
		HashString(Hash, GroupTagsAsString);

		for (const TSoftObjectPtr<UDataLayerAsset>& TargetDataLayer : CollectionAsset->TargetDataLayers)
		{
			HashString(Hash, TargetDataLayer.ToSoftObjectPath().ToString());
		}

		for (const FName TargetDataLayerName : CollectionAsset->TargetDataLayerNames)
		{
			HashName(Hash, TargetDataLayerName);
		}

		for (const FString& TargetCellRule : CollectionAsset->TargetCellRules)
		{
			HashString(Hash, TargetCellRule);
		}

		for (const FSoftObjectPath& AssetPath : CollectionAsset->AssetList)
		{
			HashString(Hash, AssetPath.ToString());
		}

		return Hash;
	}

	uint32 ComputeLevelStateHash(UWorld* SavedWorld, const FLPTFilterSettings& EffectiveFilterSettings)
	{
		if (!SavedWorld)
		{
			return 0;
		}

		uint32 Hash = ComputeFilterSettingsHash(EffectiveFilterSettings);
		const bool bIsWorldPartition = SavedWorld->IsPartitionedWorld();

		TArray<FString> ActorIdentifiers;
		int32 ActorCount = 0;
		if (bIsWorldPartition)
		{
			if (UWorldPartition* WorldPartition = SavedWorld->GetWorldPartition())
			{
				FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, [&ActorIdentifiers, &ActorCount](const FWorldPartitionActorDescInstance* ActorDescInstance)
				{
					if (!ActorDescInstance)
					{
						return true;
					}

					++ActorCount;
					const FGuid ActorGuid = ActorDescInstance->GetGuid();
					if (ActorGuid.IsValid())
					{
						ActorIdentifiers.Add(ActorGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
					}
					return true;
				});
			}
		}
		else
		{
			for (const ULevel* Level : SavedWorld->GetLevels())
			{
				if (!Level)
				{
					continue;
				}

				for (const AActor* Actor : Level->Actors)
				{
					if (!Actor)
					{
						continue;
					}

					++ActorCount;
					const FGuid ActorGuid = Actor->GetActorGuid();
					if (ActorGuid.IsValid())
					{
						ActorIdentifiers.Add(ActorGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
					}
				}
			}
		}

		ActorIdentifiers.Sort();
		Hash = HashCombineFast(Hash, GetTypeHash(ActorCount));
		Hash = HashCombineFast(Hash, GetTypeHash(ActorIdentifiers.Num()));
		for (const FString& ActorIdentifier : ActorIdentifiers)
		{
			HashString(Hash, ActorIdentifier);
		}

		if (bIsWorldPartition)
		{
			TArray<FString> DataLayerIdentifiers;
			if (UDataLayerManager* DataLayerManager = SavedWorld->GetDataLayerManager())
			{
				for (const UDataLayerInstance* DataLayerInstance : DataLayerManager->GetDataLayerInstances())
				{
					if (!DataLayerInstance)
					{
						continue;
					}

					const UDataLayerAsset* DataLayerAsset = DataLayerInstance->GetAsset();
					if (DataLayerAsset)
					{
						DataLayerIdentifiers.Add(DataLayerAsset->GetPathName());
					}
					else
					{
						DataLayerIdentifiers.Add(DataLayerInstance->GetDataLayerFullName());
					}
				}
			}

			DataLayerIdentifiers.Sort();
			Hash = HashCombineFast(Hash, GetTypeHash(DataLayerIdentifiers.Num()));
			for (const FString& DataLayerIdentifier : DataLayerIdentifiers)
			{
				HashString(Hash, DataLayerIdentifier);
			}
		}

		return Hash;
	}

	bool EnsureLongPackageFolderExists(const FString& FolderLongPackagePath)
	{
		if (FolderLongPackagePath.IsEmpty() || !FPackageName::IsValidLongPackageName(FolderLongPackagePath))
		{
			return false;
		}

		const FString ProbePackagePath = FString::Printf(TEXT("%s/%s"), *FolderLongPackagePath, TEXT("LPT_Probe"));
		FString ProbeFilename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(ProbePackagePath, ProbeFilename, FPackageName::GetAssetPackageExtension()))
		{
			return false;
		}

		const FString FolderOnDisk = FPaths::GetPath(ProbeFilename);
		return IFileManager::Get().MakeDirectory(*FolderOnDisk, true);
	}

	bool SaveAssetObject(UObject* AssetObject)
	{
		if (!AssetObject)
		{
			return false;
		}

		UPackage* Package = AssetObject->GetOutermost();
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
		return UPackage::SavePackage(Package, AssetObject, *PackageFilename, SaveArgs);
	}

	UAssetCollectionDataLPT* GetOrCreateCollectionAsset(const ULevelProgressTrackerSettings* Settings, const FString& LevelAssetName, const FName CollectionKey)
	{
		if (!Settings)
		{
			return nullptr;
		}

		FString CollectionFolderLongPath;
		if (!Settings->ResolveAssetCollectionFolderPath(CollectionFolderLongPath) || !EnsureLongPackageFolderExists(CollectionFolderLongPath))
		{
			return nullptr;
		}

		const FString SanitizedLevelName = SanitizeAssetToken(LevelAssetName, TEXT("Level"));
		const FString SanitizedCollectionKey = SanitizeAssetToken(
			CollectionKey.IsNone() ? DefaultCollectionKey.ToString() : CollectionKey.ToString(),
			TEXT("Default"));
		const FString LevelCollectionFolderLongPath = FString::Printf(TEXT("%s/%s"), *CollectionFolderLongPath, *SanitizedLevelName);
		if (!EnsureLongPackageFolderExists(LevelCollectionFolderLongPath))
		{
			return nullptr;
		}

		FString AssetName;
		if (SanitizedCollectionKey.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
		{
			AssetName = FString::Printf(TEXT("DA_LPT_Default_%s"), *SanitizedLevelName);
		}
		else
		{
			AssetName = FString::Printf(TEXT("DA_LPT_%s_%s"), *SanitizedCollectionKey, *SanitizedLevelName);
		}

		const FString PackagePath = FString::Printf(TEXT("%s/%s"), *LevelCollectionFolderLongPath, *AssetName);
		bool bCreated = false;
		UAssetCollectionDataLPT* CollectionAsset = LoadOrCreateDataAsset<UAssetCollectionDataLPT>(PackagePath, AssetName, bCreated);
		if (!CollectionAsset)
		{
			return nullptr;
		}

		if (bCreated)
		{
			CollectionAsset->CollectionKey = CollectionKey.IsNone() ? DefaultCollectionKey : CollectionKey;
			CollectionAsset->bAutoGenerate = true;
			CollectionAsset->CollectionContentHash = 0;
			SaveAssetObject(CollectionAsset);
		}

		return CollectionAsset;
	}

	UAssetFilterSettingsLPT* GetOrCreateFilterSettingsAsset(const ULevelProgressTrackerSettings* Settings, const FString& LevelAssetName)
	{
		if (!Settings)
		{
			return nullptr;
		}

		FString FilterFolderLongPath;
		if (!Settings->ResolveFilterSettingsFolderPath(FilterFolderLongPath) || !EnsureLongPackageFolderExists(FilterFolderLongPath))
		{
			return nullptr;
		}

		const FString SanitizedLevelName = SanitizeAssetToken(LevelAssetName, TEXT("Level"));
		const FString LevelFilterFolderLongPath = FString::Printf(TEXT("%s/%s"), *FilterFolderLongPath, *SanitizedLevelName);
		if (!EnsureLongPackageFolderExists(LevelFilterFolderLongPath))
		{
			return nullptr;
		}

		const FString AssetName = FString::Printf(TEXT("DA_LPT_FilterSettings_%s"), *SanitizedLevelName);
		const FString PackagePath = FString::Printf(TEXT("%s/%s"), *LevelFilterFolderLongPath, *AssetName);

		bool bCreated = false;
		UAssetFilterSettingsLPT* FilterSettingsAsset = LoadOrCreateDataAsset<UAssetFilterSettingsLPT>(PackagePath, AssetName, bCreated);
		if (!FilterSettingsAsset)
		{
			return nullptr;
		}

		if (bCreated)
		{
			FilterSettingsAsset->InitializeDefaultsFromProjectSettings(Settings);

			if (FilterSettingsAsset->CollectionPresets.IsEmpty())
			{
				FLPTCollectionPresetLPT& DefaultPreset = FilterSettingsAsset->CollectionPresets.AddDefaulted_GetRef();
				DefaultPreset.CollectionKey = DefaultCollectionKey;
				DefaultPreset.bAutoGenerate = true;
			}

			SaveAssetObject(FilterSettingsAsset);
		}

		return FilterSettingsAsset;
	}

	bool AddUniqueDataLayerAssetRule(TArray<TSoftObjectPtr<UDataLayerAsset>>& InOutRules, const TSoftObjectPtr<UDataLayerAsset>& Rule)
	{
		const FSoftObjectPath RulePath = Rule.ToSoftObjectPath();
		if (!RulePath.IsValid())
		{
			return false;
		}

		for (const TSoftObjectPtr<UDataLayerAsset>& ExistingRule : InOutRules)
		{
			if (ExistingRule.ToSoftObjectPath() == RulePath)
			{
				return false;
			}
		}

		InOutRules.Add(Rule);
		return true;
	}

	const UDataLayerInstance* ResolveDataLayerInstanceByName(UDataLayerManager* DataLayerManager, const FName DataLayerName)
	{
		if (!DataLayerManager || DataLayerName.IsNone())
		{
			return nullptr;
		}

		if (const UDataLayerInstance* InstanceFromName = DataLayerManager->GetDataLayerInstanceFromName(DataLayerName))
		{
			return InstanceFromName;
		}

		const FString RuleNameString = DataLayerName.ToString();
		if (RuleNameString.IsEmpty())
		{
			return nullptr;
		}

		for (const UDataLayerInstance* Instance : DataLayerManager->GetDataLayerInstances())
		{
			if (!Instance)
			{
				continue;
			}

			if (RuleNameString.Equals(Instance->GetDataLayerShortName(), ESearchCase::IgnoreCase) ||
				RuleNameString.Equals(Instance->GetDataLayerFullName(), ESearchCase::IgnoreCase))
			{
				return Instance;
			}
		}

		return nullptr;
	}

	bool DeduplicateCollectionAssetData(UAssetCollectionDataLPT* CollectionAsset)
	{
		if (!CollectionAsset)
		{
			return false;
		}

		bool bModified = false;

		TSet<FSoftObjectPath> UniqueAssetPaths;
		TArray<FSoftObjectPath> DeduplicatedAssetList;
		DeduplicatedAssetList.Reserve(CollectionAsset->AssetList.Num());
		for (const FSoftObjectPath& AssetPath : CollectionAsset->AssetList)
		{
			if (!AssetPath.IsValid() || UniqueAssetPaths.Contains(AssetPath))
			{
				bModified = true;
				continue;
			}

			UniqueAssetPaths.Add(AssetPath);
			DeduplicatedAssetList.Add(AssetPath);
		}
		if (DeduplicatedAssetList.Num() != CollectionAsset->AssetList.Num())
		{
			CollectionAsset->AssetList = MoveTemp(DeduplicatedAssetList);
		}

		TSet<FSoftObjectPath> UniqueLayerAssets;
		TArray<TSoftObjectPtr<UDataLayerAsset>> DeduplicatedLayerAssets;
		DeduplicatedLayerAssets.Reserve(CollectionAsset->TargetDataLayers.Num());
		for (const TSoftObjectPtr<UDataLayerAsset>& LayerAsset : CollectionAsset->TargetDataLayers)
		{
			const FSoftObjectPath LayerPath = LayerAsset.ToSoftObjectPath();
			if (!LayerPath.IsValid() || UniqueLayerAssets.Contains(LayerPath))
			{
				bModified = true;
				continue;
			}

			UniqueLayerAssets.Add(LayerPath);
			DeduplicatedLayerAssets.Add(LayerAsset);
		}
		if (DeduplicatedLayerAssets.Num() != CollectionAsset->TargetDataLayers.Num())
		{
			CollectionAsset->TargetDataLayers = MoveTemp(DeduplicatedLayerAssets);
		}

		TSet<FName> UniqueLayerNames;
		TArray<FName> DeduplicatedLayerNames;
		DeduplicatedLayerNames.Reserve(CollectionAsset->TargetDataLayerNames.Num());
		for (const FName LayerName : CollectionAsset->TargetDataLayerNames)
		{
			if (LayerName.IsNone() || UniqueLayerNames.Contains(LayerName))
			{
				bModified = true;
				continue;
			}

			UniqueLayerNames.Add(LayerName);
			DeduplicatedLayerNames.Add(LayerName);
		}
		if (DeduplicatedLayerNames.Num() != CollectionAsset->TargetDataLayerNames.Num())
		{
			CollectionAsset->TargetDataLayerNames = MoveTemp(DeduplicatedLayerNames);
		}

		TSet<FString> UniqueCellRules;
		TArray<FString> DeduplicatedCellRules;
		DeduplicatedCellRules.Reserve(CollectionAsset->TargetCellRules.Num());
		for (const FString& CellRule : CollectionAsset->TargetCellRules)
		{
			FString NormalizedCellRule = CellRule;
			NormalizedCellRule.TrimStartAndEndInline();
			if (NormalizedCellRule.IsEmpty() || UniqueCellRules.Contains(NormalizedCellRule))
			{
				bModified = true;
				continue;
			}

			UniqueCellRules.Add(NormalizedCellRule);
			DeduplicatedCellRules.Add(NormalizedCellRule);
		}
		if (DeduplicatedCellRules.Num() != CollectionAsset->TargetCellRules.Num())
		{
			CollectionAsset->TargetCellRules = MoveTemp(DeduplicatedCellRules);
		}

		return bModified;
	}

	void ApplyCollectionPresetToAsset(const FLPTCollectionPresetLPT& Preset, UAssetCollectionDataLPT* CollectionAsset)
	{
		if (!CollectionAsset)
		{
			return;
		}

		CollectionAsset->CollectionKey = Preset.CollectionKey.IsNone() ? DefaultCollectionKey : Preset.CollectionKey;
		CollectionAsset->bAutoGenerate = Preset.bAutoGenerate;
		CollectionAsset->GroupTags = Preset.GroupTags;
		CollectionAsset->TargetDataLayers = Preset.TargetDataLayers;
		CollectionAsset->TargetDataLayerNames = Preset.TargetDataLayerNames;
		CollectionAsset->TargetCellRules = Preset.TargetCellRules;
	}

	bool MaterializeCollectionPresets(
		const ULevelProgressTrackerSettings* Settings,
		const FString& LevelAssetName,
		const UAssetFilterSettingsLPT* FilterSettingsAsset,
		FLevelPreloadEntryLPT& InOutEntry)
	{
		if (!Settings || !FilterSettingsAsset || FilterSettingsAsset->CollectionPresets.IsEmpty())
		{
			return false;
		}

		bool bEntryModified = false;

		for (const FLPTCollectionPresetLPT& Preset : FilterSettingsAsset->CollectionPresets)
		{
			const FName PresetCollectionKey = Preset.CollectionKey.IsNone() ? DefaultCollectionKey : Preset.CollectionKey;
			UAssetCollectionDataLPT* CollectionAsset = GetOrCreateCollectionAsset(Settings, LevelAssetName, PresetCollectionKey);
			if (!CollectionAsset)
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to materialize collection preset '%s' for level '%s'."),
					*PresetCollectionKey.ToString(),
					*LevelAssetName
				);
				continue;
			}

			CollectionAsset->Modify();
			ApplyCollectionPresetToAsset(Preset, CollectionAsset);
			DeduplicateCollectionAssetData(CollectionAsset);
			CollectionAsset->MarkPackageDirty();
			CollectionAsset->GetOutermost()->MarkPackageDirty();
			if (!SaveAssetObject(CollectionAsset))
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save collection asset '%s' after applying preset."), *CollectionAsset->GetPathName());
			}

			const FSoftObjectPath CollectionPath(CollectionAsset->GetPathName());
			bool bExistsInEntry = false;
			for (const TSoftObjectPtr<UAssetCollectionDataLPT>& CollectionRef : InOutEntry.Collections)
			{
				if (CollectionRef.ToSoftObjectPath() == CollectionPath)
				{
					bExistsInEntry = true;
					break;
				}
			}

			if (!bExistsInEntry)
			{
				InOutEntry.Collections.Add(CollectionAsset);
				bEntryModified = true;
			}
		}

		return bEntryModified;
	}

	bool ResolveCollectionTargetDataLayerAssetsFromNames(UWorld* SavedWorld, UAssetCollectionDataLPT* CollectionAsset)
	{
		if (!CollectionAsset)
		{
			return false;
		}

		bool bModified = false;

		UDataLayerManager* DataLayerManager = SavedWorld ? SavedWorld->GetDataLayerManager() : nullptr;
		for (const FName DataLayerNameRule : CollectionAsset->TargetDataLayerNames)
		{
			if (DataLayerNameRule.IsNone())
			{
				continue;
			}

			const UDataLayerInstance* ResolvedDataLayerInstance = ResolveDataLayerInstanceByName(DataLayerManager, DataLayerNameRule);
			if (!ResolvedDataLayerInstance)
			{
				continue;
			}

			if (const UDataLayerAsset* ResolvedDataLayerAsset = ResolvedDataLayerInstance->GetAsset())
			{
				const TSoftObjectPtr<UDataLayerAsset> ResolvedDataLayerAssetRef{ FSoftObjectPath(ResolvedDataLayerAsset) };
				bModified |= AddUniqueDataLayerAssetRule(CollectionAsset->TargetDataLayers, ResolvedDataLayerAssetRef);
			}
		}

		return bModified;
	}

	FLPTFilterSettings BuildCollectionEffectiveRules(const FLPTFilterSettings& BaseRules, const UAssetCollectionDataLPT* CollectionAsset, const bool bIsWorldPartition)
	{
		if (!CollectionAsset || !bIsWorldPartition)
		{
			return BaseRules;
		}

		FLPTFilterSettings CollectionRules = BaseRules;
		CollectionRules.WorldPartitionDataLayerAssets = AssetFilterLPT::MergeDataLayerAssetRules(BaseRules.WorldPartitionDataLayerAssets, CollectionAsset->TargetDataLayers);
		CollectionRules.WorldPartitionRegions = AssetFilterLPT::MergeNameRules(BaseRules.WorldPartitionRegions, CollectionAsset->TargetDataLayerNames);
		CollectionRules.WorldPartitionCells = AssetFilterLPT::MergeStringRules(BaseRules.WorldPartitionCells, CollectionAsset->TargetCellRules);
		return CollectionRules;
	}

	TArray<FSoftObjectPath> BuildFilteredAssetsForRules(
		UWorld* SavedWorld,
		IAssetRegistry& Registry,
		const FLPTFilterSettings& EffectiveRules)
	{
		TArray<FSoftObjectPath> CandidateAssets;
		TSet<FSoftObjectPath> UniqueCandidateAssets;
		TArray<FName> TraversalRootPackages;
		const bool bIsWorldPartition = SavedWorld && SavedWorld->IsPartitionedWorld();

		if (!bIsWorldPartition)
		{
			const FName LevelPackageName = FName(*SavedWorld->GetOutermost()->GetName());
			TraversalRootPackages = { LevelPackageName };
			AssetCollectorLPT::AppendHardDependencyClosureAssets(Registry, TraversalRootPackages, UniqueCandidateAssets, CandidateAssets, &EffectiveRules);
		}
		else
		{
			FLPTFilterSettings WorldPartitionScanRules = EffectiveRules;
			const bool bHasWorldPartitionScopeRule = HasAnyWorldPartitionScopeRule(WorldPartitionScanRules);
			const bool bShouldScanAllActors = WorldPartitionScanRules.bAllowWorldPartitionUnscopedAutoScan;
			if (bHasWorldPartitionScopeRule || bShouldScanAllActors)
			{
				DataLayerResolverLPT::ResolveWorldPartitionRegionRulesAsDataLayers(SavedWorld, WorldPartitionScanRules);

				TSet<FName> CandidateActorPackages;
				AssetCollectorLPT::CollectWorldPartitionActorPackages(SavedWorld, WorldPartitionScanRules, CandidateActorPackages);

				TraversalRootPackages = CandidateActorPackages.Array();
				TraversalRootPackages.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
				AssetCollectorLPT::AppendHardDependencyClosureAssets(Registry, TraversalRootPackages, UniqueCandidateAssets, CandidateAssets, &EffectiveRules);
			}
			else
			{
				UE_LOG(LogLPTEditor, Verbose, TEXT("World Partition actor scan skipped for '%s': no Data Layer/Cell scope found and unscoped auto scan is disabled."),
					SavedWorld ? *SavedWorld->GetOutermost()->GetName() : TEXT("UnknownWorld")
				);
			}

			AssetCollectorLPT::AppendExplicitAssetRuleCandidates(EffectiveRules, UniqueCandidateAssets, CandidateAssets);
		}

		FLPTFilterSettings FinalFilterRules = EffectiveRules;
		if (bIsWorldPartition)
		{
			FinalFilterRules.WorldPartitionDataLayerAssets.Empty();
			FinalFilterRules.WorldPartitionRegions.Empty();
			FinalFilterRules.WorldPartitionCells.Empty();
		}

		if (bIsWorldPartition && !FinalFilterRules.bUseExclusionMode)
		{
			AssetCollectorLPT::AppendFolderRuleCandidates(Registry, FinalFilterRules, UniqueCandidateAssets, CandidateAssets);
		}

		FLPTFilterSettings PostExpansionFilterRules = FinalFilterRules;
		const bool bHasAssetOrFolderRules = FinalFilterRules.AssetRules.Num() > 0 || FinalFilterRules.FolderRules.Num() > 0;
		if (!FinalFilterRules.bUseExclusionMode && bHasAssetOrFolderRules)
		{
			const TArray<FSoftObjectPath> RuleSeedAssets = ULevelPreloadAssetFilter::FilterAssets(CandidateAssets, &FinalFilterRules);

			TSet<FName> RuleSeedPackages;
			RuleSeedPackages.Reserve(RuleSeedAssets.Num());
			for (const FSoftObjectPath& RuleSeedAsset : RuleSeedAssets)
			{
				const FString RuleSeedPackageName = RuleSeedAsset.GetLongPackageName();
				if (!RuleSeedPackageName.IsEmpty())
				{
					RuleSeedPackages.Add(FName(*RuleSeedPackageName));
				}
			}

			UniqueCandidateAssets.Reset();
			CandidateAssets.Reset();

			if (RuleSeedPackages.Num() > 0)
			{
				const TArray<FName> RuleSeedPackageArray = RuleSeedPackages.Array();
				AssetCollectorLPT::AppendHardDependencyClosureAssets(Registry, RuleSeedPackageArray, UniqueCandidateAssets, CandidateAssets, &FinalFilterRules);
			}

			PostExpansionFilterRules.AssetRules.Empty();
			PostExpansionFilterRules.FolderRules.Empty();
		}
		else if (FinalFilterRules.bUseExclusionMode && bHasAssetOrFolderRules)
		{
			PruneExcludedDependencyBranches(Registry, TraversalRootPackages, FinalFilterRules, CandidateAssets);
		}

		TArray<FSoftObjectPath> FilteredAssets = ULevelPreloadAssetFilter::FilterAssets(CandidateAssets, &PostExpansionFilterRules);
		FilteredAssets.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B)
		{
			return A.ToString() < B.ToString();
		});
		return FilteredAssets;
	}
}
