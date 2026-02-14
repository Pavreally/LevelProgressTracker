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
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "IStructureDetailsView.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
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
		TArray<FSoftObjectPath>& OutAssets
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

	static void AppendDirectDependenciesAssets(
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
			UE::AssetRegistry::FDependencyQuery()
		);

		for (const FName DependencyPackageName : Dependencies)
		{
			AppendAssetsFromPackage(Registry, DependencyPackageName, UniquePaths, OutAssets);
		}
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

	UWorld* SavedWorld = UWorld::FindWorldInPackage(SavedPackage);
	if (!SavedWorld)
	{
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

	OutLevelPackagePath = WorldPackage->GetName();
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
		Settings->BuildGlobalDefaultRules(LevelEntry->Rules);
		LevelEntry->bRulesInitializedFromGlobalDefaults = true;
	}

	const bool bIsWorldPartition = SavedWorld->IsPartitionedWorld();
	if (bIsWorldPartition && !LevelEntry->Rules.bAllowWorldPartitionAutoScan)
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
		LevelProgressTrackerEditorPrivate::AppendDirectDependenciesAssets(Registry, LevelPackageName, UniqueCandidateAssets, CandidateAssets);
	}
	else
	{
		TSet<FName> CandidateActorPackages;

		for (ULevel* LoadedLevel : SavedWorld->GetLevels())
		{
			if (!LoadedLevel)
			{
				continue;
			}

			for (AActor* Actor : LoadedLevel->Actors)
			{
				if (!IsValid(Actor))
				{
					continue;
				}

				UPackage* ActorPackage = Actor->GetPackage();
				if (!ActorPackage)
				{
					continue;
				}

				const FString ActorPackagePath = ActorPackage->GetName();
				const FString ActorAssetName = FPackageName::GetLongPackageAssetName(ActorPackagePath);
				const FString ActorObjectName = !ActorAssetName.IsEmpty() ? ActorAssetName : Actor->GetName();
				const FSoftObjectPath ActorObjectPath(FString::Printf(TEXT("%s.%s"), *ActorPackagePath, *ActorObjectName));

				const TArray<FName> ActorRegions = Actor->GetDataLayerInstanceNames();
				if (!ULevelPreloadAssetFilter::ShouldIncludeWorldPartitionActor(ActorObjectPath, ActorRegions, &LevelEntry->Rules))
				{
					continue;
				}

				CandidateActorPackages.Add(FName(*ActorPackagePath));
			}
		}

		// World Partition scan uses only packages of actors currently loaded in memory.
		// No forced loading of hidden cells or full-world traversal is performed.
		for (const FName ActorPackageName : CandidateActorPackages)
		{
			LevelProgressTrackerEditorPrivate::AppendAssetsFromPackage(Registry, ActorPackageName, UniqueCandidateAssets, CandidateAssets);
			LevelProgressTrackerEditorPrivate::AppendDirectDependenciesAssets(Registry, ActorPackageName, UniqueCandidateAssets, CandidateAssets);
		}
	}

	const TArray<FSoftObjectPath> FilteredAssets = ULevelPreloadAssetFilter::FilterAssets(CandidateAssets, &LevelEntry->Rules);

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
	bApplyGlobalDefaults = true;

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
				.IsChecked(ECheckBoxState::Checked)
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
		bool bApplyGlobalDefaults = true;
		if (!PromptCreateLevelRules(bApplyGlobalDefaults))
		{
			return;
		}

		bool bWasAdded = false;
		Entry = DatabaseAsset->FindOrAddEntryByLevel(LevelSoftPtr, bWasAdded);
		if (!Entry)
		{
			ShowWarningDialog(FString::Printf(TEXT("LPT Editor: Failed to create level rules entry for '%s'."), *LevelPackagePath));
			return;
		}

		if (bApplyGlobalDefaults)
		{
			EffectiveSettings->BuildGlobalDefaultRules(Entry->Rules);
			Entry->bRulesInitializedFromGlobalDefaults = true;
		}
		else
		{
			Entry->Rules = FLPTLevelRules();
			Entry->bRulesInitializedFromGlobalDefaults = false;
		}

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
