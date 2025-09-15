==================================================
IMPORTANT: How to Run Advancely Correctly
==================================================

Thank you for using Advancely! To ensure the application works correctly, please follow the instructions for your operating system.

--------------------------------------------------
--- For Windows & Linux Users ---
--------------------------------------------------

Your application folder contains the main executable (`Advancely.exe` or `Advancely`) along with required libraries (`.dll` or `.so` files) and the `resources` folder.

✔️ **CORRECT WAY:**
To run Advancely from another location (like your Desktop), please create a shortcut or a symbolic link.
1. Right-click on the main executable.
2. Select "Create shortcut" (Windows) or "Create Link" (Linux).
3. You can move this new shortcut/link file anywhere you like.

❌ **INCORRECT WAY:**
DO NOT move or copy the original executable file by itself. It will not be able to find its resources or libraries and will fail to start. The executable must always stay in the same folder as its supporting files.

--------------------------------------------------
--- For macOS Users ---
--------------------------------------------------

Your application is a self-contained `Advancely.app` bundle. All required resources and libraries are located inside it.

✔️ **CORRECT WAY:**
You can move the `Advancely.app` file anywhere you like, such as your Desktop or the Applications folder. To create a quick-launch link, simply drag the `Advancely.app` icon to your Dock.

❌ **INCORRECT WAY:**
Do not "Show Package Contents" and move or modify any of the internal files, as this can break the application.

==================================================
TL;DR (Summary)
==================================================

- **Windows/Linux:** Don't move the executable. Create a shortcut/link to it instead.
- **macOS:** You can move the `Advancely.app` file anywhere you want. Drag it to your Dock for easy access.