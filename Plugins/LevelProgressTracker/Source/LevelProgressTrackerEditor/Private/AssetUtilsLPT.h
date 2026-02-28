// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace AssetUtilsLPT
{
	bool IsEngineOrScriptPackage(const FString& LongPackageName);
	FString NormalizeFolderRuleForMerge(const FString& InFolderPath);
	bool IsExternalPackageOfWorldPartitionLevel(const FString& SavedPackageName, const UWorld* EditorWorld);
}
