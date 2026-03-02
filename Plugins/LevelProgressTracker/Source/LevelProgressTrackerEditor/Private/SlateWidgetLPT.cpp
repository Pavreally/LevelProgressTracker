// Pavel Gornostaev <https://github.com/Pavreally>

#include "SlateWidgetLPT.h"

#include "AssetFilterSettingsLPT.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "LevelPreloadDatabaseLPT.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace SlateWidgetLPT
{
	namespace
	{
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
	}

	void OpenLevelRulesWindow(
		ULevelPreloadDatabaseLPT* DatabaseAsset,
		const TSoftObjectPtr<UWorld>& LevelSoftPtr,
		const FString& LevelDisplayName,
		bool bIsWorldPartition,
		const TFunction<bool(ULevelPreloadDatabaseLPT*)>& SaveDatabaseAssetFn
	)
	{
		if (!DatabaseAsset)
		{
			return;
		}

		const FLevelPreloadEntryLPT* ExistingEntry = DatabaseAsset->FindEntryByLevel(LevelSoftPtr);
		if (!ExistingEntry)
		{
			return;
		}

		UAssetFilterSettingsLPT* FilterSettingsAsset = ExistingEntry->FilterSettings.LoadSynchronous();
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
		DetailsView->SetObject(FilterSettingsAsset);

		const TSharedRef<SWindow> RulesWindow = SNew(SWindow)
			.Title(FText::FromString(FString::Printf(TEXT("LPT Rules - %s"), *LevelDisplayName)))
			.ClientSize(FVector2D(800.f, 680.f))
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
					.Text(FText::FromString(TEXT("Filter settings are stored in a separate DataAsset and used for collection generation.")))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Collection Presets define preconfigured AssetCollectionDataLPT templates for this level.")))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.Visibility_Lambda([FilterSettingsAsset, bIsWorldPartition]()
					{
						if (!bIsWorldPartition || !FilterSettingsAsset)
						{
							return EVisibility::Collapsed;
						}

						return FilterSettingsAsset->bAllowWorldPartitionAutoScan ? EVisibility::Collapsed : EVisibility::Visible;
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
					DetailsView
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
						.OnClicked_Lambda([DatabaseAsset, FilterSettingsAsset, RulesWindow, SaveDatabaseAssetFn]()
						{
							if (FilterSettingsAsset)
							{
								FilterSettingsAsset->Modify();
								FilterSettingsAsset->MarkPackageDirty();
								FilterSettingsAsset->GetOutermost()->MarkPackageDirty();
								SaveAssetObject(FilterSettingsAsset);
							}

							if (DatabaseAsset)
							{
								DatabaseAsset->Modify();
								DatabaseAsset->MarkPackageDirty();
								DatabaseAsset->GetOutermost()->MarkPackageDirty();

								if (SaveDatabaseAssetFn)
								{
									SaveDatabaseAssetFn(DatabaseAsset);
								}
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
