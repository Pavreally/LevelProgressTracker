// Pavel Gornostaev <https://github.com/Pavreally>

#include "EditorModuleLPT.h"

#include "AssetCollectorLPT.h"
#include "AssetFilterLPT.h"
#include "AssetUtilsLPT.h"
#include "DataLayerResolverLPT.h"
#include "DatabaseLPT.h"
#include "LevelPreloadAssetFilter.h"
#include "LevelPreloadDatabase.h"
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

namespace EditorModuleLPTPrivate
{
	const FName StyleSetName(TEXT("LevelProgressTrackerStyle"));
	const FName ToolbarIconName(TEXT("LevelProgressTracker.LPTRules"));

	bool IsGlobalDefaultsEnabled(const FLevelPreloadEntry& Entry)
	{
		// Rules flag is the source of truth; legacy mirror is synchronized separately.
		return Entry.Rules.bRulesInitializedFromGlobalDefaults;
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
		FText::FromString(TEXT("Open per-level rules for the currently opened level. Rules are stored per level in LevelPreloadDatabase.")),
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

	ULevelPreloadDatabase* DatabaseAsset = GetOrCreateDatabaseAsset(Settings);
	if (!DatabaseAsset)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create or load LevelPreloadDatabase asset."));
		return;
	}

	bool bWasEntryAdded = false;
	FLevelPreloadEntry* LevelEntry = DatabaseAsset->FindOrAddEntryByLevel(LevelSoftPtr, bWasEntryAdded);
	if (!LevelEntry)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to create or resolve database entry for '%s'."), *LevelPackagePath);
		return;
	}

	if (bWasEntryAdded)
	{
		LevelEntry->Rules = FLPTLevelRules();
		LevelEntry->Rules.bRulesInitializedFromGlobalDefaults = false;
		LevelEntry->bRulesInitializedFromGlobalDefaults = false;
	}

	const bool bUseGlobalDefaults = EditorModuleLPTPrivate::IsGlobalDefaultsEnabled(*LevelEntry);
	// Keep legacy and current flags synchronized in both directions of the toggle.
	LevelEntry->Rules.bRulesInitializedFromGlobalDefaults = bUseGlobalDefaults;
	LevelEntry->bRulesInitializedFromGlobalDefaults = bUseGlobalDefaults;

	const FLPTLevelRules EffectiveRules = bUseGlobalDefaults
		? AssetFilterLPT::BuildMergedRulesWithGlobalDominance(LevelEntry->Rules, Settings)
		: LevelEntry->Rules;

	const bool bIsWorldPartition = SavedWorld->IsPartitionedWorld();
	if (bIsWorldPartition && !EffectiveRules.bAllowWorldPartitionAutoScan)
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("World Partition auto scan is disabled for this level. Skipping database generation for '%s'."),
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
		AssetCollectorLPT::AppendHardDependencyClosureAssets(Registry, RootPackages, UniqueCandidateAssets, CandidateAssets, &EffectiveRules);
	}
	else
	{
		FLPTLevelRules WorldPartitionScanRules = EffectiveRules;
		DataLayerResolverLPT::ResolveWorldPartitionRegionRulesAsDataLayers(SavedWorld, WorldPartitionScanRules);

		TSet<FName> CandidateActorPackages;
		AssetCollectorLPT::CollectWorldPartitionActorPackages(SavedWorld, WorldPartitionScanRules, CandidateActorPackages);

		// World Partition scan uses ActorDesc metadata and AssetRegistry package dependencies only.
		// It is independent from currently loaded editor actors.
		const TArray<FName> RootPackages = CandidateActorPackages.Array();
		AssetCollectorLPT::AppendHardDependencyClosureAssets(Registry, RootPackages, UniqueCandidateAssets, CandidateAssets, &EffectiveRules);

		UE_LOG(LogLPTEditor, Log, TEXT("WP Candidates: %d"), CandidateAssets.Num());

		// Keep explicit asset rules discoverable in inclusion mode even when they were not reached by package traversal.
		AssetCollectorLPT::AppendExplicitAssetRuleCandidates(EffectiveRules, UniqueCandidateAssets, CandidateAssets);
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
		// In WP inclusion mode, folder rules can contribute additional candidates that are outside
		// actor dependency traversal (for explicit include workflows).
		AssetCollectorLPT::AppendFolderRuleCandidates(Registry, FinalFilterRules, UniqueCandidateAssets, CandidateAssets);
	}

	FLPTLevelRules PostExpansionFilterRules = FinalFilterRules;
	const bool bHasAssetOrFolderRules = FinalFilterRules.AssetRules.Num() > 0 || FinalFilterRules.FolderRules.Num() > 0;
	if (!FinalFilterRules.bUseExclusionMode && bHasAssetOrFolderRules)
	{
		// In inclusion mode, asset/folder rules define seed assets, then we expand hard dependencies
		// from those seeds while respecting class filters.
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

		// Seed rules are already applied. Final pass should keep expanded candidates.
		PostExpansionFilterRules.AssetRules.Empty();
		PostExpansionFilterRules.FolderRules.Empty();
	}

	// Always run final filtering pass so folder/asset include-exclude rules behave consistently.
	FilteredAssets = ULevelPreloadAssetFilter::FilterAssets(CandidateAssets, &PostExpansionFilterRules);

	DatabaseAsset->Modify();
	if (!DatabaseAsset->UpdateEntryAssetsByLevel(LevelSoftPtr, FilteredAssets))
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to update preload assets for level '%s'."), *LevelPackagePath);
		return;
	}

	DatabaseAsset->MarkPackageDirty();
	DatabaseAsset->GetOutermost()->MarkPackageDirty();

	if (!SaveDatabaseAsset(DatabaseAsset))
	{
		UE_LOG(LogLPTEditor, Warning, TEXT("Failed to save LevelPreloadDatabase after updating '%s'."),
			*LevelPackagePath
		);
	}
}

bool FLevelProgressTrackerEditorModule::PromptCreateLevelRules(bool& bApplyGlobalDefaults) const
{
	return SlateWidgetLPT::PromptCreateLevelRules(bApplyGlobalDefaults);
}

void FLevelProgressTrackerEditorModule::OpenLevelRulesWindow(ULevelPreloadDatabase* DatabaseAsset, const TSoftObjectPtr<UWorld>& LevelSoftPtr, const FString& LevelDisplayName, bool bIsWorldPartition)
{
	SlateWidgetLPT::OpenLevelRulesWindow(
		DatabaseAsset,
		LevelSoftPtr,
		LevelDisplayName,
		bIsWorldPartition,
		[this](ULevelPreloadDatabase* InDatabaseAsset)
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

	ULevelPreloadDatabase* DatabaseAsset = GetOrCreateDatabaseAsset(EffectiveSettings);
	if (!DatabaseAsset)
	{
		ShowWarningDialog(TEXT("Failed to create or load LevelPreloadDatabase asset."));
		return;
	}

	FLevelPreloadEntry* Entry = DatabaseAsset->FindEntryByLevel(LevelSoftPtr);
	if (!Entry)
	{
		bool bWasAdded = false;
		Entry = DatabaseAsset->FindOrAddEntryByLevel(LevelSoftPtr, bWasAdded);
		if (!Entry)
		{
			ShowWarningDialog(FString::Printf(TEXT("Failed to create level rules entry for '%s'."), *LevelPackagePath));
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
	return DatabaseLPT::GetOrCreateDatabaseAsset(Settings);
}

bool FLevelProgressTrackerEditorModule::SaveDatabaseAsset(ULevelPreloadDatabase* DatabaseAsset) const
{
	return DatabaseLPT::SaveDatabaseAsset(DatabaseAsset);
}
#endif

IMPLEMENT_MODULE(FLevelProgressTrackerEditorModule, LevelProgressTrackerEditor)



