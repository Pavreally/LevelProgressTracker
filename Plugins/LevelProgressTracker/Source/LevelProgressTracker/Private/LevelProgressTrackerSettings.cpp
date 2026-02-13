// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelProgressTrackerSettings.h"
#include "Misc/PackageName.h"


namespace LevelProgressTrackerSettingsPrivate
{
	static const FString DefaultDatabaseFolder = TEXT("_DataLPT");
	static const FString DatabaseAssetName = TEXT("LevelPreloadDatabase");
}

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

	if (FolderPath.StartsWith(TEXT("/Game/")))
	{
		FolderPath.RightChopInline(6, EAllowShrinking::No);
	}
	else if (FolderPath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase) || FolderPath.Equals(TEXT("Game"), ESearchCase::IgnoreCase))
	{
		FolderPath = LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder;
	}
	else if (FolderPath.StartsWith(TEXT("Game/")))
	{
		FolderPath.RightChopInline(5, EAllowShrinking::No);
	}
	else if (FolderPath.StartsWith(TEXT("/")))
	{
		FolderPath.RightChopInline(1, EAllowShrinking::No);
	}

	while (FolderPath.EndsWith(TEXT("/")))
	{
		FolderPath.LeftChopInline(1, EAllowShrinking::No);
	}

	if (FolderPath.IsEmpty())
	{
		FolderPath = LevelProgressTrackerSettingsPrivate::DefaultDatabaseFolder;
	}

	OutDatabaseFolderLongPath = FString::Printf(TEXT("/Game/%s"), *FolderPath);

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

const FLPTLevelRules* ULevelProgressTrackerSettings::FindLevelRules(const TSoftObjectPtr<UWorld>& Level) const
{
	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();

	if (!LevelPath.IsValid())
	{
		return nullptr;
	}

	for (const FLPTLevelRules& Rules : LevelRules)
	{
		if (Rules.TargetLevel.ToSoftObjectPath() == LevelPath)
		{
			return &Rules;
		}
	}

	return nullptr;
}

FLPTLevelRules* ULevelProgressTrackerSettings::FindOrAddLevelRules(const TSoftObjectPtr<UWorld>& Level, bool& bWasAdded)
{
	bWasAdded = false;

	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();
	if (!LevelPath.IsValid())
	{
		return nullptr;
	}

	for (FLPTLevelRules& Rules : LevelRules)
	{
		if (Rules.TargetLevel.ToSoftObjectPath() == LevelPath)
		{
			return &Rules;
		}
	}

	const int32 NewIndex = LevelRules.AddDefaulted();
	FLPTLevelRules& NewRules = LevelRules[NewIndex];
	NewRules.TargetLevel = Level;
	bWasAdded = true;

	return &NewRules;
}
