// Pavel Gornostaev <https://github.com/Pavreally>

#include "SlateWidgetLPT.h"

#include "AssetFilterSettingsLPT.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "LevelPreloadDatabaseLPT.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace SlateWidgetLPT
{
	namespace
	{
		TWeakPtr<SWindow> ActiveRulesWindow;
	}

	bool TryActivateOpenLevelRulesWindow()
	{
		if (const TSharedPtr<SWindow> ExistingWindow = ActiveRulesWindow.Pin())
		{
			ExistingWindow->BringToFront();
			return true;
		}

		return false;
	}

	void OpenLevelRulesWindow(
		ULevelPreloadDatabaseLPT* DatabaseAsset,
		UAssetFilterSettingsLPT* FilterSettingsAsset,
		const FString& LevelDisplayName,
		const bool bIsWorldPartition,
		const bool bHasSublevels,
		const TFunction<bool(ULevelPreloadDatabaseLPT*, UAssetFilterSettingsLPT*)>& SaveAndGenerateFn,
		const TFunction<bool(UAssetFilterSettingsLPT*)>& AddAllDataLayersFn,
		const TFunction<bool(UAssetFilterSettingsLPT*)>& ConfigureSublevelsFn
	)
	{
		if (TryActivateOpenLevelRulesWindow())
		{
			return;
		}

		if (!FilterSettingsAsset)
		{
			return;
		}

		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

		const TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
		DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([bHasSublevels](const FPropertyAndParent& PropertyAndParent)
		{
			return bHasSublevels || PropertyAndParent.Property.GetFName() != GET_MEMBER_NAME_CHECKED(UAssetFilterSettingsLPT, bAutoCreateSublevelCollections);
		}));
		DetailsView->SetObject(FilterSettingsAsset);

		const TSharedRef<SWindow> RulesWindow = SNew(SWindow)
			.Title(FText::FromString(FString::Printf(TEXT("LPT Rules - %s"), *LevelDisplayName)))
			.ClientSize(FVector2D(800.f, 680.f))
			.SupportsMaximize(true)
			.SupportsMinimize(true);

		ActiveRulesWindow = RulesWindow;
		RulesWindow->SetOnWindowClosed(FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>& ClosedWindow)
		{
			const TSharedPtr<SWindow> OpenWindow = ActiveRulesWindow.Pin();
			if (OpenWindow.Get() == &ClosedWindow.Get())
			{
				ActiveRulesWindow.Reset();
			}
		}));

		RulesWindow->SetContent(
			SNew(SBorder)
			.Padding(12.f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Top)
					.Padding(0.f, 0.f, 12.f, 0.f)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 0.f, 0.f, 4.f)
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(TEXT("Configure the level filter settings and collection presets below. Save Level Data stores these settings and generates the preload resource references.")))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text_Lambda([FilterSettingsAsset, bIsWorldPartition]()
							{
								if (!bIsWorldPartition)
								{
									return FText::FromString(TEXT("Settings are ready. Save Level Data to generate the preload resource references."));
								}

								if (!FilterSettingsAsset->bAllowWorldPartitionAutoScan)
								{
									return FText::FromString(TEXT("Warning: World Partition auto scan is disabled. Save Level Data will store settings but skip auto-collected assets."));
								}

								for (const FLPTCollectionPresetLPT& Preset : FilterSettingsAsset->CollectionPresets)
								{
									for (const TSoftObjectPtr<UDataLayerAsset>& DataLayer : Preset.TargetDataLayers)
									{
										if (DataLayer.ToSoftObjectPath().IsValid())
										{
											return FText::FromString(TEXT("World Partition scan is scoped to the configured Data Layers."));
										}
									}

									if (!Preset.TargetDataLayerNames.IsEmpty() || !Preset.TargetCellRules.IsEmpty())
									{
										return FText::FromString(TEXT("World Partition scan is scoped by Data Layer names or cell rules."));
									}
								}

								if (FilterSettingsAsset->bAllowWorldPartitionUnscopedAutoScan)
								{
									return FText::FromString(TEXT("World Partition scan has no Data Layer scope; the full level may be scanned."));
								}

								return FText::FromString(TEXT("Warning: no Data Layer or Cell Rule is assigned to any collection. Add a Data Layer scope or enable Unscoped Auto Scan to generate World Partition assets."));
							})
						]

					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Top)
					.HAlign(HAlign_Right)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Save Level Data")))
							.ToolTipText(FText::FromString(TEXT("Save LPT settings and generate the preload resource references for this level.")))
							.OnClicked_Lambda([DatabaseAsset, FilterSettingsAsset, RulesWindow, SaveAndGenerateFn]()
							{
								if (!SaveAndGenerateFn || !SaveAndGenerateFn(DatabaseAsset, FilterSettingsAsset))
								{
									FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("LPT settings could not be saved or the preload resource references could not be generated. Check the Output Log for details.")));
									return FReply::Handled();
								}

								RulesWindow->RequestDestroyWindow();
								return FReply::Handled();
							})
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 6.f, 0.f, 0.f)
						.HAlign(HAlign_Right)
						[
							SNew(SButton)
							.Visibility_Lambda([FilterSettingsAsset, bIsWorldPartition]()
							{
								return bIsWorldPartition &&
									FilterSettingsAsset->bAllowWorldPartitionAutoScan &&
									FilterSettingsAsset->bAllowWorldPartitionUnscopedAutoScan
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.Text(FText::FromString(TEXT("Add All Data Layers")))
							.ToolTipText(FText::FromString(TEXT("Create one collection preset for each Data Layer in this World Partition level. Save Level Data to generate their resource references.")))
							.OnClicked_Lambda([FilterSettingsAsset, DetailsView, AddAllDataLayersFn]()
							{
								if (!AddAllDataLayersFn || !AddAllDataLayersFn(FilterSettingsAsset))
								{
									FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("No new World Partition Data Layers were added. Check the current level and existing collection presets.")));
									return FReply::Handled();
								}

								DetailsView->ForceRefresh();
								return FReply::Handled();
							})
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 6.f, 0.f, 0.f)
						.HAlign(HAlign_Right)
						[
							SNew(SButton)
							.Visibility(bHasSublevels ? EVisibility::Visible : EVisibility::Collapsed)
							.Text(FText::FromString(TEXT("Configure Sublevels")))
							.ToolTipText(FText::FromString(TEXT("Add one collection preset per streaming sublevel. You can disable generation for individual collections before saving.")))
							.OnClicked_Lambda([FilterSettingsAsset, DetailsView, ConfigureSublevelsFn]()
							{
								if (!ConfigureSublevelsFn || !ConfigureSublevelsFn(FilterSettingsAsset))
								{
									FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("No new sublevel presets were added. Check that the level is still open and that its streaming sublevels are not already configured.")));
									return FReply::Handled();
								}

								DetailsView->ForceRefresh();
								return FReply::Handled();
							})
						]
					]
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
					DetailsView
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Close")))
					.OnClicked_Lambda([RulesWindow]()
					{
						RulesWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		);

		FSlateApplication::Get().AddWindow(RulesWindow);
	}
}
