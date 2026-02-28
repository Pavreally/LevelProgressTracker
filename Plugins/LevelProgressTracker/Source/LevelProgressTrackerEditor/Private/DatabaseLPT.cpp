// Pavel Gornostaev <https://github.com/Pavreally>

#include "DatabaseLPT.h"

#include "LevelPreloadAssetFilter.h"
#include "LevelPreloadDatabase.h"
#include "LogLPTEditor.h"
#include "SettingsLPT.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace DatabaseLPT
{
	ULevelPreloadDatabase* GetOrCreateDatabaseAsset(const ULevelProgressTrackerSettings* Settings)
	{
		FString DatabasePackagePath;
		FSoftObjectPath DatabaseObjectPath;

		if (!ULevelPreloadAssetFilter::ResolveDatabaseAssetPath(Settings, DatabasePackagePath, DatabaseObjectPath))
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("Invalid database folder in project settings. Expected a valid long package path (for example '/Game/_DataLPT' or '/PluginName/Data')."));
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

	bool SaveDatabaseAsset(ULevelPreloadDatabase* DatabaseAsset)
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
}


