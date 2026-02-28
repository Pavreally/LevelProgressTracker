// Pavel Gornostaev <https://github.com/Pavreally>

#include "DataLayerResolverLPT.h"
#include "LogLPTEditor.h"

#include "Engine/World.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"

namespace DataLayerResolverLPT
{
	namespace
	{
		const UDataLayerInstance* ResolveDataLayerInstanceByRuleName(const UDataLayerManager* DataLayerManager, const FName RuleName)
		{
			if (!DataLayerManager || RuleName.IsNone())
			{
				return nullptr;
			}

			if (const UDataLayerInstance* DataLayerInstance = DataLayerManager->GetDataLayerInstanceFromName(RuleName))
			{
				return DataLayerInstance;
			}

			FString RuleNameString = RuleName.ToString();
			RuleNameString.TrimStartAndEndInline();
			if (RuleNameString.IsEmpty())
			{
				return nullptr;
			}

			for (const UDataLayerInstance* DataLayerInstance : DataLayerManager->GetDataLayerInstances())
			{
				if (!DataLayerInstance)
				{
					continue;
				}

				if (RuleNameString.Equals(DataLayerInstance->GetDataLayerShortName(), ESearchCase::IgnoreCase) ||
					RuleNameString.Equals(DataLayerInstance->GetDataLayerFullName(), ESearchCase::IgnoreCase))
				{
					return DataLayerInstance;
				}
			}

			return nullptr;
		}
	}

	void AddDataLayerNameWithVariants(const FName InName, TArray<FName>& InOutNames)
	{
		if (InName.IsNone())
		{
			return;
		}

		auto AddUniqueName = [&InOutNames](const FName NameToAdd)
		{
			if (!NameToAdd.IsNone())
			{
				InOutNames.AddUnique(NameToAdd);
			}
		};

		AddUniqueName(InName);

		FString NameString = InName.ToString();
		if (NameString.IsEmpty())
		{
			return;
		}

		// Keep both full and short forms because Data Layer names can be represented
		// differently between actor descriptors and rule sources.
		auto AddShortVariant = [&AddUniqueName](const FString& Candidate)
		{
			if (!Candidate.IsEmpty())
			{
				AddUniqueName(FName(*Candidate));
			}
		};

		AddShortVariant(NameString);

		const int32 LastSlash = NameString.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastSlash != INDEX_NONE)
		{
			AddShortVariant(NameString.RightChop(LastSlash + 1));
		}

		const int32 LastDot = NameString.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastDot != INDEX_NONE)
		{
			AddShortVariant(NameString.RightChop(LastDot + 1));
		}

		const int32 LastColon = NameString.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastColon != INDEX_NONE)
		{
			AddShortVariant(NameString.RightChop(LastColon + 1));
		}
	}

	void ResolveWorldPartitionRegionRulesAsDataLayers(UWorld* World, FLPTLevelRules& InOutRules)
	{
		if (!World || (InOutRules.WorldPartitionDataLayerAssets.IsEmpty() && InOutRules.WorldPartitionRegions.IsEmpty()))
		{
			return;
		}

		UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
		if (!WorldPartitionSubsystem)
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("UWorldPartitionSubsystem is unavailable for '%s'. Continuing with best-effort Data Layer rule resolution."),
				*World->GetOutermost()->GetName()
			);
		}

		UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>();
		if (!DataLayerSubsystem)
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("UDataLayerSubsystem is unavailable for '%s'. Continuing with best-effort Data Layer rule resolution."),
				*World->GetOutermost()->GetName()
			);
		}

		UDataLayerManager* DataLayerManager = World->GetDataLayerManager();
		if (!DataLayerManager)
		{
			UE_LOG(LogLPTEditor, Warning, TEXT("UDataLayerManager is unavailable for '%s'. Keeping unresolved Data Layer name rules as-is."),
				*World->GetOutermost()->GetName()
			);
			return;
		}

		TArray<FName> ResolvedDataLayers;
		TSet<FName> UniqueResolvedDataLayers;
		ResolvedDataLayers.Reserve(InOutRules.WorldPartitionRegions.Num() + InOutRules.WorldPartitionDataLayerAssets.Num() * 2);

		auto AddResolvedDataLayerName = [&ResolvedDataLayers, &UniqueResolvedDataLayers](const FName DataLayerName)
		{
			if (DataLayerName.IsNone() || UniqueResolvedDataLayers.Contains(DataLayerName))
			{
				return;
			}

			UniqueResolvedDataLayers.Add(DataLayerName);
			ResolvedDataLayers.Add(DataLayerName);
		};

		for (const FName RegionRuleName : InOutRules.WorldPartitionRegions)
		{
			if (RegionRuleName.IsNone())
			{
				continue;
			}

			const UDataLayerInstance* DataLayerInstance = ResolveDataLayerInstanceByRuleName(DataLayerManager, RegionRuleName);
			if (!DataLayerInstance)
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("Data Layer '%s' was not found in world '%s'. Falling back to raw name match."),
					*RegionRuleName.ToString(),
					*World->GetOutermost()->GetName()
				);
				AddResolvedDataLayerName(RegionRuleName);
				continue;
			}

			AddResolvedDataLayerName(DataLayerInstance->GetDataLayerFName());
			AddResolvedDataLayerName(FName(*DataLayerInstance->GetDataLayerShortName()));
		}

		for (const TSoftObjectPtr<UDataLayerAsset>& DataLayerAssetRule : InOutRules.WorldPartitionDataLayerAssets)
		{
			const FSoftObjectPath DataLayerAssetPath = DataLayerAssetRule.ToSoftObjectPath();
			if (!DataLayerAssetPath.IsValid())
			{
				continue;
			}

			const UDataLayerAsset* DataLayerAsset = DataLayerAssetRule.LoadSynchronous();
			if (!DataLayerAsset)
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("Failed to load Data Layer asset '%s' in world '%s'. Rule will be ignored."),
					*DataLayerAssetPath.ToString(),
					*World->GetOutermost()->GetName()
				);
				continue;
			}

			const UDataLayerInstance* DataLayerInstance = DataLayerManager->GetDataLayerInstanceFromAsset(DataLayerAsset);
			if (!DataLayerInstance)
			{
				UE_LOG(LogLPTEditor, Warning, TEXT("Data Layer asset '%s' has no instance in world '%s'. Rule will be ignored."),
					*DataLayerAssetPath.ToString(),
					*World->GetOutermost()->GetName()
				);
				continue;
			}

			AddResolvedDataLayerName(DataLayerInstance->GetDataLayerFName());
			AddResolvedDataLayerName(FName(*DataLayerInstance->GetDataLayerShortName()));
		}

		InOutRules.WorldPartitionRegions = MoveTemp(ResolvedDataLayers);
	}
}

