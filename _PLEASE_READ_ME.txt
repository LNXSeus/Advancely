==================================================
IMPORTANT: How to Run Advancely Correctly
==================================================

Thank you for using Advancely! To ensure the application works correctly, please follow the instructions for your operating system.

--------------------------------------------------
--- For macOS Users ---
--------------------------------------------------

Your application is in a portable folder. Due to macOS security (Gatekeeper), after unzipping the file, you may see an error saying the application is "damaged". The app is not damaged. To fix this, you only need to run a single command in the Terminal.

✔️ **CORRECT WAY TO RUN (First Time):**

1.  Open the **Terminal** app (you can find it in Applications -> Utilities, or search for it with Spotlight).
2.  Type `xattr -cr ` (note the space at the end) but DO NOT press Enter yet.
3.  Drag the unzipped `Advancely` folder from Finder and drop it onto the Terminal window.
4.  The command should now look like this: `xattr -cr /path/to/your/Advancely-folder`
5.  Press **Enter**.
6.  You can now double-click the `Advancely` executable in the folder to run it.

After running this command once, you will not need to do it again for this copy of the application. To run Advancely from your Desktop or Dock, right-click the `Advancely` executable and select "Make Alias", then move the new alias anywhere you like.

❌ **INCORRECT WAY:**
DO NOT move or copy the original executable file out of its folder. It must stay next to the `resources` folder and library files to work correctly.

--------------------------------------------------
--- For Windows, macOS, & Linux Users ---
--------------------------------------------------

Your application folder contains the main executable (`Advancely.exe`, `Advancely`, etc.), required library files, and the `resources` folder.

✔️ **CORRECT WAY:**
To run Advancely from another location (like your Desktop or Dock), please create a shortcut.
- **Windows:** Right-click `Advancely.exe` -> "Create shortcut".
- **macOS:** Right-click `Advancely` -> "Make Alias".
- **Linux:** Right-click `Advancely` -> "Create Link".

You can move this new shortcut, alias, or link anywhere you like.

❌ **INCORRECT WAY:**
DO NOT move or copy the original executable file by itself. It will not be able to find its resources or libraries and will fail to start, run, or save correctly. The executable must always stay in the same folder as its supporting files.

==================================================
TL;DR (Summary)
==================================================

- On all systems, don't move the executable out of its folder. Create a shortcut/alias/link to it instead.