![Level Progress Tracker](./_Misc/Preview.png)

# Level Progress Tracker
LPT is a plugin for Unreal Engine 5 that allows you to quickly and easily create a loading screen with a progress bar for level transitions.

<br>

> [!NOTE]
> The plugin has been pre-packaged only for Win64 and Android.

## Latest Updates
`Experimental`

`Version 1.4.0`
- The plugin has been upgraded to a new major version due to a significant overhaul of the asset filtering setup approach and the addition of much more flexible support for World Partition levels.
- All configuration settings are now stored in dedicated Data Assets, unique to each level.
This greatly improves optimization and management when working with a large number of game levels.
- Added more flexible configuration of storage paths for asset lists and their filtering rules.
- For World Partition levels, Asset Collections have been introduced.
This means that for each Data Layer, you can now pre-create an asset collection and preload it procedurally — even before the level is opened — in a already packaged, shipping build of the game.
During level loading, you can load these collections either
<br> - by tag (loading an entire group at once), or
<br> - individually by specific keys.
<br>Example use case: In one session you can preload the boss arena section ahead of time, while in the next session you preload the checkpoint area instead.

- Complete global code refactoring and optimization.
- Added widget filtering option.
- Fixed and significantly improved asset filtering logic during the level database list creation stage.
- Introduced the ability to manually set per-level asset preloading step size, allowing you to balance precision and performance individually for each level.


## What it's for
- Tracking the progress of level asset loading.

## Features
- Ultra-fast setup and creation of a loading screen for level transitions.
- Automatic level type recognition: World Partition, Streaming Level, or regular.
- Tracks the loading progress of both regular and streaming levels.
- Extremely flexible loading progress bar configuration.
You can choose one of three approaches:
<br> - fully automatic asset detection for the level.
<br> - completely manual list of assets to load.
<br> - or hybrid mode (automatic + manual overrides).

- Optional resource loading. It can be disabled, but in that case, progress tracking for resource loading will not work. However, delegates for full level loading will still function.
- Tracks level asset loading packages and their count.
- Built-in functions for loading screens — simply add your UMG widget, and it will function as a Slate widget. This means that your specified UMG widget will not be forcibly closed during a level transition.
- Powerful global asset filtering system. You can add individual assets, entire folders, Data Layers or Cells either as exclusions (blacklist) or as allowed items only (whitelist / inverse mode).
- Convenient plugin button that lets you quickly configure filtering rules specifically for the current level right from the editor.

## Install

> [!NOTE]
> Starting with Unreal Engine version 5.6, it is recommended to use the new project type based on C++. After copying the plugin folder, be sure to perform a full project rebuild in your C++ IDE.

1. Make sure the Unreal Engine editor is closed.
2. Move the "Plugins" folder to the root folder of your created project.
3. Run your project to which the "Plugins" folder with 'LevelProgressTracker' was added. If a message about restoring the module appears, select "Yes".
4. Done! The 'Level Progress Tracker' folders should appear in the Unreal Engine browser and the plugin should be automatically activated. If the plugin folder is not visible, activate visibility through the browser settings: `Settings > Show Plugin Content`.

## How to use it?
An interactive step-by-step tutorial on how to use LPT can be found in the file: `B_LPT_GameMode_Demo`, which is located at the path `Plugins\Level Progress Tracker Content\DemoFiles\`.

![Level Progress Tracker](./_Misc/Tutorial/Slide_1.jpg)
![Level Progress Tracker](./_Misc/Tutorial/Slide_2.jpg)
![Level Progress Tracker](./_Misc/Tutorial/Slide_3.jpg)
![Level Progress Tracker](./_Misc/Tutorial/Slide_4.jpg)

## (C++) Documentaion
All sources contain self-documenting code.
