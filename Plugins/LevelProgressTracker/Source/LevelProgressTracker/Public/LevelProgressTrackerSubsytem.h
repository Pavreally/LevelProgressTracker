// Pavel Gornostaev <https://github.com/Pavreally>

/**
 * The Level Progress Tracker (LPT) subsystem provides control over the level resource loading process, 
 * enabling real-time monitoring of the percentage of loading progress, the current number of loaded 
 * assets, the total number of assets, and the level name.
 * 
 * The core idea of the subsystem is to automatically scan the target level and retrieve a list of its assets, 
 * then preload all discovered resources before the level itself is loaded. This means that when a new level
 * is opened, only its shell is loaded, while all its resources are already present in memory.
 * 
 * The subsystem supports two loading modes: standard and streaming (embedded). An embedded streaming
 * level refers to a target level that is dynamically loaded in real time into the currently active main game level.
 * 
 * After loading a level in standard mode (similar to OpenLevel), the handler reference is automatically
 * released, thereby handing over full memory management to Unreal Engine's default system. If the 
 * level is loaded via streaming (similar to LoadLevelInstanceBySoftObjectPtr), you are responsible
 * for managing the lifetime of the level by retaining both the handler and the reference to the streaming
 * level. This reference can later be used to unload the level asynchronously.
 * 
 * Note: In both cases, once resources are unloaded, full memory control is delegated back to Unreal Engine’s 
 * standard memory management system.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/LevelStreamingDynamic.h"

#include "LevelProgressTrackerSubsytem.generated.h"

struct FStreamableHandle;

// Contains data for a streaming embedded game level.
USTRUCT(BlueprintType)
struct FLevelInstanceState
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY()
	TObjectPtr<ULevelStreamingDynamic> LevelReference = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "Position and size of the game level."))
	FTransform Transform;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "Allows you to specify a custom class instead of the standard one."))
	TSubclassOf<ULevelStreamingDynamic> OptionalLevelStreamingClass = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "If this is true, the level is loaded as a temporary package that is not saved to disk."))
	bool bLoadAsTempPackage = false;
};

// Primary structure for information about a loadable game level.
USTRUCT(BlueprintType)
struct FLevelState
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "Soft link to target level."))
	TSoftObjectPtr<UWorld> LevelSoftPtr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "A common name for a game level."))
	FName LevelName;

	TSharedPtr<FStreamableHandle> Handle;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "Total assets the target level contains."))
	int32 TotalAssets = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "Total assets that have been loaded into memory and are currently held in memory."))
	int32 LoadedAssets = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "If true, then the level is streaming (in this case, the FLevelInstanceState structure is used). If false, then this is a regular level that is loaded similarly to the 'OpenLevel' function."))
	bool bIsStreamingLevel = false;

	UPROPERTY()
	FLevelInstanceState LevelInstanceState;
};

/**
 * Level Progress Tracker Subsystem Class
 */
UCLASS(BlueprintType, DisplayName = "LPT Subsystem")
class LEVELPROGRESSTRACKER_API ULevelProgressTrackerSubsytem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~End USubsystem

#pragma region DELEGATES
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnLevelLoadProgressLPT, TSoftObjectPtr<UWorld>, LevelSoftPtr, FName, LevelName, FName, PackageName, float, Progress);
	// Notification about the current progress of asset loading.
	UPROPERTY(BlueprintAssignable, Category = "LPT Subsystem")
	FOnLevelLoadProgressLPT OnLevelLoadProgressLPT;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInstanceLevelLoadedLPT, TSoftObjectPtr<UWorld>, LevelSoftPtr, FName, LevelName);
	// Streaming level loading notification.
	UPROPERTY(BlueprintAssignable, Category = "LPT Subsystem")
	FOnInstanceLevelLoadedLPT OnInstanceLevelLoadedLPT;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGlobalLevelLoadedLPT, TSoftObjectPtr<UWorld>, LevelSoftPtr, FName, LevelName);
	// Delegate for the case of loading a level via the 'OpenLevel' function.
	UPROPERTY(BlueprintAssignable, Category = "LPT Subsystem")
	FOnGlobalLevelLoadedLPT OnGlobalLevelLoadedLPT;

#pragma endregion DELEGATES

	/**
	 * The main data storage of the LPT subsystem. It stores information about the level and a reference to resources, 
	 * which keeps resources in memory and prevents the garbage collector from cleaning them up before the level is quickly loaded.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "The main data storage of the LPT subsystem."))
	TMap<FName, FLevelState> LevelLoadedMap;

	/**
	 * A function that opens a new level. Similar to the standard 'OpenLevel' function.
	 * @param LevelSoftPtr Soft link to target level.
	 */
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem")
	void OpenLevelLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr);

	/**
	 * Function that causes an asynchronous loading of an embedded level into the current game level. 
	 * Similar to the standard 'LoadLevelInstanceBySoftObjectPtr' function.
	 * @param LevelSoftPtr Soft link to target level.
	 * @param Transform Position and size of the game level.
	 * @param OptionalLevelStreamingClass Allows you to specify a custom class instead of the standard one.
	 * @param bLoadAsTempPackage If this is true, the level is loaded as a temporary package that is not saved to disk.
	 */
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem")
	void LoadLevelInstanceLPT(
		TSoftObjectPtr<UWorld> LevelSoftPtr,
		const FTransform Transform,
		TSubclassOf<ULevelStreamingDynamic> OptionalLevelStreamingClass = nullptr,
		bool bLoadAsTempPackage = false
	);

	/**
	 * Unloads the streaming level and breaks the reference to cached resources in memory, 
	 * handing over memory control to the standard Unreal Enigne system.
	 * @param LevelSoftPtr Soft link to target level.
	 */
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem")
	void UnloadLevelInstanceLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr);

	/**
	 * Unloads all streaming levels and breaks the link to cached resources in memory, 
	 * handing over memory control to the standard Unreal Enigne system.
	 */
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem")
	void UnloadAllLPT();

protected:
	/**
	 * Scans the selected level and, based on the received data, asynchronously loads all found assets and resources into memory.
	 * @param LevelSoftPtr Soft link to target level.
	 * @param bIsStreamingLevel Will the level be downloaded via streaming.
	 * @param LevelInstanceState If the level is streaming, then parameters for function 'LoadLevelInstanceBySoftObjectPtr()' are passed to it.
	 */
	UFUNCTION()
	void AsyncLoadAssetsLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, bool bIsStreamingLevel = false, FLevelInstanceState LevelInstanceState = FLevelInstanceState());

private:
	// Callback when all downloads are complete.
	UFUNCTION()
	void OnAllAssetsLoaded(FName PackageName, bool bIsStreamingLevel);

	// Сallback when the global level is fully loaded.
	void OnPostLoadMapWithWorld(UWorld* LoadedWorld);

	// Callback when loading each asset.
	void HandleAssetLoaded(TSharedRef<FStreamableHandle> Handle, FName PackageName);
};
