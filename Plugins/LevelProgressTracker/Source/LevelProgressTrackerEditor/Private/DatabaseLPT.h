// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

class ULevelPreloadDatabaseLPT;
class ULevelProgressTrackerSettings;

namespace DatabaseLPT
{
	ULevelPreloadDatabaseLPT* GetOrCreateDatabaseAsset(const ULevelProgressTrackerSettings* Settings);
	bool SaveDatabaseAsset(ULevelPreloadDatabaseLPT* DatabaseAsset);
}
