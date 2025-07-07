![Advancely Logo](/resources/gui/Advancely_Logo.png)

# A highly customizable tool to track Minecraft progress beyond just Advancements. <br>
## This tracker supports modded advancements/recipes, custom statistics, unlocks, multi-stage goals, manual goals and custom counters. <br>
### This tracker is built to be flexible, supporting everything from vanilla, through modded advancements and even custom datapacks. It automatically detects changes in your latest world save file, providing real-time progress updates.

## Download (Windows, Linux, MacOS)

[**Advancely Releases**](https://github.com/LNXSeus/Advancely/releases)

[My YouTube](https://www.youtube.com/@lnxs?sub_confirmation=1)

## Core Features

* **Comprehensive Tracking**: Advancely isn't limited to just advancements. It tracks multiple data types directly from your save file, including:

    * **Advancements/Recipes**: Tracks completion status and individual criteria as well as recipes.
    * **Statistics**: Monitors any game statistic, such as blocks mined or distance flown, against a target value.
    * **Unlocks**: Allows you to fully track unlocks, which is exclusive to the 25w14craftmine snapshot.

* **Full Mod & Datapack Support**: The tracker is designed to be data-driven. It correctly parses items, stats, and advancements from any mod or data pack (e.g., `conquest:`, `blazeandcave:`) without being limited to the vanilla `minecraft:` namespace.

* **Multi-Stage Goals**: Create long-term objectives that combine several smaller steps. A single goal can require you to first achieve an advancement, then reach a certain statistic, then an unlock (from the 25w14craftmine) and lastly complete another advancement in sequence.

* **Custom Counters with Hotkeys and Manual Tracking**: For goals that can't be automatically tracked (like counting amount of structures visited or finding specific items), you can create custom counters or check them off manually. Set a target value in your template and use configurable hotkeys to increment or decrement the count. List of hotkeys can be found [here](https://pastebin.com/vPKgWAen). Currently the hotkeys only work when tabbed into the tracker window. All progress is saved automatically back into `settings.json`.

## Customization

The true power of Advancely lies in its customization. You can define exactly what you want to track by creating your own template files from within the settings window.

You select the minecraft version, the category, all the advancements, statistics, goals you want to track with custom translations. The tracker will then parse your template and track progress against the goals you've defined.

## Supported Minecraft Versions (Java Edition Only)

This tracker supports **any version** from Minecraft 1.0 upwards. It does so by only reading from the stats file for any version prior to 1.12. From 1.12 upwards in reads the stats and advancements file and for the 25w14craftmine snapshot it additionally reads the unlocks file.

## How To Use

1.  **Download**: Grab the latest release from the releases page (supports Windows, Linux, and MacOS).
2.  **Configure**: Open `settings.json` to select your Minecraft version, category, and save path detection mode (`auto` or `manual`). You can also configure hotkeys there.
3.  **Run**: Launch Advancely and play Minecraft. The tracker will automatically find your most recent world and display your progress in real time.

### Licenses

*This project uses dmon by Sepehr Taghdisian, licensed under the BSD 2-Clause License.*
*This project also uses the SDL3 library suite and cJSON. More information can be found in the [LICENSES.txt](LICENSES.txt) file.*