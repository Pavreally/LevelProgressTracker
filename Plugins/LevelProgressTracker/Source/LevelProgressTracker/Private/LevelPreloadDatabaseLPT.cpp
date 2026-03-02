// Pavel Gornostaev <https://github.com/Pavreally>

#include "LevelPreloadDatabaseLPT.h"

const FLevelPreloadEntryLPT* ULevelPreloadDatabaseLPT::FindEntryByLevel(const TSoftObjectPtr<UWorld>& Level) const
{
	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();
	if (!LevelPath.IsValid())
	{
		return nullptr;
	}

	for (const FLevelPreloadEntryLPT& Entry : Levels)
	{
		if (Entry.Level.ToSoftObjectPath() == LevelPath)
		{
			return &Entry;
		}
	}

	return nullptr;
}

FLevelPreloadEntryLPT* ULevelPreloadDatabaseLPT::FindEntryByLevel(const TSoftObjectPtr<UWorld>& Level)
{
	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();
	if (!LevelPath.IsValid())
	{
		return nullptr;
	}

	for (FLevelPreloadEntryLPT& Entry : Levels)
	{
		if (Entry.Level.ToSoftObjectPath() == LevelPath)
		{
			return &Entry;
		}
	}

	return nullptr;
}

FLevelPreloadEntryLPT* ULevelPreloadDatabaseLPT::FindOrAddEntryByLevel(const TSoftObjectPtr<UWorld>& Level, bool& bWasAdded)
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

		DeduplicateCollections(Levels[PrimaryIndex]);
		return &Levels[PrimaryIndex];
	}

	const int32 NewEntryIndex = Levels.AddDefaulted();
	FLevelPreloadEntryLPT& NewEntry = Levels[NewEntryIndex];
	NewEntry.Level = Level;
	DeduplicateCollections(NewEntry);
	bWasAdded = true;

	return &NewEntry;
}

void ULevelPreloadDatabaseLPT::DeduplicateCollections(FLevelPreloadEntryLPT& Entry)
{
	TSet<FSoftObjectPath> UniqueCollectionPaths;
	TArray<TSoftObjectPtr<UAssetCollectionDataLPT>> DeduplicatedCollections;
	DeduplicatedCollections.Reserve(Entry.Collections.Num());

	for (const TSoftObjectPtr<UAssetCollectionDataLPT>& CollectionRef : Entry.Collections)
	{
		const FSoftObjectPath CollectionPath = CollectionRef.ToSoftObjectPath();
		if (!CollectionPath.IsValid() || UniqueCollectionPaths.Contains(CollectionPath))
		{
			continue;
		}

		UniqueCollectionPaths.Add(CollectionPath);
		DeduplicatedCollections.Add(CollectionRef);
	}

	Entry.Collections = MoveTemp(DeduplicatedCollections);
}
