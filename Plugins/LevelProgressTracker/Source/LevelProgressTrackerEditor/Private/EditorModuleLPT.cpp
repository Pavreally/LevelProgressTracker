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

DEFINE_LOG_CATEGORY(LogLPTEditor);

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

		UE_LOG(LogLPTEditor, Log, TEXT("Detected WP external package save '%s'. Rebuilding for '%s'."),
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
		UE_LOG(LogLPTEditor, Warning, TEXT("Project settings are not available. Skipping database generation."));
		return;
	}

	const FString LevelPackagePath = SavedWorld->GetOutermost()->GetName();
	const FString LevelAssetName = FPackageName::GetLongPackageAssetName(LevelPackagePath);
	const FSoftObjectPath LevelObjectPath(FString::Printf(TEXT("%s.%s"), *LevelPackagePath, *LevelAssetName));
	const TSoftObjectPtr<UWorld> LevelSoftPtr(LevelObjectPath);

	ULevelPreloadDatabaseLPT* DatabaseAsset = GetOrCreateDatabaseAsset(Settings);
	if (!DatabaseAsset)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create or load LevelPreloadDatabaseLPT asset."));
		return;
	}

	bool bWasEntryAdded = false;
	FLevelPreloadEntryLPT* LevelEntry = DatabaseAsset->FindOrAddEntryByLevel(LevelSoftPtr, bWasEntryAdded);
	if (!LevelEntry)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create or resolve database entry for '%s'."), *LevelPackagePath);
		return;
	}

	DatabaseAsset->Modify();

	UAssetFilterSettingsLPT* FilterSettingsAsset = LevelEntry->FilterSettings.LoadSynchronous();
	if (!FilterSettingsAsset)
	{
		FilterSettingsAsset = EditorModuleLPTPrivate::GetOrCreateFilterSettingsAsset(Settings, LevelAssetName);
		if (!FilterSettingsAsset)
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create filter settings DataAsset for level '%s'."), *LevelPackagePath);
			return;
		}

		LevelEntry->FilterSettings = FilterSettingsAsset;
	}

	EditorModuleLPTPrivate::MaterializeCollectionPresets(Settings, LevelAssetName, FilterSettingsAsset, *LevelEntry);

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

	ULevelPreloadDatabaseLPT::DeduplicateCollections(*LevelEntry);

	const bool bIsWorldPartition = SavedWorld->IsPartitionedWorld();
	const FLPTFilterSettings BaseRules = FilterSettingsAsset ? FilterSettingsAsset->ToFilterSettings() : FLPTFilterSettings();

	LevelEntry->LevelStateHash = EditorModuleLPTPrivate::ComputeLevelStateHash(SavedWorld, BaseRules);
	LevelEntry->GenerationTimestamp = FDateTime::UtcNow();

	if (bIsWorldPartition && !BaseRules.bAllowWorldPartitionAutoScan)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("World Partition auto scan is disabled in filter settings. Skipping collection auto-generation for '%s'."),
			*SavedWorld->GetOutermost()->GetName()
		);

		DatabaseAsset->MarkPackageDirty();
		DatabaseAsset->GetOutermost()->MarkPackageDirty();
		SaveDatabaseAsset(DatabaseAsset);
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = AssetRegistryModule.Get();

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
			const TArray<FSoftObjectPath> GeneratedAssetList = EditorModuleLPTPrivate::BuildFilteredAssetsForRules(
				SavedWorld,
				Registry,
				CollectionRules
			);

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
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save collection asset '%s'."), *CollectionAsset->GetPathName());
			}
		}
	}

	DatabaseAsset->MarkPackageDirty();
	DatabaseAsset->GetOutermost()->MarkPackageDirty();

	if (!SaveDatabaseAsset(DatabaseAsset))
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save LevelPreloadDatabaseLPT after updating '%s'."),
			*LevelPackagePath
		);
	}
}

void FLevelProgressTrackerEditorModule::OpenLevelRulesWindow(ULevelPreloadDatabaseLPT* DatabaseAsset, const TSoftObjectPtr<UWorld>& LevelSoftPtr, const FString& LevelDisplayName, bool bIsWorldPartition)
{
	SlateWidgetLPT::OpenLevelRulesWindow(
		DatabaseAsset,
		LevelSoftPtr,
		LevelDisplayName,
		bIsWorldPartition,
		[this](ULevelPreloadDatabaseLPT* InDatabaseAsset)
		{
			return SaveDatabaseAsset(InDatabaseAsset);
		}
	);
}

void FLevelProgressTrackerEditorModule::HandleOpenLevelRulesEditorRequested(ULevelProgressTrackerSettings* Settings)
{
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

	ULevelPreloadDatabaseLPT* DatabaseAsset = nullptr;
	if (EffectiveSettings->bAutoGenerateOnLevelSave)
	{
		DatabaseAsset = GetOrCreateDatabaseAsset(EffectiveSettings);
	}
	else
	{
		FString IgnoredDatabasePackagePath;
		FSoftObjectPath DatabaseObjectPath;
		if (!ULevelPreloadAssetFilter::ResolveDatabaseAssetPath(EffectiveSettings, IgnoredDatabasePackagePath, DatabaseObjectPath))
		{
			ShowWarningDialog(TEXT("Invalid LPT database path in project settings. With Auto Generate on Level Save disabled, database must already exist."));
			return;
		}

		DatabaseAsset = LoadObject<ULevelPreloadDatabaseLPT>(nullptr, *DatabaseObjectPath.ToString());
	}

	if (!DatabaseAsset)
	{
		ShowWarningDialog(TEXT("LevelPreloadDatabaseLPT asset was not found. Enable Auto Generate on Level Save or create database first."));
		return;
	}

	FLevelPreloadEntryLPT* Entry = nullptr;
	UAssetFilterSettingsLPT* FilterSettingsAsset = nullptr;

	if (!EffectiveSettings->bAutoGenerateOnLevelSave)
	{
		Entry = DatabaseAsset->FindEntryByLevel(LevelSoftPtr);
		if (!Entry)
		{
			ShowWarningDialog(FString::Printf(
				TEXT("No LPT entry exists for level '%s'. Auto Generate on Level Save is disabled, so no files were created."),
				*LevelPackagePath));
			return;
		}

		FilterSettingsAsset = Entry->FilterSettings.LoadSynchronous();
		if (!FilterSettingsAsset)
		{
			ShowWarningDialog(FString::Printf(
				TEXT("Filter settings asset is missing for level '%s'. Auto Generate on Level Save is disabled, so no files were created."),
				*LevelPackagePath));
			return;
		}

		OpenLevelRulesWindow(DatabaseAsset, LevelSoftPtr, LevelDisplayName, bIsWorldPartition);
		return;
	}

	bool bWasAdded = false;
	Entry = DatabaseAsset->FindOrAddEntryByLevel(LevelSoftPtr, bWasAdded);
	if (!Entry)
	{
		ShowWarningDialog(FString::Printf(TEXT("Failed to create level rules entry for '%s'."), *LevelPackagePath));
		return;
	}

	bool bDatabaseModified = false;
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

	if (bDatabaseModified)
	{
		DatabaseAsset->MarkPackageDirty();
		DatabaseAsset->GetOutermost()->MarkPackageDirty();
		SaveDatabaseAsset(DatabaseAsset);
	}

	OpenLevelRulesWindow(DatabaseAsset, LevelSoftPtr, LevelDisplayName, bIsWorldPartition);
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

