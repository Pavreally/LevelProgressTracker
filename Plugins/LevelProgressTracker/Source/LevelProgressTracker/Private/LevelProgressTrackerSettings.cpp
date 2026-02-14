// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerSettings.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


namespace LevelProgressTrackerSettingsPrivate
{
	static const FString DefaultDatabaseFolder = TEXT("/Game/_DataLPT");
	static const FString DatabaseAssetName = TEXT("LevelPreloadDatabase");
}

#if WITH_EDITOR
ULevelProgressTrackerSettings::FOnOpenLevelRulesEditorRequested ULevelProgressTrackerSettings::OnOpenLevelRulesEditorRequested;
#endif

ULevelProgressTrackerSettings::ULevelProgressTrackerSettings()
{
	DatabaseFolder.Path = LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder;
	bAutoGenerateOnLevelSave = true;
}

FName ULevelProgressTrackerSettings::GetCategoryName() const
{
	return TEXT("Project");
}

bool ULevelProgressTrackerSettings::ResolveDatabaseAssetPaths(FString& OutDatabaseFolderLongPath, FString& OutDatabasePackagePath, FSoftObjectPath& OutDatabaseObjectPath) const
{
	FString FolderPath = DatabaseFolder.Path;
	FolderPath.TrimStartAndEndInline();
	FolderPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	if (FolderPath.IsEmpty())
	{
		FolderPath = LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder;
	}

	while (FolderPath.EndsWith(TEXT("/")))
	{
		FolderPath.LeftChopInline(1, EAllowShrinking::No);
	}

	if (FolderPath.IsEmpty())
	{
		FolderPath = LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder;
	}

	// Keep historical behavior: plain "/Game" meant "use the default subfolder".
	if (FolderPath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase) || FolderPath.Equals(TEXT("Game"), ESearchCase::IgnoreCase))
	{
		FolderPath = LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder;
	}

	// Backward compatibility: old settings could be saved as relative paths.
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

	OutDatabaseFolderLongPath = FolderPath;

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

void ULevelProgressTrackerSettings::BuildGlobalDefaultRules(FLPTLevelRules& OutRules) const
{
	OutRules.bUseExclusionMode = bUseExclusionMode;
	OutRules.AssetRules = AssetRules;
	OutRules.FolderRules = FolderRules;
	OutRules.bAllowWorldPartitionAutoScan = bAllowWorldPartitionAutoScan;
	OutRules.WorldPartitionRegions = WorldPartitionRegions;
	OutRules.WorldPartitionCells = WorldPartitionCells;
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
