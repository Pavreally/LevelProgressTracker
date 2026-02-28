// Pavel Gornostaev <https://github.com/Pavreally>

#include "AssetUtilsLPT.h"

#include "Engine/World.h"
#include "UObject/Package.h"

namespace AssetUtilsLPT
{
	bool IsEngineOrScriptPackage(const FString& LongPackageName)
	{
		return LongPackageName.StartsWith(TEXT("/Engine/")) || LongPackageName.StartsWith(TEXT("/Script/"));
	}

	FString NormalizeFolderRuleForMerge(const FString& InFolderPath)
	{
		FString FolderPath = InFolderPath;
		FolderPath.TrimStartAndEndInline();
		FolderPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (FolderPath.IsEmpty())
		{
			return FString();
		}

		while (FolderPath.EndsWith(TEXT("/")))
		{
			FolderPath.LeftChopInline(1, EAllowShrinking::No);
		}

		if (FolderPath.IsEmpty())
		{
			return FString();
		}

		if (FolderPath.StartsWith(TEXT("/")))
		{
			return FolderPath;
		}

		if (FolderPath.StartsWith(TEXT("Game/")))
		{
			return FString::Printf(TEXT("/%s"), *FolderPath);
		}

		return FString::Printf(TEXT("/Game/%s"), *FolderPath);
	}

	namespace
	{
		FString BuildWorldPartitionExternalPackagePrefix(const FString& WorldPackagePath, const TCHAR* ExternalFolderName)
		{
			if (!WorldPackagePath.StartsWith(TEXT("/")) || !ExternalFolderName || !*ExternalFolderName)
			{
				return FString();
			}

			const int32 MountRootEnd = WorldPackagePath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 1);
			if (MountRootEnd == INDEX_NONE)
			{
				return FString();
			}

			const FString MountRoot = WorldPackagePath.Left(MountRootEnd + 1);
			const FString RelativeWorldPath = WorldPackagePath.RightChop(MountRootEnd + 1);
			if (RelativeWorldPath.IsEmpty())
			{
				return FString();
			}

			return FString::Printf(TEXT("%s%s/%s/"), *MountRoot, ExternalFolderName, *RelativeWorldPath);
		}
	}

	bool IsExternalPackageOfWorldPartitionLevel(const FString& SavedPackageName, const UWorld* EditorWorld)
	{
		if (!EditorWorld)
		{
			return false;
		}

		const UPackage* WorldPackage = EditorWorld->GetOutermost();
		if (!WorldPackage)
		{
			return false;
		}

		const FString WorldPackagePath = UWorld::RemovePIEPrefix(WorldPackage->GetName());
		const FString NormalizedSavedPackageName = UWorld::RemovePIEPrefix(SavedPackageName);
		if (WorldPackagePath.IsEmpty() || NormalizedSavedPackageName.IsEmpty())
		{
			return false;
		}

		const FString ExternalActorsPrefix = BuildWorldPartitionExternalPackagePrefix(WorldPackagePath, TEXT("__ExternalActors__"));
		if (!ExternalActorsPrefix.IsEmpty() && NormalizedSavedPackageName.StartsWith(ExternalActorsPrefix))
		{
			return true;
		}

		const FString ExternalObjectsPrefix = BuildWorldPartitionExternalPackagePrefix(WorldPackagePath, TEXT("__ExternalObjects__"));
		if (!ExternalObjectsPrefix.IsEmpty() && NormalizedSavedPackageName.StartsWith(ExternalObjectsPrefix))
		{
			return true;
		}

		return false;
	}
}
