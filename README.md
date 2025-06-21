![Level Progress Tracker](./_Misc/Preview.png)

# Level Progress Tracker
LPT is a plugin for Unreal Engine 5 that allows you to quickly and easily create a loading screen with a progress bar for level transitions.

<br>

> [!NOTE]
> The plugin has been pre-packaged only for Win64 and Android.

## Latest Updates
`Experimental` `Beta` `Version 1.0.0` 
- Build version for Unreal Engine 5.6.0

## What it's for
- Tracking the progress of level asset loading.

## Features
- Ultra-fast setup and creation of a loading screen for level transitions.
- Tracks the loading progress of both regular and streaming levels.
- Automatically scans specified levels for assets and monitors their loading progress in real time.
- Tracks level asset loading packages and their count.
- Supports subscribing to notifications when level assets finish loading.

## Install

> [!NOTE]
> Starting with Unreal Engine version 5.6, it is recommended to use the new project type based on C++. Otherwise, when creating a new Blueprint project, you may encounter the following error: "Engine modules are out of date, and cannot be compiled while the engine is running. Please build through your IDE." This issue does not occur in a pure C++ project.

1. Make sure the Unreal Engine editor is closed.
2. Move the "Plugins" folder to the root folder of your created project.
3. Run your project to which the "Plugins" folder with 'LevelProgressTracker' was added. If a message about restoring the module appears, select "Yes".
4. Done! The 'Level Progress Tracker' folders should appear in the Unreal Engine browser and the plugin should be automatically activated. If the plugin folder is not visible, activate visibility through the browser settings: `Settings > Show Plugin Content`.

## How to use it?
An interactive step-by-step tutorial on how to use LPT can be found in the file: `B_LPT_GameMode_Demo`, which is located at the path `Plugins\Level Progress Tracker Content\DemoFiles\`.

![Window Manager](./_Misc/Tutorial/Tutorial_1.jpg)
![Window Manager](./_Misc/Tutorial/Tutorial_2.jpg)
![Window Manager](./_Misc/Tutorial/Tutorial_3.jpg)
![Window Manager](./_Misc/Tutorial/Tutorial_4.jpg)

## (C++) Documentaion
All sources contain self-documenting code.
