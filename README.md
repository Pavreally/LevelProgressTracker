![Level Progress Tracker](./_Misc/Preview.png)

# Level Progress Tracker
LPT is a plugin for Unreal Engine 5 that allows you to quickly and easily create a loading screen with a progress bar for level transitions.

<br>

> [!NOTE]
> The plugin has been pre-packaged only for Win64 and Android.

## Latest Updates
`Experimental`

`Version 1.4.1`
- Built for Unreal Engine 5.7.4.
- Fixed an issue with asset generation during the cooking process of World Partition levels.

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
- Preload asset list collections that can be loaded by tag (highly useful when working with World Partition levels).
- The "Preload Chunk Size" option allows you to specify the amount of assets loaded per single operation. Lower values offer higher precision but result in slower loading times.

## Install

> [!NOTE]
> Starting with Unreal Engine version 5.6, it is recommended to use the new project type based on C++. After copying the plugin folder, be sure to perform a full project rebuild in your C++ IDE.

1. Make sure the Unreal Engine editor is closed.
2. Move the "Plugins" folder to the root folder of your created project.
3. Rebuild the project in your C++ IDE.
4. Done! The 'Level Progress Tracker' folders should appear in the Unreal Engine browser and the plugin should be automatically activated. If the plugin folder is not visible, activate visibility through the browser settings: `Settings > Show Plugin Content`.

## How to use it?
An interactive step-by-step tutorial on how to use LPT can be found in the file: `B_LPT_GameMode_Demo`, which is located at the path `Plugins\Level Progress Tracker Content\DemoFiles\`.

![Level Progress Tracker](./_Misc/Tutorial/Slide_1.jpg)
![Level Progress Tracker](./_Misc/Tutorial/Slide_2.jpg)
![Level Progress Tracker](./_Misc/Tutorial/Slide_3.jpg)
![Level Progress Tracker](./_Misc/Tutorial/Slide_4.jpg)

## (C++) Documentaion
All sources contain self-documenting code.
