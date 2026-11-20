# REPENTOGON J273 compatibility preview

This build covers Repentance+ **1.9.7.12.J273**. Install it in the **Repentogon** game subdirectory created by the official REPENTOGON Launcher. The Steam J510 installation keeps its own HighFPS build.

The official Launcher patch for Steam 1.9.7.19.J510 produces an x86 executable that identifies itself as J273. Its PE timestamp is 0x68154d8d, its image size is 0x88d000, and the SHA-256 is ceb598b4e5e03dabbd2da6ea322a9aa6ffc66fcc377aeea4391f008403b7faa4. Use **tools/audit-j273.py** with the path to the J273 executable to check the file and the three native patch sites.

The loader checks the PE identity before reading any engine addresses. It checks the exact update/render call sequence and both limiter guards before patching. The J273 route leaves Lua imports and file lookups in REPENTOGON's control. Mods' render callbacks follow display presentations in this preview, so mods that assume 60 render callbacks each second can behave differently above 60 FPS. The default software ceiling is 90 FPS and the game controls its swap interval.

Build with **build.bat** on Windows or **build-wsl.sh** with an x86 Windows SDK on Linux. Exit the game, back up any existing **winmm.dll** in the **Repentogon** subdirectory and copy in **build/winmm.dll**. Set the Proton override **WINEDLLOVERRIDES=winmm=n,b** alongside the existing REPENTOGON launch settings.
