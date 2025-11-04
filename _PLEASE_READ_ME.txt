==================================================
IMPORTANT: How to Run Advancely Correctly
==================================================

Thank you for using Advancely!
To ensure the application works correctly, please follow the instructions for your operating system.

--------------------------------------------------
--- For ALL Users: Use English-Only (ASCII) File Paths ---
--------------------------------------------------

To prevent errors, please make sure the **entire folder path** to your Advancely application contains **only standard English (ASCII) characters**.

Special characters, accents (like `é`, `ü`, `ñ`), or symbols in the file path can cause the tracker to fail when loading templates or watching your save files.

- **BAD Path:** `C:\Users\Jörg\Desktop\Spiele\Advancely\`
- **GOOD Path:** `C:\Users\Joerg\Desktop\Games\Advancely\`

--------------------------------------------------
--- For macOS Users ---
--------------------------------------------------

Your download contains the `Advancely.app` application and a `resources` folder.
**Both must be kept in the same directory for the app to work.**

Due to macOS security (Gatekeeper), you must authorize the app before running it.

✔️ **First-Time Run (Easy Method):**

1. Double-click `Advancely.app`. You will see an error. Click OK.
2. NOW, RIGHT-CLICK the `Advancely.app` file and choose "Open".
3. A new window will appear. Click the "Open" button on this window.
4. You only need to do this once.

✔️ **Alternative Method (Terminal Authorization):**

1. Open the **Terminal** app (you can find it in Applications -> Utilities).
2. Type `xattr -cr ` (note the space at the end) but DO NOT press Enter yet.
3. Drag the `Advancely.app` file from Finder and drop it onto the Terminal window.
4. The command should now look like this: `xattr -cr /path/to/your/Advancely.app`
5. Press **Enter**.
6. You can now double-click `Advancely.app` to run it.

✔️ **Running from the Terminal (for Debugging):**

If you can't open the app or want to see error messages:

1. Open the **Terminal** app.
2. Type `cd ` (with a space), then drag the folder containing `Advancely.app` onto the Terminal window and press **Enter**.
3. Type or paste this command and press **Enter**: `./Advancely.app/Contents/MacOS/Advancely`

✔️ **CORRECT WAY TO INSTALL:**

* For the best experience, move **both** `Advancely.app` and the `resources` folder into your main `/Applications` folder.
* You can then drag `Advancely.app` from the Applications folder to your Dock to add a shortcut.

❌ **INCORRECT WAY:**
DO NOT separate the `Advancely.app` file from its `resources` folder.
The application needs the `resources` folder to load settings, templates, icons, and fonts.

--------------------------------------------------
--- For Windows & Linux Users ---
--------------------------------------------------

Your application folder contains the main executable (`Advancely.exe` or `Advancely`), required library files, and the `resources` folder.

✔️ **CORRECT WAY:**
To run Advancely from another location (like your Desktop), please create a shortcut.
- **Windows:** Right-click `Advancely.exe` -> "Create shortcut".
- **Linux:** Right-click `Advancely` -> "Create Link".
You can move this new shortcut or link anywhere you like.

❌ **INCORRECT WAY:**
DO NOT move or copy the original executable file by itself.
It will not be able to find its resources or libraries and will fail to start, run, or save correctly.
The executable must always stay in the same folder as its supporting files.

==================================================
TL;DR (Summary)
==================================================

- **For ALL Users:** Place the Advancely folder in a path with only English characters (no accents like é, ü, etc.).
- **On macOS:** Run the one-time Terminal command on `Advancely.app`. Keep the `.app` and `resources` folder together.
- **On Windows/Linux:** Don't move the executable out of its folder. Create a shortcut/link to it instead.