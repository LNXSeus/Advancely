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

⚠️ **CRITICAL: DO NOT RUN FROM DOWNLOADS FOLDER**
If you run the app directly from Downloads, macOS will block it from saving settings (App Translocation).
**YOU MUST MOVE THE APP** to your **Applications** folder or **Desktop** before running it.

✔️ **Method 1: The Right-Click Trick (Most Common)**

1. **MOVE** `Advancely.app` and the `resources` folder to your **Applications** folder.
2. **RIGHT-CLICK** (or Control+Click) `Advancely.app` and choose **"Open"**.
3. A warning box will appear. Click the **"Open"** button.
4. You only need to do this once.

✔️ **Method 2: System Settings (If Method 1 fails)**

If the app does not open or says it is damaged/cannot be checked:
1. Attempt to open the app normally so the error message appears. Click OK.
2. Open **System Settings** -> **Privacy & Security**.
3. Scroll down to the **Security** section.
4. You should see a message saying "Advancely was blocked...". Click **"Open Anyway"**.
5. Enter your password if prompted.

✔️ **Method 3: Terminal Authorization (The "Nuclear" Option)**

If the above fail, run this command to remove the quarantine attribute:
1. Open the **Terminal** app.
2. Type `xattr -cr ` (note the space at the end).
3. Drag `Advancely.app` into the terminal window.
4. Press Enter.

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

- **For ALL Users:** Place the Advancely folder in a path with only English characters.
- **On macOS:** Keep `.app` and `resources` together. If it won't open, Right-Click -> Open, or check System Settings > Privacy & Security.
- **On Windows/Linux:** Don't move the executable out of its folder. Create a shortcut/link to it instead.