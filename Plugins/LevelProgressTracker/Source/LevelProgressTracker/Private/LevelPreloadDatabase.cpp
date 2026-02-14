// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelPreloadDatabase.h"

namespace
{
	static void SyncLegacyGlobalDefaultsFlag(FLevelPreloadEntry& Entry)
	{
		Entry.bRulesInitializedFromGlobalDefaults = Entry.Rules.bRulesInitializedFromGlobalDefaults;
	}
}

const FLevelPreloadEntry* ULevelPreloadDatabase::FindEntryByLevel(const TSoftObjectPtr<UWorld>& Level) const
{
	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();
	if (!LevelPath.IsValid())
	{
		return nullptr;
	}

	for (const FLevelPreloadEntry& Entry : Levels)
	{
		if (Entry.Level.ToSoftObjectPath() == LevelPath)
		{
			return &Entry;
		}
	}

	return nullptr;
}

FLevelPreloadEntry* ULevelPreloadDatabase::FindEntryByLevel(const TSoftObjectPtr<UWorld>& Level)
{
	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();
	if (!LevelPath.IsValid())
	{
		return nullptr;
	}

	for (FLevelPreloadEntry& Entry : Levels)
	{
		if (Entry.Level.ToSoftObjectPath() == LevelPath)
		{
			SyncLegacyGlobalDefaultsFlag(Entry);
			return &Entry;
		}
	}

	return nullptr;
}

FLevelPreloadEntry* ULevelPreloadDatabase::FindOrAddEntryByLevel(const TSoftObjectPtr<UWorld>& Level, bool& bWasAdded)
{
	bWasAdded = false;

	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();
	if (!LevelPath.IsValid())
	{
		return nullptr;
	}

	int32 PrimaryIndex = INDEX_NONE;
	TArray<int32> DuplicateIndices;

	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		if (Levels[Index].Level.ToSoftObjectPath() != LevelPath)
		{
			continue;
		}

		if (PrimaryIndex == INDEX_NONE)
		{
			PrimaryIndex = Index;
		}
		else
		{
			DuplicateIndices.Add(Index);
		}
	}

	if (PrimaryIndex != INDEX_NONE)
	{
		for (int32 DupArrayIndex = DuplicateIndices.Num() - 1; DupArrayIndex >= 0; --DupArrayIndex)
		{
			Levels.RemoveAt(DuplicateIndices[DupArrayIndex]);
		}

		SyncLegacyGlobalDefaultsFlag(Levels[PrimaryIndex]);
		return &Levels[PrimaryIndex];
	}

	const int32 NewEntryIndex = Levels.AddDefaulted();
	FLevelPreloadEntry& NewEntry = Levels[NewEntryIndex];
	NewEntry.Level = Level;
	SyncLegacyGlobalDefaultsFlag(NewEntry);
	bWasAdded = true;

	return &NewEntry;
}

bool ULevelPreloadDatabase::UpdateEntryAssetsByLevel(const TSoftObjectPtr<UWorld>& Level, const TArray<FSoftObjectPath>& AssetPaths)
{
	bool bWasAdded = false;
	FLevelPreloadEntry* Entry = FindOrAddEntryByLevel(Level, bWasAdded);
	if (!Entry)
	{
		return false;
	}

	Entry->Level = Level;
	Entry->GenerationTimestamp = FDateTime::UtcNow();

	TSet<FSoftObjectPath> UniquePaths;
	Entry->Assets.Reset(AssetPaths.Num());

	for (const FSoftObjectPath& AssetPath : AssetPaths)
	{
		if (!AssetPath.IsValid() || UniquePaths.Contains(AssetPath))
		{
			continue;
		}

		UniquePaths.Add(AssetPath);
		Entry->Assets.Add(AssetPath);
	}

	SyncLegacyGlobalDefaultsFlag(*Entry);

	return true;
}
