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
class IAssetRegistry;
class SWidgetWrapLPT;

UENUM()
enum class ELevelLoadMethod : uint8
{
	Standard UMETA(DisplayName = "Standard"),
	LevelStreaming UMETA(DisplayName = "LevelStreaming"),
	WorldPartition UMETA(DisplayName = "WorldPartition")
};

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "If this is true, the level is loaded as a temporary package that is not saved to disk."))
	bool IsLoaded = false;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LPT Subsystem", meta = (ToolTip = "Level loading method. Can be standard, world partition or stream level."))
	ELevelLoadMethod LoadMethod = ELevelLoadMethod::Standard;

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
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnLevelLoadProgressLPT, TSoftObjectPtr<UWorld>, LevelSoftPtr, FName, LevelName, float, Progress, int32, LoadedAssets, int32, TotalAssets);
	// Notification about the current progress of asset loading.
	UPROPERTY(BlueprintAssignable, Category = "LPT Subsystem")
	FOnLevelLoadProgressLPT OnLevelLoadProgressLPT;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLevelLoadedLPT, TSoftObjectPtr<UWorld>, LevelSoftPtr, FName, LevelName);
	// Streaming level loading notification.
	UPROPERTY(BlueprintAssignable, Category = "LPT Subsystem")
	FOnLevelLoadedLPT OnLevelLoadedLPT;

#pragma endregion DELEGATES

	/**
	 * A function that opens a new level. Similar to the standard 'OpenLevel' function.
	 * @param LevelSoftPtr Soft link to target level.
	 * @param WhiteListDir A list of keywords used to specify paths to directories containing the necessary resources for the level. Only these will be processed and loaded. (Note: the full path does not need to be specified. For example, "/Game/" will match any path that contains this sequence.)
	 * @param PreloadingResources Before opening a level, its resources are automatically loaded. If false, then the calculation of loaded assets and progress does not work.
	 */
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem", meta = (AutoCreateRefTerm = "WhiteListDir"))
	void OpenLevelLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, TArray<FName> WhiteListDir, bool PreloadingResources = true);

	/**
	 * Function that causes an asynchronous loading of an embedded level into the current game level. 
	 * Similar to the standard 'LoadLevelInstanceBySoftObjectPtr' function.
	 * @param LevelSoftPtr Soft link to target level.
	 * @param WhiteListDir A list of keywords used to specify paths to directories containing the necessary resources for the level. Only these will be processed and loaded. (Note: the full path does not need to be specified. For example, "/Game/" will match any path that contains this sequence.)
	 * @param Transform Position and size of the game level.
	 * @param OptionalLevelStreamingClass Allows you to specify a custom class instead of the standard one.
	 * @param bLoadAsTempPackage If this is true, the level is loaded as a temporary package that is not saved to disk.
	 * @param PreloadingResources Before opening a level, its resources are automatically loaded. If False, then the calculation of loaded assets and progress does not work.
	 */
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem", meta = (AutoCreateRefTerm = "WhiteListDir"))
	void LoadLevelInstanceLPT(
		TSoftObjectPtr<UWorld> LevelSoftPtr,
		TArray<FName> WhiteListDir,
		const FTransform Transform,
		TSubclassOf<ULevelStreamingDynamic> OptionalLevelStreamingClass = nullptr,
		bool bLoadAsTempPackage = false,
		bool PreloadingResources = true
	);

	/**
	 * Unloads the streaming level and breaks the reference to cached resources in memory, 
	 * handing over memory control to the standard Unreal Enigne system.
	 * @param LevelSoftPtr Soft link to target level.
	 */
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem")
	void UnloadLevelInstanceLPT(const TSoftObjectPtr<UWorld> LevelSoftPtr, FName& LevelName);

	/**
	 * Unloads all streaming levels and breaks the link to cached resources in memory, 
	 * handing over memory control to the standard Unreal Enigne system.
	 */
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem")
	void UnloadAllLevelInstanceLPT();

	/**
	 * Creates a Slate widget as a wrapper for the target UMG widget.
	 * @param UserWidgetClass A target widget of type UMG that will be embedded into the parent Slate widget.
	 * @param ZOrder Widget display layer order.
	 */
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem")
	void CreateSlateWidgetLPT(TSubclassOf<UUserWidget> UserWidgetClass, int32 ZOrder);

	// Remove Slate widget
	UFUNCTION(BlueprintCallable, Category = "LPT Subsystem")
	void RemoveSlateWidgetLPT();

	// Returns true if the launch took place in the editor or false if the launch was not from the editor.
	UFUNCTION(BlueprintPure, Category = "LPT Subsystem")
	bool CheckingPIE();

protected:
	// Determining the level type (World Partition).
	bool CheckWorldPartition(const TSoftObjectPtr<UWorld>& LevelSoftPtr, IAssetRegistry& Registry);

	/**
	 * Checks the whitelist and filters the data if it is in it.
	 * @param WhiteListDir Whitelist containing directories of files allowed to be loaded before the level is opened.
	 * @param Dependencies Contains the names of all package dependencies.
	 * @param Paths Contains links to assets filtered by WhiteListDir.
	 */
	void GetFilteredWhiteList(TArray<FName>& WhiteListDir, TArray<FName>& Dependencies, TArray<FSoftObjectPath> &Paths);

private:
	/**
	 * The main data storage of the LPT subsystem. It stores information about the level and a reference to resources, 
	 * which keeps resources in memory and prevents the garbage collector from cleaning them up before the level is quickly loaded.
	 */
	TMap<FName, TSharedPtr<FLevelState>> LevelLoadedMap;

	// Storage for a Slate type widget. Required for the optional loading screen to work.
	TSharedPtr<SWidgetWrapLPT> SWidgetWrap;

	/**
	 * Scans the selected level and, based on the received data, asynchronously loads all found assets and resources into memory.
	 * @param LevelSoftPtr Soft link to target level.
	 * @param WhiteListDir A list of keywords used to specify paths to directories containing the necessary resources for the level. Only these will be processed and loaded. (Note: the full path does not need to be specified. For example, "/Game/" will match any path that contains this sequence.)
	 * @param PreloadingResources Before opening a level, its resources are automatically loaded. If False, then the calculation of loaded assets and progress does not work.
	 * @param bIsStreamingLevel Will the level be loaded via streaming.
	 * @param LevelInstanceState If the level is streaming, then parameters for function 'LoadLevelInstanceBySoftObjectPtr()' are passed to it.
	 */
	UFUNCTION()
	void AsyncLoadAssetsLPT(
		const TSoftObjectPtr<UWorld> LevelSoftPtr,
		TArray<FName>& WhiteListDir,
		bool PreloadingResources,
		bool bIsStreamingLevel = false,
		FLevelInstanceState LevelInstanceState = FLevelInstanceState());

	// Callback when all loads are complete.
	void OnAllAssetsLoaded(FName PackagePath, bool bIsStreamingLevel, TSharedRef<FLevelState> LevelState);

	// Сallback when the global level is fully loaded.
	void OnPostLoadMapWithWorld(UWorld* LoadedWorld);

	// Callback when loading each asset.
	void HandleAssetLoaded(TSharedRef<FStreamableHandle> Handle, FName PackagePath, TSharedRef<FLevelState> LevelState);

	// Request to open a game level
	void StartLevelLPT(FName PackagePath, bool bIsStreamingLevel, TSharedRef<FLevelState> LevelState);

	/**
	 * Scanning target level data for resource content and loading it
	 * @param Registry Reference to IAssetRegistry to get package dependencies.
	 * @param PackagePath The name of the level package (FName) for which we are looking for resources.
	 * @param WhiteListDir A list of keywords used to specify paths to directories containing the necessary resources for the level. Only these will be processed and loaded. (Note: the full path does not need to be specified. For example, "/Game/" will match any path that contains this sequence.)
	 * @param LevelState A shared FLevelState structure that stores progress, pointers, and streaming parameters.
	 * @param bIsStreamingLevel Will the level be loaded via streaming.
	 */
	void StartPreloadingResources(IAssetRegistry& Registry, FName& PackagePath, TArray<FName>& WhiteListDir, TSharedRef<FLevelState>& LevelState, bool& bIsStreamingLevel);

	// Call after loading the streaming level
	UFUNCTION()
	void OnLevelShown();
};
