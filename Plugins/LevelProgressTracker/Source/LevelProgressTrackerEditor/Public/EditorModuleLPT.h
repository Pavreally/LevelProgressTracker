// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class UPackage;
class UWorld;
class ULevelPreloadDatabaseLPT;
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
	void RebuildLevelDependencies(UWorld* SavedWorld);
	void RegisterMenus();
	void HandleToolbarOpenLevelRulesClicked();
	void HandleOpenLevelRulesEditorRequested(ULevelProgressTrackerSettings* Settings);
	bool TryGetCurrentEditorLevel(TSoftObjectPtr<UWorld>& OutLevelSoftPtr, FString& OutLevelPackagePath, FString& OutLevelDisplayName, bool& bIsWorldPartition) const;
	void OpenLevelRulesWindow(ULevelPreloadDatabaseLPT* DatabaseAsset, const TSoftObjectPtr<UWorld>& LevelSoftPtr, const FString& LevelDisplayName, bool bIsWorldPartition);
	ULevelPreloadDatabaseLPT* GetOrCreateDatabaseAsset(const ULevelProgressTrackerSettings* Settings) const;
	bool SaveDatabaseAsset(ULevelPreloadDatabaseLPT* DatabaseAsset) const;

	TSharedPtr<FSlateStyleSet> StyleSet;
#endif
};
