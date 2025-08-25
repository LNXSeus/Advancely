==================================================
IMPORTANT: How to Run Advancely Correctly
==================================================

Thank you for using Advancely!

This application is not a single, self-contained file. It relies on several other files and folders to work correctly, including `.dll` files (shared libraries) and the `resources` folder which contains all icons, fonts, and templates.

To ensure the tracker works, the main executable file must **ALWAYS** remain in its original folder next to these required files.

--------------------------------------------------
--- For Windows Users ---
--------------------------------------------------

✔️ **CORRECT WAY:** To run Advancely from another location like your Desktop, please create a shortcut.
    1. Right-click on `Advancely.exe`.
    2. Select "Create shortcut" or "Send to > Desktop (create shortcut)".
    3. You can move this new shortcut file anywhere you like.

❌ **INCORRECT WAY:** DO NOT move, copy, or drag the original `Advancely.exe` file out of its folder. If you do, it will lose its connection to the required `.dll` files and the `resources` folder, and it will fail to start or run correctly.

--------------------------------------------------
--- For macOS Users ---
--------------------------------------------------

✔️ **CORRECT WAY:** The macOS equivalent of a shortcut is an "Alias".
    1. Right-click on the `Advancely.app` application.
    2. Select "Make Alias".
    3. You can move this new alias file anywhere you like.
    (Alternatively, you can drag the `Advancely.app` icon to your Dock to create a permanent link there.)

❌ **INCORRECT WAY:** DO NOT move the `Advancely.app` file away from its supporting files and folders. Do not open the `.app` bundle (Show Package Contents) and move the internal executable file.

--------------------------------------------------
--- For Linux Users ---
--------------------------------------------------

✔️ **CORRECT WAY:** The best way to run Advancely is to create a launcher or a symbolic link (symlink) that points to the original executable. The exact steps may vary depending on your desktop environment (GNOME, KDE, etc.).

❌ **INCORRECT WAY:** DO NOT move the main `Advancely` executable away from its folder. It needs to be in the same location as its shared libraries (`.so` files) and the `resources` folder to work.


==================================================
TL;DR (Summary)
==================================================

- ✔️ **DO** create a shortcut, alias, or launcher to run the program from your Desktop or Dock.
- ❌ **DO NOT** move the original executable (`Advancely.exe`, `Advancely.app`, etc.) out of the main application folder.