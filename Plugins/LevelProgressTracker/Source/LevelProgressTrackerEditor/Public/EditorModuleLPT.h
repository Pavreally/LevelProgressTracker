// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class UPackage;
class UWorld;
class ULevelPreloadDatabaseLPT;
class UAssetFilterSettingsLPT;
class ULevelProgressTrackerSettings;
class FObjectPostSaveContext;
class FSlateStyleSet;

class FLevelProgressTrackerEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

#if WITH_EDITOR
private:
	void RegisterStyle();
	void UnregisterStyle();
	void OnPackageSaved(const FString& PackageFilename, UPackage* SavedPackage, FObjectPostSaveContext SaveContext);
	bool RebuildLevelDependencies(UWorld* SavedWorld, bool bForceGeneration = false);
	void RegisterMenus();
	void HandleToolbarOpenLevelRulesClicked();
	void HandleOpenLevelRulesEditorRequested(ULevelProgressTrackerSettings* Settings);
	bool TryGetCurrentEditorLevel(TSoftObjectPtr<UWorld>& OutLevelSoftPtr, FString& OutLevelPackagePath, FString& OutLevelDisplayName, bool& bIsWorldPartition) const;
	void OpenLevelRulesWindow(ULevelPreloadDatabaseLPT* DatabaseAsset, UAssetFilterSettingsLPT* FilterSettingsAsset, const TSoftObjectPtr<UWorld>& LevelSoftPtr, const FString& LevelDisplayName, bool bIsWorldPartition, bool bHasSublevels);
	ULevelPreloadDatabaseLPT* GetOrCreateDatabaseAsset(const ULevelProgressTrackerSettings* Settings) const;
	bool SaveDatabaseAsset(ULevelPreloadDatabaseLPT* DatabaseAsset) const;
	bool AddAllWorldPartitionDataLayersToPresets(UWorld* EditorWorld, UAssetFilterSettingsLPT* FilterSettingsAsset) const;

	bool bIsGenerating = false;
	TSharedPtr<FSlateStyleSet> StyleSet;
#endif
};
