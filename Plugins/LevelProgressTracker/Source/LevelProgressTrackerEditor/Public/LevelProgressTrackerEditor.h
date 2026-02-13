// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class UPackage;
class UWorld;
class ULevelPreloadDatabase;
class ULevelProgressTrackerSettings;
class FObjectPostSaveContext;

class FLevelProgressTrackerEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

#if WITH_EDITOR
private:
	void OnPackageSaved(const FString& PackageFilename, UPackage* SavedPackage, FObjectPostSaveContext SaveContext);
	void RebuildLevelDependencies(UWorld* SavedWorld);
	ULevelPreloadDatabase* GetOrCreateDatabaseAsset(const ULevelProgressTrackerSettings* Settings) const;
	bool SaveDatabaseAsset(ULevelPreloadDatabase* DatabaseAsset) const;
#endif
};
