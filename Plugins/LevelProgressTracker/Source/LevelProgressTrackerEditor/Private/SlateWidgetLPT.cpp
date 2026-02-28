// Pavel Gornostaev <https://github.com/Pavreally>

#include "SlateWidgetLPT.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IStructureDetailsView.h"
#include "LevelPreloadDatabase.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace SlateWidgetLPT
{
	namespace
	{
		bool IsGlobalDefaultsEnabled(const FLevelPreloadEntry& Entry)
		{
			// Rules flag is the source of truth; legacy mirror is synchronized separately.
			return Entry.Rules.bRulesInitializedFromGlobalDefaults;
		}
	}

	bool PromptCreateLevelRules(bool& bApplyGlobalDefaults)
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

	void OpenLevelRulesWindow(
		ULevelPreloadDatabase* DatabaseAsset,
		const TSoftObjectPtr<UWorld>& LevelSoftPtr,
		const FString& LevelDisplayName,
		bool bIsWorldPartition,
		const TFunction<bool(ULevelPreloadDatabase*)>& SaveDatabaseAssetFn
	)
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
			WorkingRules->bRulesInitializedFromGlobalDefaults = IsGlobalDefaultsEnabled(*ExistingEntry);
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
						.OnClicked_Lambda([DatabaseAsset, LevelSoftPtr, RulesStructOnScope, RulesWindow, SaveDatabaseAssetFn]()
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

							if (SaveDatabaseAssetFn)
							{
								SaveDatabaseAssetFn(DatabaseAsset);
							}

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
}

