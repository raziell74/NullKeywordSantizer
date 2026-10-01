**Null Keyword Santizer** is an SKSE plugin for Skyrim Special Edition, built with [CommonLibSSE NG](https://github.com/alandtse/CommonLibSSE-NG). It targets Skyrim SE and AE.

Some items pick up keywords whose EditorID is blank. Mods that walk an item's keyword array can crash when they use one of those keywords. This plugin packs those keywords out of the array on armor, weapons, ammo, misc items (including keys and soul gems), books, notes, alchemy items, ingredients, and scrolls. The keyword form stays loaded. Other plugins and Papyrus then see a shorter array that does not include it.

The scan runs when Keyword Item Distributor finishes, after every plugin has handled data load, and when a game is started or loaded. Copies the game makes later, such as crafted items, are cleaned as they are copied. `General.Enabled=false` turns this off.

## Requirements

- [SKSE](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Installation

Install using your favorite mod manager or manually extract the contents of the archive to your Skyrim Special Edition Data folder. Please also make sure you also have all of the required mods installed.

Load order doesn't matter. There is no plugin file.

Logging is controlled from `SKSE/Plugins/NullKeywordSantizer.ini`. `Level=info` is the default. Each scan logs how many blank keywords were removed. Per-item detail is written at `debug`.

## Uninstallation

Uninstall using your favorite mod manager or manually delete the files from your Skyrim Special Edition Data folder.

## Compatibility

The SKSE plugin is built using [CommonLibSSE NG](https://github.com/alandtse/CommonLibSSE-NG). It targets Skyrim SE 1.5.97, AE 1.6.1170, and AE 1.7.99.

## Credits

SKSE Source Code: Null Keyword Santizer - Licensed under GPL-3.0-or-later, same as [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG).
