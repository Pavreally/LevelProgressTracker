// Pavel Gornostaev <https://github.com/Pavreally>

#include "EditorModuleLPT.h"

#include "EditorModuleGenerationLPT.h"

#include "AssetUtilsLPT.h"
#include "DatabaseLPT.h"
#include "LevelPreloadAssetFilter.h"
#include "LevelPreloadDatabaseLPT.h"
#include "AssetCollectionDataLPT.h"
#include "AssetFilterSettingsLPT.h"
#include "LogLPTEditor.h"
#include "SettingsLPT.h"
#include "SlateWidgetLPT.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Brushes/SlateImageBrush.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "Framework/Commands/UIAction.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

DEFINE_LOG_CATEGORY(LogLPTEditor);

namespace
{
	void CopyFilterSettings(const UAssetFilterSettingsLPT* Source, UAssetFilterSettingsLPT* Destination)
	{
		if (!Source || !Destination || Source == Destination)
		{
			return;
		}

		Destination->AssetClassFilter = Source->AssetClassFilter;
		Destination->bUseExclusionMode = Source->bUseExclusionMode;
		Destination->AssetRules = Source->AssetRules;
		Destination->FolderRules = Source->FolderRules;
		Destination->bUseChunkedPreload = Source->bUseChunkedPreload;
		Destination->PreloadChunkSize = Source->PreloadChunkSize;
		Destination->bAllowWorldPartitionAutoScan = Source->bAllowWorldPartitionAutoScan;
		Destination->bAllowWorldPartitionUnscopedAutoScan = Source->bAllowWorldPartitionUnscopedAutoScan;
		Destination->bAutoCreateSublevelCollections = Source->bAutoCreateSublevelCollections;
		Destination->CollectionPresets = Source->CollectionPresets;
	}

	bool RefreshCollectionPresetAssetCounts(UAssetFilterSettingsLPT* FilterSettingsAsset, const FLevelPreloadEntryLPT& Entry)
	{
		if (!FilterSettingsAsset)
		{
			return false;
		}

		bool bModified = false;
		for (FLPTCollectionPresetLPT& Preset : FilterSettingsAsset->CollectionPresets)
		{
			const FName PresetKey = Preset.CollectionKey.IsNone() ? FName(TEXT("Default")) : Preset.CollectionKey;
			int32 SavedAssetCount = 0;
			for (const TSoftObjectPtr<UAssetCollectionDataLPT>& CollectionRef : Entry.Collections)
			{
				const UAssetCollectionDataLPT* CollectionAsset = CollectionRef.LoadSynchronous();
				if (!CollectionAsset || CollectionAsset->CollectionKey != PresetKey)
				{
					continue;
				}

				SavedAssetCount = CollectionAsset->AssetList.Num();
				break;
			}

			if (Preset.GeneratedAssetCount != SavedAssetCount)
			{
				Preset.GeneratedAssetCount = SavedAssetCount;
				bModified = true;
			}
		}

		return bModified;
	}

	bool AddSublevelCollectionPresets(UWorld* ParentWorld, UAssetFilterSettingsLPT* FilterSettingsAsset)
	{
		if (!ParentWorld || ParentWorld->IsPartitionedWorld() || !FilterSettingsAsset)
		{
			return false;
		}

		const FString ParentPackageName = UWorld::RemovePIEPrefix(ParentWorld->GetOutermost()->GetName());
		TSet<FSoftObjectPath> ExistingSublevelPaths;
		TSet<FName> UsedCollectionKeys;
		for (const FLPTCollectionPresetLPT& Preset : FilterSettingsAsset->CollectionPresets)
		{
			const FSoftObjectPath TargetPath = Preset.TargetSublevel.ToSoftObjectPath();
			if (TargetPath.IsValid())
			{
				ExistingSublevelPaths.Add(TargetPath);
			}
			if (!Preset.CollectionKey.IsNone())
			{
				UsedCollectionKeys.Add(Preset.CollectionKey);
			}
		}

		TSet<FSoftObjectPath> UniqueSublevelPaths;
		TArray<FSoftObjectPath> SublevelPaths;
		for (const ULevelStreaming* StreamingLevel : ParentWorld->GetStreamingLevels())
		{
			if (!StreamingLevel)
			{
				continue;
			}

			const FSoftObjectPath SublevelPath = StreamingLevel->GetWorldAsset().ToSoftObjectPath();
			const FString SublevelPackageName = SublevelPath.GetLongPackageName();
			if (!SublevelPath.IsValid() || SublevelPackageName.IsEmpty() ||
				SublevelPackageName == ParentPackageName ||
				UniqueSublevelPaths.Contains(SublevelPath))
			{
				continue;
			}

			UniqueSublevelPaths.Add(SublevelPath);
			SublevelPaths.Add(SublevelPath);
		}

		SublevelPaths.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B)
		{
			return A.ToString() < B.ToString();
		});

		bool bAddedPresets = false;
		for (const FSoftObjectPath& SublevelPath : SublevelPaths)
		{
			if (ExistingSublevelPaths.Contains(SublevelPath))
			{
				continue;
			}

			FString SublevelName = FPackageName::GetLongPackageAssetName(SublevelPath.GetLongPackageName());
			if (SublevelName.IsEmpty())
			{
				SublevelName = SublevelPath.GetAssetName();
			}
			if (SublevelName.IsEmpty())
			{
				continue;
			}

			const FString BaseKey = FString::Printf(TEXT("Sublevel_%s"), *SublevelName);
			FString UniqueKeyString = BaseKey;
			int32 Suffix = 2;
			while (UsedCollectionKeys.Contains(FName(*UniqueKeyString)))
			{
				UniqueKeyString = FString::Printf(TEXT("%s_%d"), *BaseKey, Suffix++);
			}

			FLPTCollectionPresetLPT& NewPreset = FilterSettingsAsset->CollectionPresets.AddDefaulted_GetRef();
			NewPreset.CollectionKey = FName(*UniqueKeyString);
			NewPreset.bAutoGenerate = true;
			NewPreset.TargetSublevel = TSoftObjectPtr<UWorld>(SublevelPath);
			ExistingSublevelPaths.Add(SublevelPath);
			UsedCollectionKeys.Add(NewPreset.CollectionKey);
			bAddedPresets = true;
		}

		if (bAddedPresets && FilterSettingsAsset->GetOutermost() != GetTransientPackage())
		{
			FilterSettingsAsset->Modify();
			FilterSettingsAsset->MarkPackageDirty();
			FilterSettingsAsset->GetOutermost()->MarkPackageDirty();
		}
		return bAddedPresets;
	}

	bool HasStreamingSublevels(const UWorld* ParentWorld)
	{
		if (!ParentWorld || ParentWorld->IsPartitionedWorld())
		{
			return false;
		}

		const FString ParentPackageName = UWorld::RemovePIEPrefix(ParentWorld->GetOutermost()->GetName());
		for (const ULevelStreaming* StreamingLevel : ParentWorld->GetStreamingLevels())
		{
			if (StreamingLevel)
			{
				const FSoftObjectPath SublevelPath = StreamingLevel->GetWorldAsset().ToSoftObjectPath();
				const FString SublevelPackageName = SublevelPath.GetLongPackageName();
				if (SublevelPath.IsValid() && !SublevelPackageName.IsEmpty() && SublevelPackageName != ParentPackageName)
				{
					return true;
				}
			}
		}

		return false;
	}

	UWorld* GetCurrentEditorWorldForLevel(const TSoftObjectPtr<UWorld>& LevelSoftPtr)
	{
		if (!GEditor)
		{
			return nullptr;
		}

		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (!EditorWorld)
		{
			return nullptr;
		}

		const FString CurrentPackageName = UWorld::RemovePIEPrefix(EditorWorld->GetOutermost()->GetName());
		const FString RequestedPackageName = LevelSoftPtr.ToSoftObjectPath().GetLongPackageName();
		return CurrentPackageName.Equals(RequestedPackageName, ESearchCase::IgnoreCase) ? EditorWorld : nullptr;
	}
}

void FLevelProgressTrackerEditorModule::StartupModule()
{
#if WITH_EDITOR
	UE_LOG(LogLPTEditor, Log, TEXT("StartupModule."));
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
	UE_LOG(LogLPTEditor, Log, TEXT("ShutdownModule."));
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
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to find plugin directory for style registration."));
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(EditorModuleLPTPrivate::StyleSetName);
	StyleSet->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

	const FVector2f Icon20(20.f, 20.f);
	const FVector2f Icon40(40.f, 40.f);
	const FString IconPath = StyleSet->RootToContentDir(TEXT("IconLPT"), TEXT(".svg"));

	StyleSet->Set(
		EditorModuleLPTPrivate::ToolbarIconName,
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
		UE_LOG(LogLPTEditor, Verbose, TEXT("Tool menu UI is disabled. Skipping menu registration."));
		return;
	}

	UToolMenus* ToolMenus = UToolMenus::Get();

	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolbarMenu = ToolMenus->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.AssetsToolBar"));
	if (!ToolbarMenu)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to extend LevelEditor toolbar menu."));
		return;
	}

	ToolMenus->RemoveEntry(TEXT("LevelEditor.LevelEditorToolBar.AssetsToolBar"), TEXT("Content"), TEXT("LPT_OpenLevelRules"));

	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection(TEXT("Content"));
	FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
		TEXT("LPT_OpenLevelRules"),
		FUIAction(FExecuteAction::CreateRaw(this, &FLevelProgressTrackerEditorModule::HandleToolbarOpenLevelRulesClicked)),
		FText::FromString(TEXT("LPT Rules")),
		FText::FromString(TEXT("Open per-level filter settings DataAsset for the currently opened level. Collection presets are configured in that asset.")),
		FSlateIcon(
			EditorModuleLPTPrivate::StyleSetName,
			EditorModuleLPTPrivate::ToolbarIconName,
			TEXT("LevelProgressTracker.LPTRules.Small")
		)
	);
	Entry.InsertPosition = FToolMenuInsert(TEXT("EditCinematics"), EToolMenuInsertType::After);
	Section.AddEntry(Entry);

	UE_LOG(LogLPTEditor, Log, TEXT("Registered toolbar button 'LPT Rules'."));
	ToolMenus->RefreshAllWidgets();
}

void FLevelProgressTrackerEditorModule::HandleToolbarOpenLevelRulesClicked()
{
	UE_LOG(LogLPTEditor, Log, TEXT("Toolbar button clicked."));
	HandleOpenLevelRulesEditorRequested(GetMutableDefault<ULevelProgressTrackerSettings>());
}

void FLevelProgressTrackerEditorModule::OnPackageSaved(const FString& PackageFilename, UPackage* SavedPackage, FObjectPostSaveContext SaveContext)
{
	if (IsRunningCookCommandlet())
	{
		// PackageSavedWithContextEvent is also emitted by Cook. LPT generation is
		// an editor-only mutation and must never create or save assets during Cook.
		return;
	}

	if (bIsGenerating)
	{
		// Rebuilding saves LPT assets, which emits this event recursively.
		return;
	}

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
		if (!GEditor)
		{
			return;
		}

		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (!EditorWorld || !EditorWorld->IsPartitionedWorld())
		{
			return;
		}

		if (!AssetUtilsLPT::IsExternalPackageOfWorldPartitionLevel(SavedPackageName, EditorWorld))
		{
			return;
		}

		UE_LOG(LogLPTEditor, Log, TEXT("Detected WP external package save '%s'. Checking LPT state for '%s'."),
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

bool FLevelProgressTrackerEditorModule::RebuildLevelDependencies(UWorld* SavedWorld, const bool bForceGeneration)
{
	if (!SavedWorld)
	{
		return false;
	}

	const ULevelProgressTrackerSettings* Settings = GetDefault<ULevelProgressTrackerSettings>();
	if (!Settings)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Project settings are not available. Skipping database generation."));
		return false;
	}

	const FString LevelPackagePath = SavedWorld->GetOutermost()->GetName();
	const FString LevelAssetName = FPackageName::GetLongPackageAssetName(LevelPackagePath);
	const FSoftObjectPath LevelObjectPath(FString::Printf(TEXT("%s.%s"), *LevelPackagePath, *LevelAssetName));
	const TSoftObjectPtr<UWorld> LevelSoftPtr(LevelObjectPath);

	TGuardValue<bool> GenerationGuard(bIsGenerating, true);

	ULevelPreloadDatabaseLPT* DatabaseAsset = GetOrCreateDatabaseAsset(Settings);
	if (!DatabaseAsset)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create or load LevelPreloadDatabaseLPT asset."));
		return false;
	}

	const int32 LevelCountBeforeNormalization = DatabaseAsset->Levels.Num();
	bool bWasEntryAdded = false;
	FLevelPreloadEntryLPT* LevelEntry = DatabaseAsset->FindOrAddEntryByLevel(LevelSoftPtr, bWasEntryAdded);
	if (!LevelEntry)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create or resolve database entry for '%s'."), *LevelPackagePath);
		return false;
	}
	const bool bDatabaseStructureChanged = DatabaseAsset->Levels.Num() != LevelCountBeforeNormalization;

	UAssetFilterSettingsLPT* FilterSettingsAsset = LevelEntry->FilterSettings.LoadSynchronous();
	bool bFilterSettingsCreated = false;
	if (!FilterSettingsAsset)
	{
		FilterSettingsAsset = EditorModuleLPTPrivate::GetOrCreateFilterSettingsAsset(Settings, LevelAssetName);
		if (!FilterSettingsAsset)
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create filter settings DataAsset for level '%s'."), *LevelPackagePath);
			return false;
		}

		LevelEntry->FilterSettings = FilterSettingsAsset;
		bFilterSettingsCreated = true;
	}

	const int32 CollectionCountBeforeNormalization = LevelEntry->Collections.Num();
	ULevelPreloadDatabaseLPT::DeduplicateCollections(*LevelEntry);
	bool bEntryCollectionsChanged = LevelEntry->Collections.Num() != CollectionCountBeforeNormalization;
	const bool bSublevelPresetsAdded = FilterSettingsAsset->bAutoCreateSublevelCollections &&
		AddSublevelCollectionPresets(SavedWorld, FilterSettingsAsset);

	const bool bCollectionsMaterialized = EditorModuleLPTPrivate::MaterializeCollectionPresets(
		Settings,
		LevelAssetName,
		FilterSettingsAsset,
		*LevelEntry
	);
	bEntryCollectionsChanged |= bCollectionsMaterialized;

	if (LevelEntry->Collections.IsEmpty())
	{
		if (UAssetCollectionDataLPT* DefaultCollectionAsset = EditorModuleLPTPrivate::GetOrCreateCollectionAsset(Settings, LevelAssetName, EditorModuleLPTPrivate::DefaultCollectionKey))
		{
			LevelEntry->Collections.Add(DefaultCollectionAsset);
		}
		else
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create default collection asset for level '%s'."), *LevelPackagePath);
		}
	}

	bEntryCollectionsChanged |= LevelEntry->Collections.Num() != CollectionCountBeforeNormalization;

	const bool bIsWorldPartition = SavedWorld->IsPartitionedWorld();
	const FLPTFilterSettings BaseRules = FilterSettingsAsset ? FilterSettingsAsset->ToFilterSettings() : FLPTFilterSettings();

	bool bCollectionPreparationChanged = false;
	bool bCollectionPreparationSavesSucceeded = true;
	bool bFilterSettingsSavesSucceeded = true;
	for (const TSoftObjectPtr<UAssetCollectionDataLPT>& CollectionRef : LevelEntry->Collections)
	{
		UAssetCollectionDataLPT* CollectionAsset = CollectionRef.LoadSynchronous();
		if (!CollectionAsset)
		{
			continue;
		}

		CollectionAsset->Modify();
		bool bCollectionPreparationModified = EditorModuleLPTPrivate::ResolveCollectionTargetDataLayerAssetsFromNames(SavedWorld, CollectionAsset);
		bCollectionPreparationModified |= EditorModuleLPTPrivate::DeduplicateCollectionAssetData(CollectionAsset);
		bCollectionPreparationChanged |= bCollectionPreparationModified;
		if (bCollectionPreparationModified)
		{
			CollectionAsset->MarkPackageDirty();
			CollectionAsset->GetOutermost()->MarkPackageDirty();
			if (!EditorModuleLPTPrivate::SaveAssetObject(CollectionAsset))
			{
				bCollectionPreparationSavesSucceeded = false;
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save prepared collection asset '%s'."), *CollectionAsset->GetPathName());
			}
		}
	}

	const bool bPresetCountsChanged = RefreshCollectionPresetAssetCounts(FilterSettingsAsset, *LevelEntry);
	const bool bFilterSettingsNeedSave = bPresetCountsChanged || bSublevelPresetsAdded || bFilterSettingsCreated ||
		(FilterSettingsAsset->GetOutermost() && FilterSettingsAsset->GetOutermost()->IsDirty());
	if (bFilterSettingsNeedSave)
	{
		FilterSettingsAsset->Modify();
		FilterSettingsAsset->MarkPackageDirty();
		FilterSettingsAsset->GetOutermost()->MarkPackageDirty();
		if (!EditorModuleLPTPrivate::SaveAssetObject(FilterSettingsAsset))
		{
			bFilterSettingsSavesSucceeded = false;
			UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save collection preset asset counts for '%s'."), *FilterSettingsAsset->GetPathName());
		}
	}

	const uint32 CurrentLevelStateHash = EditorModuleLPTPrivate::ComputeLevelStateHash(SavedWorld, BaseRules);
	const bool bLevelStateChanged = LevelEntry->LevelStateHash != CurrentLevelStateHash;
	const bool bNeedsGeneration = bDatabaseStructureChanged ||
		bWasEntryAdded ||
		bFilterSettingsCreated ||
		bEntryCollectionsChanged ||
		bSublevelPresetsAdded ||
		bCollectionPreparationChanged ||
		bPresetCountsChanged ||
		bLevelStateChanged ||
		bForceGeneration;

	if (!bNeedsGeneration)
	{
		UE_LOG(LogLPTEditor, Verbose, TEXT("Skipping unchanged LPT generation for '%s' (LevelStateHash=%u)."),
			*LevelPackagePath,
			CurrentLevelStateHash
		);
		return true;
	}
	DatabaseAsset->Modify();

	LevelEntry->LevelStateHash = CurrentLevelStateHash;
	LevelEntry->GenerationTimestamp = FDateTime::UtcNow();

	if (bIsWorldPartition && !BaseRules.bAllowWorldPartitionAutoScan)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("World Partition auto scan is disabled in filter settings. Skipping collection auto-generation for '%s'."),
			*SavedWorld->GetOutermost()->GetName()
		);

		DatabaseAsset->MarkPackageDirty();
		DatabaseAsset->GetOutermost()->MarkPackageDirty();
		return bCollectionPreparationSavesSucceeded && bFilterSettingsSavesSucceeded && SaveDatabaseAsset(DatabaseAsset);
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = AssetRegistryModule.Get();

	bool bCollectionSavesSucceeded = true;
	for (const TSoftObjectPtr<UAssetCollectionDataLPT>& CollectionRef : LevelEntry->Collections)
	{
		UAssetCollectionDataLPT* CollectionAsset = CollectionRef.LoadSynchronous();
		if (!CollectionAsset)
		{
			continue;
		}

		bool bCollectionModified = false;
		CollectionAsset->Modify();

		if (EditorModuleLPTPrivate::ResolveCollectionTargetDataLayerAssetsFromNames(SavedWorld, CollectionAsset))
		{
			bCollectionModified = true;
		}

		if (EditorModuleLPTPrivate::DeduplicateCollectionAssetData(CollectionAsset))
		{
			bCollectionModified = true;
		}

		const FLPTFilterSettings CollectionRules = EditorModuleLPTPrivate::BuildCollectionEffectiveRules(BaseRules, CollectionAsset, bIsWorldPartition);

		if (CollectionAsset->bAutoGenerate)
		{
			bool bTargetLevelResolved = false;
			const TArray<FSoftObjectPath> GeneratedAssetList = EditorModuleLPTPrivate::BuildFilteredAssetsForRules(
				SavedWorld,
				Registry,
				CollectionRules,
				CollectionAsset->TargetSublevel.ToSoftObjectPath(),
				bTargetLevelResolved
			);
			if (!bTargetLevelResolved)
			{
				bCollectionSavesSucceeded = false;
				continue;
			}

			if (CollectionAsset->AssetList != GeneratedAssetList)
			{
				CollectionAsset->AssetList = GeneratedAssetList;
				bCollectionModified = true;
			}
		}
		const uint32 NewCollectionHash = EditorModuleLPTPrivate::ComputeCollectionContentHash(CollectionAsset, CollectionRules);
		if (CollectionAsset->CollectionContentHash != NewCollectionHash)
		{
			CollectionAsset->CollectionContentHash = NewCollectionHash;
			bCollectionModified = true;
		}

		if (bCollectionModified)
		{
			CollectionAsset->MarkPackageDirty();
			CollectionAsset->GetOutermost()->MarkPackageDirty();

			if (!EditorModuleLPTPrivate::SaveAssetObject(CollectionAsset))
			{
				bCollectionSavesSucceeded = false;
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save collection asset '%s'."), *CollectionAsset->GetPathName());
			}
		}
	}

	if (RefreshCollectionPresetAssetCounts(FilterSettingsAsset, *LevelEntry))
	{
		FilterSettingsAsset->Modify();
		FilterSettingsAsset->MarkPackageDirty();
		FilterSettingsAsset->GetOutermost()->MarkPackageDirty();
		if (!EditorModuleLPTPrivate::SaveAssetObject(FilterSettingsAsset))
		{
			bFilterSettingsSavesSucceeded = false;
			UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save generated collection counts for '%s'."), *FilterSettingsAsset->GetPathName());
		}
	}

	DatabaseAsset->MarkPackageDirty();
	DatabaseAsset->GetOutermost()->MarkPackageDirty();

	const bool bDatabaseSaved = SaveDatabaseAsset(DatabaseAsset);
	if (!bDatabaseSaved)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save LevelPreloadDatabaseLPT after updating '%s'."),
			*LevelPackagePath
		);
	}
	return bCollectionPreparationSavesSucceeded && bFilterSettingsSavesSucceeded && bCollectionSavesSucceeded && bDatabaseSaved;
}

void FLevelProgressTrackerEditorModule::OpenLevelRulesWindow(
	ULevelPreloadDatabaseLPT* DatabaseAsset,
	UAssetFilterSettingsLPT* FilterSettingsAsset,
	const TSoftObjectPtr<UWorld>& LevelSoftPtr,
	const FString& LevelDisplayName,
	const bool bIsWorldPartition,
	const bool bHasSublevels)
{
	SlateWidgetLPT::OpenLevelRulesWindow(
		DatabaseAsset,
		FilterSettingsAsset,
		LevelDisplayName,
		bIsWorldPartition,
		bHasSublevels,
		[this, LevelSoftPtr, LevelDisplayName](ULevelPreloadDatabaseLPT* InDatabaseAsset, UAssetFilterSettingsLPT* InFilterSettingsAsset)
		{
			if (!InFilterSettingsAsset)
			{
				return false;
			}

			UWorld* EditorWorld = GetCurrentEditorWorldForLevel(LevelSoftPtr);
			if (!EditorWorld)
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("The editor's current level no longer matches the open LPT window for '%s'. Reopen the LPT window for the active level before saving."), *LevelDisplayName);
				return false;
			}

			const ULevelProgressTrackerSettings* ProjectSettings = GetDefault<ULevelProgressTrackerSettings>();
			ULevelPreloadDatabaseLPT* PersistentDatabase = InDatabaseAsset ? InDatabaseAsset : GetOrCreateDatabaseAsset(ProjectSettings);
			if (!ProjectSettings || !PersistentDatabase)
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create or load the LPT database while saving level '%s'."), *LevelDisplayName);
				return false;
			}

			bool bWasEntryAdded = false;
			FLevelPreloadEntryLPT* Entry = PersistentDatabase->FindOrAddEntryByLevel(LevelSoftPtr, bWasEntryAdded);
			if (!Entry)
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create LPT level entry for '%s'."), *LevelDisplayName);
				return false;
			}

			UAssetFilterSettingsLPT* PersistentFilterSettings = Entry->FilterSettings.LoadSynchronous();
			if (!PersistentFilterSettings)
			{
				PersistentFilterSettings = EditorModuleLPTPrivate::GetOrCreateFilterSettingsAsset(ProjectSettings, LevelDisplayName);
				if (!PersistentFilterSettings)
				{
					UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create LPT filter settings for '%s'."), *LevelDisplayName);
					return false;
				}
				Entry->FilterSettings = PersistentFilterSettings;
			}

			PersistentFilterSettings->Modify();
			CopyFilterSettings(InFilterSettingsAsset, PersistentFilterSettings);
			if (PersistentFilterSettings->bAutoCreateSublevelCollections)
			{
				AddSublevelCollectionPresets(EditorWorld, PersistentFilterSettings);
			}
			EditorModuleLPTPrivate::MaterializeCollectionPresets(ProjectSettings, LevelDisplayName, PersistentFilterSettings, *Entry);

			if (Entry->Collections.IsEmpty())
			{
				if (UAssetCollectionDataLPT* DefaultCollectionAsset = EditorModuleLPTPrivate::GetOrCreateCollectionAsset(
					ProjectSettings, LevelDisplayName, EditorModuleLPTPrivate::DefaultCollectionKey))
				{
					Entry->Collections.Add(DefaultCollectionAsset);
				}
			}

			RefreshCollectionPresetAssetCounts(PersistentFilterSettings, *Entry);
			PersistentFilterSettings->MarkPackageDirty();
			PersistentFilterSettings->GetOutermost()->MarkPackageDirty();
			if (!EditorModuleLPTPrivate::SaveAssetObject(PersistentFilterSettings))
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save LPT filter settings '%s'."), *PersistentFilterSettings->GetPathName());
				return false;
			}

			PersistentDatabase->Modify();
			PersistentDatabase->MarkPackageDirty();
			PersistentDatabase->GetOutermost()->MarkPackageDirty();
			(void)bWasEntryAdded;
			if (!SaveDatabaseAsset(PersistentDatabase))
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save LPT database before generation."));
				return false;
			}

			return RebuildLevelDependencies(EditorWorld, true);
		},
		[this, LevelSoftPtr](UAssetFilterSettingsLPT* InFilterSettingsAsset)
		{
			return AddAllWorldPartitionDataLayersToPresets(GetCurrentEditorWorldForLevel(LevelSoftPtr), InFilterSettingsAsset);
		},
		[this, LevelSoftPtr](UAssetFilterSettingsLPT* InFilterSettingsAsset)
		{
			return AddSublevelCollectionPresets(GetCurrentEditorWorldForLevel(LevelSoftPtr), InFilterSettingsAsset);
		}
	);
}

bool FLevelProgressTrackerEditorModule::AddAllWorldPartitionDataLayersToPresets(UWorld* EditorWorld, UAssetFilterSettingsLPT* FilterSettingsAsset) const
{
	if (!FilterSettingsAsset ||
		!FilterSettingsAsset->bAllowWorldPartitionAutoScan ||
		!FilterSettingsAsset->bAllowWorldPartitionUnscopedAutoScan)
	{
		return false;
	}

	UDataLayerManager* DataLayerManager = EditorWorld ? EditorWorld->GetDataLayerManager() : nullptr;
	if (!EditorWorld || !EditorWorld->IsPartitionedWorld() || !DataLayerManager)
	{
		return false;
	}

	TArray<const UDataLayerAsset*> DataLayerAssets;
	TSet<FSoftObjectPath> UniqueDataLayerPaths;
	for (const UDataLayerInstance* DataLayerInstance : DataLayerManager->GetDataLayerInstances())
	{
		const UDataLayerAsset* DataLayerAsset = DataLayerInstance ? DataLayerInstance->GetAsset() : nullptr;
		if (!DataLayerAsset)
		{
			continue;
		}

		const FSoftObjectPath DataLayerPath(DataLayerAsset);
		if (DataLayerPath.IsValid() && !UniqueDataLayerPaths.Contains(DataLayerPath))
		{
			UniqueDataLayerPaths.Add(DataLayerPath);
			DataLayerAssets.Add(DataLayerAsset);
		}
	}

	DataLayerAssets.Sort([](const UDataLayerAsset& A, const UDataLayerAsset& B)
	{
		return A.GetPathName() < B.GetPathName();
	});

	TSet<FName> UsedCollectionKeys;
	for (const FLPTCollectionPresetLPT& Preset : FilterSettingsAsset->CollectionPresets)
	{
		if (!Preset.CollectionKey.IsNone())
		{
			UsedCollectionKeys.Add(Preset.CollectionKey);
		}
	}

	bool bAddedPresets = false;
	for (const UDataLayerAsset* DataLayerAsset : DataLayerAssets)
	{
		if (!DataLayerAsset)
		{
			continue;
		}

		const FSoftObjectPath DataLayerPath(DataLayerAsset);
		bool bAlreadyCovered = false;
		for (const FLPTCollectionPresetLPT& Preset : FilterSettingsAsset->CollectionPresets)
		{
			for (const TSoftObjectPtr<UDataLayerAsset>& PresetDataLayer : Preset.TargetDataLayers)
			{
				if (PresetDataLayer.ToSoftObjectPath() == DataLayerPath)
				{
					bAlreadyCovered = true;
					break;
				}
			}
			if (bAlreadyCovered)
			{
				break;
			}
		}
		if (bAlreadyCovered)
		{
			continue;
		}

		const FString BaseKey = FString::Printf(TEXT("DataLayer_%s"), *DataLayerAsset->GetName());
		FString UniqueKeyString = BaseKey;
		int32 Suffix = 2;
		while (UsedCollectionKeys.Contains(FName(*UniqueKeyString)))
		{
			UniqueKeyString = FString::Printf(TEXT("%s_%d"), *BaseKey, Suffix++);
		}

		FLPTCollectionPresetLPT& NewPreset = FilterSettingsAsset->CollectionPresets.AddDefaulted_GetRef();
		NewPreset.CollectionKey = FName(*UniqueKeyString);
		NewPreset.bAutoGenerate = true;
		NewPreset.TargetDataLayers.Add(TSoftObjectPtr<UDataLayerAsset>(DataLayerPath));
		UsedCollectionKeys.Add(NewPreset.CollectionKey);
		bAddedPresets = true;
	}

	if (bAddedPresets)
	{
		if (FilterSettingsAsset->GetOutermost() != GetTransientPackage())
		{
			FilterSettingsAsset->Modify();
			FilterSettingsAsset->MarkPackageDirty();
			FilterSettingsAsset->GetOutermost()->MarkPackageDirty();
		}
	}
	return bAddedPresets;
}

void FLevelProgressTrackerEditorModule::HandleOpenLevelRulesEditorRequested(ULevelProgressTrackerSettings* Settings)
{
	if (SlateWidgetLPT::TryActivateOpenLevelRulesWindow())
	{
		return;
	}

	const auto ShowWarningDialog = [](const FString& Message)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("%s"), *Message);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
	};

	ULevelProgressTrackerSettings* EffectiveSettings = Settings ? Settings : GetMutableDefault<ULevelProgressTrackerSettings>();
	if (!EffectiveSettings)
	{
		ShowWarningDialog(TEXT("Project settings are not available. Cannot open level rules editor."));
		return;
	}

	TSoftObjectPtr<UWorld> LevelSoftPtr;
	FString LevelPackagePath;
	FString LevelDisplayName;
	bool bIsWorldPartition = false;
	if (!TryGetCurrentEditorLevel(LevelSoftPtr, LevelPackagePath, LevelDisplayName, bIsWorldPartition))
	{
		ShowWarningDialog(TEXT("Failed to resolve the currently opened level."));
		return;
	}
	const bool bHasSublevels = HasStreamingSublevels(GetCurrentEditorWorldForLevel(LevelSoftPtr));

	ULevelPreloadDatabaseLPT* DatabaseAsset = nullptr;
	FLevelPreloadEntryLPT* Entry = nullptr;
	UAssetFilterSettingsLPT* FilterSettingsAsset = nullptr;

	if (!EffectiveSettings->bAutoGenerateOnLevelSave)
	{
		FString DatabasePackagePath;
		FSoftObjectPath DatabaseObjectPath;
		if (!ULevelPreloadAssetFilter::ResolveDatabaseAssetPath(EffectiveSettings, DatabasePackagePath, DatabaseObjectPath))
		{
			ShowWarningDialog(TEXT("Invalid LPT database path in Project Settings."));
			return;
		}
		(void)DatabasePackagePath;

		const FString DatabaseLongPackageName = DatabaseObjectPath.GetLongPackageName();
		if (UPackage* LoadedDatabasePackage = FindPackage(nullptr, *DatabaseLongPackageName))
		{
			DatabaseAsset = FindObject<ULevelPreloadDatabaseLPT>(LoadedDatabasePackage, *DatabaseObjectPath.GetAssetName());
		}
		if (!DatabaseAsset && FPackageName::DoesPackageExist(DatabaseLongPackageName))
		{
			DatabaseAsset = LoadObject<ULevelPreloadDatabaseLPT>(nullptr, *DatabaseObjectPath.ToString());
		}
		Entry = DatabaseAsset ? DatabaseAsset->FindEntryByLevel(LevelSoftPtr) : nullptr;
		UAssetFilterSettingsLPT* ExistingSettings = Entry ? Entry->FilterSettings.LoadSynchronous() : nullptr;
		if (ExistingSettings)
		{
			FilterSettingsAsset = DuplicateObject<UAssetFilterSettingsLPT>(ExistingSettings, GetTransientPackage());
		}
		else
		{
			FilterSettingsAsset = NewObject<UAssetFilterSettingsLPT>(GetTransientPackage());
			FilterSettingsAsset->InitializeDefaultsFromProjectSettings(EffectiveSettings);
			FLPTCollectionPresetLPT& DefaultPreset = FilterSettingsAsset->CollectionPresets.AddDefaulted_GetRef();
			DefaultPreset.CollectionKey = EditorModuleLPTPrivate::DefaultCollectionKey;
			DefaultPreset.bAutoGenerate = true;
		}

		if (!FilterSettingsAsset)
		{
			ShowWarningDialog(TEXT("Failed to prepare temporary LPT settings for the current level."));
			return;
		}

		if (Entry)
		{
			RefreshCollectionPresetAssetCounts(FilterSettingsAsset, *Entry);
		}

		OpenLevelRulesWindow(DatabaseAsset, FilterSettingsAsset, LevelSoftPtr, LevelDisplayName, bIsWorldPartition, bHasSublevels);
		return;
	}

	DatabaseAsset = GetOrCreateDatabaseAsset(EffectiveSettings);
	if (!DatabaseAsset)
	{
		ShowWarningDialog(TEXT("LevelPreloadDatabaseLPT asset could not be created or loaded. Check the database folder in LPT Project Settings."));
		return;
	}

	bool bWasAdded = false;
	Entry = DatabaseAsset->FindOrAddEntryByLevel(LevelSoftPtr, bWasAdded);
	if (!Entry)
	{
		ShowWarningDialog(FString::Printf(TEXT("Failed to create level rules entry for '%s'."), *LevelPackagePath));
		return;
	}

	bool bDatabaseModified = bWasAdded;
	DatabaseAsset->Modify();
	FilterSettingsAsset = Entry->FilterSettings.LoadSynchronous();
	if (!FilterSettingsAsset)
	{
		FilterSettingsAsset = EditorModuleLPTPrivate::GetOrCreateFilterSettingsAsset(EffectiveSettings, LevelDisplayName);
		if (!FilterSettingsAsset)
		{
			ShowWarningDialog(FString::Printf(TEXT("Failed to create filter settings asset for '%s'."), *LevelPackagePath));
			return;
		}

		Entry->FilterSettings = FilterSettingsAsset;
		bDatabaseModified = true;
	}

	const bool bSublevelPresetsAdded = FilterSettingsAsset->bAutoCreateSublevelCollections &&
		AddSublevelCollectionPresets(GetCurrentEditorWorldForLevel(LevelSoftPtr), FilterSettingsAsset);
	bDatabaseModified |= EditorModuleLPTPrivate::MaterializeCollectionPresets(EffectiveSettings, LevelDisplayName, FilterSettingsAsset, *Entry);

	if (Entry->Collections.IsEmpty())
	{
		if (UAssetCollectionDataLPT* DefaultCollectionAsset = EditorModuleLPTPrivate::GetOrCreateCollectionAsset(EffectiveSettings, LevelDisplayName, EditorModuleLPTPrivate::DefaultCollectionKey))
		{
			Entry->Collections.Add(DefaultCollectionAsset);
			bDatabaseModified = true;
		}
	}

	const int32 CollectionsBeforeDedupe = Entry->Collections.Num();
	ULevelPreloadDatabaseLPT::DeduplicateCollections(*Entry);
	bDatabaseModified |= (Entry->Collections.Num() != CollectionsBeforeDedupe);

	const bool bPresetCountsChanged = RefreshCollectionPresetAssetCounts(FilterSettingsAsset, *Entry);
	if (bSublevelPresetsAdded || bPresetCountsChanged)
	{
		FilterSettingsAsset->Modify();
		FilterSettingsAsset->MarkPackageDirty();
		FilterSettingsAsset->GetOutermost()->MarkPackageDirty();
		if (!EditorModuleLPTPrivate::SaveAssetObject(FilterSettingsAsset))
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save collection preset counts for '%s'."), *FilterSettingsAsset->GetPathName());
		}
	}

	if (bDatabaseModified)
	{
		DatabaseAsset->MarkPackageDirty();
		DatabaseAsset->GetOutermost()->MarkPackageDirty();
		if (!SaveDatabaseAsset(DatabaseAsset))
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save LPT database after preparing level '%s'."), *LevelPackagePath);
		}
	}

	OpenLevelRulesWindow(DatabaseAsset, FilterSettingsAsset, LevelSoftPtr, LevelDisplayName, bIsWorldPartition, bHasSublevels);
}

ULevelPreloadDatabaseLPT* FLevelProgressTrackerEditorModule::GetOrCreateDatabaseAsset(const ULevelProgressTrackerSettings* Settings) const
{
	return DatabaseLPT::GetOrCreateDatabaseAsset(Settings);
}

bool FLevelProgressTrackerEditorModule::SaveDatabaseAsset(ULevelPreloadDatabaseLPT* DatabaseAsset) const
{
	return DatabaseLPT::SaveDatabaseAsset(DatabaseAsset);
}
#endif

IMPLEMENT_MODULE(FLevelProgressTrackerEditorModule, LevelProgressTrackerEditor)

