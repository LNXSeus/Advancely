==================================================
IMPORTANT: How to Run Advancely Correctly
==================================================

Thank you for using Advancely!
To ensure the application works correctly, please follow the instructions for your operating system.

--------------------------------------------------
--- For macOS Users ---
--------------------------------------------------

Your download contains the `Advancely.app` application and a `resources` folder.
**Both must be kept in the same directory for the app to work.**

Due to macOS security (Gatekeeper), you must authorize the app before running it.

⚠️ **CRITICAL: DO NOT RUN FROM DOWNLOADS FOLDER**
If you run the app directly from Downloads, macOS will block it from saving settings (App Translocation).
**YOU MUST MOVE THE APP** to your **Applications** folder or **Desktop** before running it.

✔️ **Method 1: Terminal Authorization (Best Option)**

If the above fail, run this command to remove the quarantine attribute:
1. Open the **Terminal** app.
2. Type `xattr -cr ` (note the space at the end).
3. Drag `Advancely.app` into the terminal window.
4. Press Enter.

✔️ **Method 2: The Right-Click Trick (Most Common)**

1. **MOVE** `Advancely.app` and the `resources` folder to your **Applications** folder.
2. **RIGHT-CLICK** (or Control+Click) `Advancely.app` and choose **"Open"**.
3. A warning box will appear. Click the **"Open"** button.
4. You only need to do this once.

✔️ **Method 3: System Settings (If Method 2 fails)**

If the app does not open or says it is damaged/cannot be checked:
1. Attempt to open the app normally so the error message appears. Click OK.
2. Open **System Settings** -> **Privacy & Security**.
3. Scroll down to the **Security** section.
4. You should see a message saying "Advancely was blocked...". Click **"Open Anyway"**.
5. Enter your password if prompted.

--------------------------------------------------
--- For Windows Users ---
--------------------------------------------------

Your application folder contains the main executable (`Advancely.exe`), required library files, and the `resources` folder.

✔️ **CORRECT WAY:**
To run Advancely from another location (like your Desktop), please create a shortcut.
- **Windows:** Right-click `Advancely.exe` -> "Create shortcut".

You can move this new shortcut anywhere you like.

❌ **INCORRECT WAY:**
DO NOT move or copy the original executable file by itself.
It will not be able to find its resources or libraries and will fail to start.
The executable must always stay in the same folder as its supporting files.

--------------------------------------------------
--- For Linux Users ---
--------------------------------------------------

Advancely is now installed as a system application via `.deb` or `.rpm` packages.

✔️ **INSTALLATION:**
1. Download the package for your distribution (`.deb` for Debian/Ubuntu, `.rpm` for Fedora/RHEL).
2. Install it using your package manager (e.g., `sudo apt install ./Advancely...deb`).
3. Launch "Advancely" from your application menu or run `advancely` in the terminal.

ℹ️ **FILE LOCATIONS (IMPORTANT):**
To comply with Linux standards, your user data is now stored separately:
- **User Data (Editable):** `~/.local/share/advancely/`
  (This is where `settings.json`, `templates/`, and `notes/` are stored)
- **System Files (Read-Only):** `/usr/share/advancely/`

🔄 **UPDATING:**
The internal auto-updater is disabled on Linux. To update, simply download and install the new package version. The launcher script will automatically migrate your data.

==================================================
TL;DR (Summary)
==================================================

- **For ALL Users:** Ensure paths contain only English characters.
- **On macOS:** Keep `.app` and `resources` together. Move out of Downloads.
- **On Windows:** Don't move the executable out of its folder. Create a shortcut instead.
- **On Linux:** Install the package. Edit settings/templates in `~/.local/share/advancely/`.