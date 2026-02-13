// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelPreloadDatabase.h"


const FLevelPreloadEntry* ULevelPreloadDatabase::FindEntry(const TSoftObjectPtr<UWorld>& Level) const
{
	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();

	for (const FLevelPreloadEntry& Entry : Levels)
	{
		if (Entry.Level.ToSoftObjectPath() == LevelPath)
		{
			return &Entry;
		}
	}

	return nullptr;
}

void ULevelPreloadDatabase::UpdateEntry(const TSoftObjectPtr<UWorld>& Level, const TArray<FSoftObjectPath>& AssetPaths)
{
	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();

	if (!LevelPath.IsValid())
	{
		return;
	}

	int32 EntryIndex = INDEX_NONE;

	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		if (Levels[Index].Level.ToSoftObjectPath() == LevelPath)
		{
			EntryIndex = Index;
			break;
		}
	}

	if (EntryIndex == INDEX_NONE)
	{
		EntryIndex = Levels.AddDefaulted();
	}

	Levels[EntryIndex].Level = Level;
	Levels[EntryIndex].GenerationTimestamp = FDateTime::UtcNow();

	TSet<FSoftObjectPath> UniquePaths;
	Levels[EntryIndex].Assets.Reset(AssetPaths.Num());

	for (const FSoftObjectPath& AssetPath : AssetPaths)
	{
		if (!AssetPath.IsValid() || UniquePaths.Contains(AssetPath))
		{
			continue;
		}

		UniquePaths.Add(AssetPath);
		Levels[EntryIndex].Assets.Add(TSoftObjectPtr<UObject>(AssetPath));
	}
}
