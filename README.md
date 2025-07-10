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

## Supporting 100+ Minecraft versions (1.0–1.21.7+)

Find the full list of supported versions [here](https://pastebin.com/NhkaT3qD).

This tracker supports **any full release** from Minecraft 1.0 upwards including all april fool snapshots. It does so by only reading from the stats file for any version prior to 1.12. From 1.12 upwards in reads the stats and advancements file; and for the 25w14craftmine snapshot it additionally reads the unlocks file.

* **1.0–1.6.4**: It reads the global stats file and takes a snapshot when a new world is created and then starts counting from zero to simulate local per-world stats. Working with these versions requires knowing the IDs of stats and advancements. You can find the translations for those IDs [here](https://pastebin.com/qPsgc4Eb) and the individual item IDs [here](https://pastebin.com/r5tpjPFm). (Credits: [minecraftinfo.com](https://www.minecraftinfo.com/idlist.htm))
* **1.7.2–1.11.2**: Advancely reads the stats file for stats and achievements  and playtime is tracked through the stat.playOneMinute entry.
* **1.12–1.16.5**: Advancely reads the stats and advancements file and playtime is tracked through the minecraft:play_one_minute entry.
* **1.17+**: Advancely reads the stats and advancements file and playtime is tracked through the minecraft:play_time entry.
* **25w14craftmine**: Advancely reads the stats, advancements, and unlocks file. (Game version 1.21.5)


## How To Use

1.  **Download**: Grab the latest release from the [**release page**](https://github.com/LNXSeus/Advancely/releases) (supports Windows, Linux, and MacOS).
2.  **Configure**: Open `settings.json` to select your Minecraft version, category, and save path detection mode (`auto` or `manual`). You can also configure hotkeys there.
3.  **Run**: Launch Advancely and speedrun Minecraft. The tracker will automatically find your most recent world and display your progress in real time.

### Licenses

*This project uses dmon by Sepehr Taghdisian, licensed under the BSD 2-Clause License.*
*This project also uses the SDL3 library suite and cJSON. More information can be found in the [LICENSES.txt](LICENSES.txt) file.*