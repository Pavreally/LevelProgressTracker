// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

class ULevelPreloadDatabase;
class ULevelProgressTrackerSettings;

namespace DatabaseLPT
{
	ULevelPreloadDatabase* GetOrCreateDatabaseAsset(const ULevelProgressTrackerSettings* Settings);
	bool SaveDatabaseAsset(ULevelPreloadDatabase* DatabaseAsset);
}
