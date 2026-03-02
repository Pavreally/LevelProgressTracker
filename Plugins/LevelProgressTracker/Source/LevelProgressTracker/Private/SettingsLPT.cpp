// Pavel Gornostaev <https://github.com/Pavreally>

#include "SettingsLPT.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


namespace LevelProgressTrackerSettingsPrivate
{
	static const FString DefaultDatabaseFolder = TEXT("/Game/_DataLPT");
	static const FString DefaultCollectionSubfolder = TEXT("AssetList");
	static const FString DefaultFilterSettingsSubfolder = TEXT("AssetFilterSettings");
	static const FString DatabaseAssetName = TEXT("LevelPreloadDatabaseLPT");

	static FString NormalizeContentFolderPath(FString FolderPath, const FString& DefaultFolderPath)
	{
		FolderPath.TrimStartAndEndInline();
		FolderPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (FolderPath.IsEmpty())
		{
			FolderPath = DefaultFolderPath;
		}

		while (FolderPath.EndsWith(TEXT("/")))
		{
			FolderPath.LeftChopInline(1, EAllowShrinking::No);
		}

		if (FolderPath.IsEmpty())
		{
			FolderPath = DefaultFolderPath;
		}

		// Keep historical behavior: plain "/Game" meant "use the default subfolder".
		if (FolderPath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase) || FolderPath.Equals(TEXT("Game"), ESearchCase::IgnoreCase))
		{
			FolderPath = DefaultFolderPath;
		}

		if (!FolderPath.StartsWith(TEXT("/")))
		{
			if (FolderPath.Equals(TEXT("Game"), ESearchCase::IgnoreCase))
			{
				FolderPath = TEXT("/Game");
			}
			else if (FolderPath.StartsWith(TEXT("Game/"), ESearchCase::IgnoreCase))
			{
				FolderPath = FString::Printf(TEXT("/%s"), *FolderPath);
			}
			else
			{
				FolderPath = FString::Printf(TEXT("/Game/%s"), *FolderPath);
			}
		}

		return FolderPath;
	}
}

#if WITH_EDITOR
ULevelProgressTrackerSettings::FOnOpenLevelRulesEditorRequested ULevelProgressTrackerSettings::OnOpenLevelRulesEditorRequested;
#endif

ULevelProgressTrackerSettings::ULevelProgressTrackerSettings()
{
	DatabaseFolder.Path = LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder;
	AssetCollectionFolder.Path = FString::Printf(
		TEXT("%s/%s"),
		*LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder,
		*LevelProgressTrackerSettingsPrivate::DefaultCollectionSubfolder);
	AssetFilterSettingsFolder.Path = FString::Printf(
		TEXT("%s/%s"),
		*LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder,
		*LevelProgressTrackerSettingsPrivate::DefaultFilterSettingsSubfolder);
	bAutoGenerateOnLevelSave = true;
}

FName ULevelProgressTrackerSettings::GetCategoryName() const
{
	return TEXT("Project");
}

bool ULevelProgressTrackerSettings::ResolveDatabaseAssetPaths(FString& OutDatabaseFolderLongPath, FString& OutDatabasePackagePath, FSoftObjectPath& OutDatabaseObjectPath) const
{
	OutDatabaseFolderLongPath = LevelProgressTrackerSettingsPrivate::NormalizeContentFolderPath(
		DatabaseFolder.Path,
		LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder);

	if (!FPackageName::IsValidLongPackageName(OutDatabaseFolderLongPath))
	{
		return false;
	}

	OutDatabasePackagePath = FString::Printf(TEXT("%s/%s"), *OutDatabaseFolderLongPath, *LevelProgressTrackerSettingsPrivate::DatabaseAssetName);

	if (!FPackageName::IsValidLongPackageName(OutDatabasePackagePath))
	{
		return false;
	}

	OutDatabaseObjectPath = FSoftObjectPath(
		FString::Printf(TEXT("%s.%s"), *OutDatabasePackagePath, *LevelProgressTrackerSettingsPrivate::DatabaseAssetName)
	);

	return OutDatabaseObjectPath.IsValid();
}

bool ULevelProgressTrackerSettings::ResolveAssetCollectionFolderPath(FString& OutCollectionFolderLongPath) const
{
	FString ResolvedDatabaseFolder;
	FString IgnoredDatabasePackage;
	FSoftObjectPath IgnoredDatabaseObjectPath;
	if (!ResolveDatabaseAssetPaths(ResolvedDatabaseFolder, IgnoredDatabasePackage, IgnoredDatabaseObjectPath))
	{
		return false;
	}

	const FString DefaultCollectionFolder = FString::Printf(
		TEXT("%s/%s"),
		*ResolvedDatabaseFolder,
		*LevelProgressTrackerSettingsPrivate::DefaultCollectionSubfolder);

	OutCollectionFolderLongPath = LevelProgressTrackerSettingsPrivate::NormalizeContentFolderPath(
		AssetCollectionFolder.Path,
		DefaultCollectionFolder);

	return FPackageName::IsValidLongPackageName(OutCollectionFolderLongPath);
}

bool ULevelProgressTrackerSettings::ResolveFilterSettingsFolderPath(FString& OutFilterSettingsFolderLongPath) const
{
	FString ResolvedDatabaseFolder;
	FString IgnoredDatabasePackage;
	FSoftObjectPath IgnoredDatabaseObjectPath;
	if (!ResolveDatabaseAssetPaths(ResolvedDatabaseFolder, IgnoredDatabasePackage, IgnoredDatabaseObjectPath))
	{
		return false;
	}

	const FString DefaultFilterSettingsFolder = FString::Printf(
		TEXT("%s/%s"),
		*ResolvedDatabaseFolder,
		*LevelProgressTrackerSettingsPrivate::DefaultFilterSettingsSubfolder);

	OutFilterSettingsFolderLongPath = LevelProgressTrackerSettingsPrivate::NormalizeContentFolderPath(
		AssetFilterSettingsFolder.Path,
		DefaultFilterSettingsFolder);

	return FPackageName::IsValidLongPackageName(OutFilterSettingsFolderLongPath);
}

void ULevelProgressTrackerSettings::BuildGlobalDefaultRules(FLPTFilterSettings& OutRules) const
{
	OutRules = FLPTFilterSettings();
	OutRules.bUseChunkedPreload = bUseChunkedPreload;
	OutRules.PreloadChunkSize = FMath::Max(1, PreloadChunkSize);
	OutRules.AssetClassFilter = AssetClassFilter;
}

void ULevelProgressTrackerSettings::OpenLevelRulesEditorForCurrentLevel()
{
#if WITH_EDITOR
	if (!OnOpenLevelRulesEditorRequested.IsBound())
	{
		FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("LevelProgressTrackerEditor"));
	}

	if (!OnOpenLevelRulesEditorRequested.IsBound())
	{
		UE_LOG(LogTemp, Warning, TEXT("LPT Editor: Failed to open level rules editor because LevelProgressTrackerEditor module is not available."));
		return;
	}

	OnOpenLevelRulesEditorRequested.Broadcast(this);
#endif
}


