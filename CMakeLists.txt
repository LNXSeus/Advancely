# Copyright (c) 2026 LNXSeus. All Rights Reserved.
#
# This project is proprietary software. You are granted a license to use the software as-is.
# You may not copy, distribute, modify, reverse-engineer, or use this software
# or its source code in any way without the express written permission of the copyright holder.


# Minimum CMake version required
cmake_minimum_required(VERSION 3.20)

# Project name and language
project(Advancely C CXX) # c for cJSON, C++ for the rest

# Read the content of main.h into a variable for version
file(READ "${CMAKE_SOURCE_DIR}/source/main.h" MAIN_H_CONTENTS)

# Use a regular expression to find the line with the version and capture the number
# This specifically looks for the pattern #define ADVANCELY_VERSION "vX.Y.Z"
# and captures only the "X.Y.Z" part.
string(REGEX MATCH "#define ADVANCELY_VERSION \"v([0-9]+\\.[0-9]+\\.[0-9]+)\"" _ ${MAIN_H_CONTENTS})


# The captured version number is now in the variable CMAKE_MATCH_1
if (CMAKE_MATCH_1)
    set(PROJECT_VERSION ${CMAKE_MATCH_1})
    message(STATUS "Project version set from main.h: ${PROJECT_VERSION}")
else ()
    message(FATAL_ERROR "Could not parse ADVANCELY_VERSION from source/main.h!")
endif ()

# If running in GitHub Actions, export the version to the environment
if (DEFINED ENV{GITHUB_ENV})
    file(APPEND "$ENV{GITHUB_ENV}" "PROJECT_VERSION=${PROJECT_VERSION}\n")
    message(STATUS "Exporting PROJECT_VERSION=${PROJECT_VERSION} to GITHUB_ENV")
endif ()

# Enable the RC (Resource Compiler) language for Windows executables
if (WIN32)
    enable_language(RC)
endif ()

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 17) # Use widely supported C++ 17
set(CMAKE_CXX_STANDARD_REQUIRED True)


# --- 1. DEFINE PATHS & SOURCE FILES ---
set(EXECUTABLE_NAME "Advancely")
set(SOURCE_FILES
        "source/main.cpp"
        "source/init_sdl.cpp"
        "source/tracker.cpp"
        "source/overlay.cpp"
        "source/global_event_handler.cpp"
        "source/settings.cpp"
        "source/path_utils.cpp"
        "source/settings_utils.cpp"
        "source/file_utils.cpp"
        "source/format_utils.cpp" # for formatting strings
        "source/temp_creator.cpp"
        "source/temp_creator_utils.cpp"
        "source/template_scanner.cpp"
        "source/update_checker.cpp"
        "source/logger.cpp"
        "source/dialog_utils.cpp"

        # External libraries included as source and stay in C
        "source/external/cJSON.c"
        "source/external/miniz.c"
        "source/external/tinyfiledialogs.c"
        # dmon.h is header-only and included in main.cpp, so it doesn't need to be listed here.

        # --- Add all the ImGui source files ---
        "source/imgui/imgui.cpp"
        "source/imgui/imgui_draw.cpp"
        "source/imgui/imgui_tables.cpp"
        "source/imgui/imgui_widgets.cpp"
        "source/imgui/imgui_demo.cpp" # Recommended for development
        "source/imgui/imgui_impl_sdl3.cpp"
        "source/imgui/imgui_impl_sdlrenderer3.cpp"
)

# append windows specific source files
if (WIN32)
    list(APPEND SOURCE_FILES
            # Windows-specific resource file
            "icon.rc"
    )
endif ()


# --- RPATH CONFIGURATION ---
if (UNIX AND NOT APPLE)
    # Making sure it can find the libraries after running .deb or .rpm
    # 1. Set RPATH to look in the ./lib folder relative to the executable
    set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

    # 2. Don't skip RPATH during the build phase
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

# --- 2. CREATE THE EXECUTABLE ---

# Use the WIN32 keyword for graphical apps on Windows to hide the console.
if (WIN32)
    add_executable(${EXECUTABLE_NAME} WIN32 ${SOURCE_FILES})
else ()
    add_executable(${EXECUTABLE_NAME} ${SOURCE_FILES})
endif ()

# This command targets a specific source file and sets its properties ONLY FOR GCC
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
            source/external/miniz.c
            PROPERTIES
            # This adds a compiler flag ONLY when compiling this specific file.
            # -Wno-type-limits will disable the "comparison is always false" warning.
            COMPILE_FLAGS "-Wno-type-limits"
    )
endif ()


# Add a specific rule for tinyfiledialogs.c to ignore the function cast warning.
set_source_files_properties(
        source/external/tinyfiledialogs.c
        PROPERTIES
        # This generator expression applies the flag ONLY when the compiler is NOT MSVC.
        COMPILE_FLAGS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-cast-function-type>"
)

# This command targets a specific source file and sets its properties.
set_source_files_properties(
        source/external/miniz.c
        PROPERTIES
        # This uses a generator expression to apply the correct flags for each compiler.
        # For MSVC, it disables warnings 4132 and 4127.
        # For other compilers (like GCC/Clang), it applies -Wno-type-limits.
        COMPILE_FLAGS "$<$<CXX_COMPILER_ID:MSVC>:/wd4132 /wd4127> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-type-limits>"
)

# Tell the compiler where to find your project's header files.
target_include_directories(Advancely PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
        # Add the path to your vendored libraries like cJSON and dmon
        "${CMAKE_CURRENT_SOURCE_DIR}/source/external"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/imgui"
)


# --- 3. FIND DEPENDENCIES (Cross-Platform) ---
# Use the modern find_package command. This will find system-installed libraries.
# You will need to install these dependencies on your system (see notes below).
find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(SDL3_ttf REQUIRED)
find_package(CURL REQUIRED)

# The official SDL3_mixer is not yet released. If you are using an unofficial build
# or the older SDL2_mixer, you would find it here. For now, it's commented out.
# find_package(SDL2_mixer REQUIRED)


# --- 4. LINK LIBRARIES (Cross-Platform) ---
target_link_libraries(${EXECUTABLE_NAME} PRIVATE
        # Link against the imported targets from find_package
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        CURL::libcurl
)

# Add platform-specific system libraries
if (WIN32)
    # Link against common Windows libraries needed by SDL and advancely
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE gdi32 user32 winmm shell32 version ntdll urlmon)
elseif (APPLE)
    # On macOS, link against required system frameworks
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE "-framework Cocoa" "-framework Metal" "-framework IOKit")

    # This block now creates the .app bundle using your custom Info.plist
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            # Copy the icon file into the bundle's Resources directory
            MACOSX_BUNDLE_ICON_FILE "${CMAKE_SOURCE_DIR}/resources/gui/Advancely_Logo_NoText.icns"
            # Use your custom Info.plist as a template
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
    )

    # Manually ensure the icon is copied to the bundle's Resources directory as a safeguard.
    add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/gui/Advancely_Logo_NoText.icns"
            "$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>/../Resources/Advancely_Logo_NoText.icns"
            COMMENT "[CMAKE] Manually copying icon to macOS app bundle."
    )
endif ()
# On Linux, find_package usually handles all necessary system library dependencies.

# --- 5. POST-BUILD COMMANDS ---

# This block will ONLY run for MinGW/GCC compilers (your local setup)
# and will copy your exact list of required DLLs.
if (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(MINGW_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)
    set(MINGW_DLLS
            "SDL3.dll" "SDL3_image.dll" "SDL3_ttf.dll"
            "libbrotlicommon.dll" "libbrotlidec.dll" "libbz2-1.dll" "libcrypto-3-x64.dll"
            "libcurl-4.dll" "libfreetype-6.dll" "libgcc_s_seh-1.dll" "libglib-2.0-0.dll"
            "libgraphite2.dll" "libharfbuzz-0.dll" "libiconv-2.dll" "libidn2-0.dll"
            "libintl-8.dll" "libnghttp2-14.dll" "libnghttp3-9.dll" "libngtcp2_crypto_ossl.dll"
            "libngtcp2-16.dll" "libpcre2-8-0.dll" "libpng16-16.dll" "libpsl-5.dll"
            "libsharpyuv-0.dll" "libssh2-1.dll" "libssl-3-x64.dll" "libstdc++-6.dll"
            "libunistring-5.dll" "libwebp-7.dll" "libwebpdemux-2.dll" "libwinpthread-1.dll"
            "libzstd.dll" "zlib1.dll"
    )
    foreach (DLL_NAME ${MINGW_DLLS})
        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/${DLL_NAME}"
                "$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>"
        )
    endforeach ()
endif ()

# This command is needed on Windows/Linux platforms to copy the resources to where executable is
# macOS .app bundle is handled further above
if (NOT APPLE)
    add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "${CMAKE_BINARY_DIR}/resources"
            COMMENT "[CMAKE] Copying resources to build directory"
    )
endif ()

if (NOT APPLE)
    # Move the _PLEASE_READ_ME.txt file to the output directory
    add_custom_command(TARGET Advancely POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/_PLEASE_READ_ME.txt"
            "$<TARGET_FILE_DIR:Advancely>"
            COMMENT "[CMAKE] Copying _PLEASE_READ_ME.txt to output directory..."
    )

    add_custom_command(TARGET Advancely POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/LICENSES.txt"
            "$<TARGET_FILE_DIR:Advancely>"
            COMMENT "[CMAKE] Copying LICENSES.txt to output directory..."
    )

    add_custom_command(TARGET Advancely POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/README.md"
            "$<TARGET_FILE_DIR:Advancely>"
            COMMENT "[CMAKE] Copying README.md to output directory..."
    )
endif ()


# --- 6. COMPILER FLAGS & SANITIZERS ---
if (MSVC)
    # Apply MSVC-specific warning flags
    target_compile_options(${EXECUTABLE_NAME} PRIVATE
            /W4     # Equivalent to -Wall / -Wextra, sets a high warning level
            /WX     # Equivalent to -Werror, treats warnings as errors

            # Acceptable warnings
            /wd4244 # Disable "possible loss of data" conversion warning
            /wd4996 # Disable "unsafe function" warnings (for fopen, strncpy, etc.)
            /wd4267 # Disable "size_t to int" conversion warning
            /wd4456 # Disable "hides previous local declaration" warning
            /wd4458 # Disable "hides class member" warning
            /wd4101 # Disable "unreferenced local variable" warning
            /wd4505 # Disable "unreferenced function" warning
            /wd4132 # Disable "'object': const object should be initialized" warning
            /wd4127 # Disable "conditional expression is constant" warning
    )
else ()
    # Apply GCC/Clang-specific warning flags
    target_compile_options(${EXECUTABLE_NAME} PRIVATE
            -Wall                   # Enable all standard warnings
            -Wextra                 # Enable even more warnings
            -Wpedantic              # Warn on non-standard language extensions
            -Wno-deprecated-declarations # Ignore sprintf deprecation error from MacOS
            # -Werror               # Treat all warnings as errors, forcing you to fix them, commented out for MacOS sprintf error
            -Wno-unused-result      # Ignore unused result warnings
            # Disable specific GNU extension warning for Clang
            $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-gnu-zero-variadic-macro-arguments>
    )

    # Add GCC-specific warning suppressions only when using the GCC compiler
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${EXECUTABLE_NAME} PRIVATE
                -Wno-stringop-truncation
                -Wno-format-truncation  # Ignore truncation warnings
        )
    endif ()
endif ()

# Add sanitizers for Debug builds on non-Windows platforms (Linux, macOS)
if (NOT WIN32)
    # This injects code to detect memory errors (like leaks, buffer overflows) at runtime.
    target_compile_options(${EXECUTABLE_NAME} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)

    # Link the AddressSanitizer library for Debug builds.
    target_link_options(${EXECUTABLE_NAME} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
endif ()


# TODO: Remove
## --- AUTO-GENERATE LINUX METADATA (metainfo.xml) ---
#if (UNIX AND NOT APPLE)
#    set(METAINFO_SRC "${CMAKE_SOURCE_DIR}/packaging/linux/dev.LNXSeus.Advancely.metainfo.xml")
#    set(METAINFO_DST "${CMAKE_BINARY_DIR}/dev.LNXSeus.Advancely.metainfo.xml")
#
#    # Read the original file
#    file(READ "${METAINFO_SRC}" METAINFO_CONTENT)
#
#    # 1. Ensure the <releases> tag exists
#    string(FIND "${METAINFO_CONTENT}" "<releases>" RELEASES_INDEX)
#    if(RELEASES_INDEX EQUAL -1)
#        # If missing, add it before the closing </component> tag
#        string(REPLACE "</component>" "  <releases>\n  </releases>\n</component>" METAINFO_CONTENT "${METAINFO_CONTENT}")
#    endif()
#
#    # 2. Check if the CURRENT version is already in the file
#    string(FIND "${METAINFO_CONTENT}" "version=\"${PROJECT_VERSION}\"" VERSION_INDEX)
#
#    # 3. If version is missing, inject it automatically
#    if(VERSION_INDEX EQUAL -1)
#        string(TIMESTAMP CURRENT_DATE "%Y-%m-%d")
#        set(NEW_RELEASE_BLOCK
#                "    <release version=\"${PROJECT_VERSION}\" date=\"${CURRENT_DATE}\">
#      <description>
#        <ul>
#          <li>Update to version ${PROJECT_VERSION}</li>
#        </ul>
#      </description>
#    </release>")
#
#        # Prepend the new block right after <releases> so it appears at the top
#        string(REPLACE "<releases>" "<releases>\n${NEW_RELEASE_BLOCK}" METAINFO_CONTENT "${METAINFO_CONTENT}")
#        message(STATUS "Auto-injecting release info for version ${PROJECT_VERSION} into metainfo.xml")
#    endif()
#
#    # Write the modified content to the BUILD directory (don't touch source file)
#    file(WRITE "${METAINFO_DST}" "${METAINFO_CONTENT}")
#endif()


# --- 7. INSTALLATION AND PACKAGING ---

# This command ensures that CMake's install features are available.
include(InstallRequiredSystemLibraries)

# Define where to install the files
if (WIN32 OR UNIX AND NOT APPLE)
    # Rule to install the main executable and its runtime dlls
    install(TARGETS ${EXECUTABLE_NAME}
            DESTINATION bin
            BUNDLE DESTINATION .
    )

    # Rule to install the entire resources directory
    install(DIRECTORY resources/ DESTINATION share/advancely/resources)

    # Rule to install the text files alongside the executable
    install(FILES
            "${CMAKE_SOURCE_DIR}/_PLEASE_READ_ME.txt"
            "${CMAKE_SOURCE_DIR}/LICENSES.txt"
            "${CMAKE_SOURCE_DIR}/README.md"
            DESTINATION bin
    )
endif ()

# --- LINUX INSTALLATION ---
if (UNIX AND NOT APPLE)
    # 1. Generate the 'version' file automatically
    file(WRITE "${CMAKE_BINARY_DIR}/version" "${PROJECT_VERSION}")

    # 2. Install the ACTUAL binary
    install(TARGETS ${EXECUTABLE_NAME} DESTINATION share/advancely)

    # 3. Install the LAUNCHER script
    install(PROGRAMS packaging/linux/launcher DESTINATION bin RENAME advancely)

    # 4. Install Resources and Version
    install(DIRECTORY resources/ DESTINATION share/advancely/resources)
    install(FILES "${CMAKE_BINARY_DIR}/version" DESTINATION share/advancely)

    # Resolve the real paths, to get the ACTUAL files and install them as the generic name
    file(REAL_PATH "/usr/local/lib/libSDL3.so.0" SDL3_REAL_PATH)
    file(REAL_PATH "/usr/local/lib/libSDL3_image.so.0" SDL3_IMG_REAL_PATH)
    file(REAL_PATH "/usr/local/lib/libSDL3_ttf.so.0" SDL3_TTF_REAL_PATH)

    # Install the REAL files, but RENAME them back to what the app expects (.so.0)
    install(FILES "${SDL3_REAL_PATH}"
            DESTINATION share/advancely/lib
            RENAME "libSDL3.so.0")

    install(FILES "${SDL3_IMG_REAL_PATH}"
            DESTINATION share/advancely/lib
            RENAME "libSDL3_image.so.0")

    install(FILES "${SDL3_TTF_REAL_PATH}"
            DESTINATION share/advancely/lib
            RENAME "libSDL3_ttf.so.0")

    # Desktop Integration
    install(FILES "packaging/linux/advancely.desktop" DESTINATION share/applications)
    install(FILES "packaging/linux/advancely.png"
            DESTINATION share/icons/hicolor/512x512/apps
            RENAME advancely.png
    )
    # Install the auto-generated file from the build directory
    install(FILES "packaging/linux/dev.LNXSeus.Advancely.metainfo.xml" DESTINATION share/metainfo)
endif ()

# --- 8. CPACK PACKAGING (Generates .deb and .rpm) ---
if (UNIX AND NOT APPLE)
    set(CPACK_PACKAGE_NAME "advancely")
    set(CPACK_PACKAGE_VENDOR "LNXSeus")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A highly customizable Minecraft progress tracker.")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_CONTACT "rodney@dfagaming.nl")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSES.txt")

    # Set the package types we want to generate
    set(CPACK_GENERATOR "DEB;RPM")

    # --- DEBIAN (.deb) CONFIG ---
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Rodney van den Velden")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF) # Automatically find dependencies
    # Explicit dependencies if auto-detection fails (adjust names for Debian/Ubuntu)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libcurl4, libfreetype6")

    # --- RPM (.rpm) CONFIG ---
    set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    set(CPACK_RPM_PACKAGE_AUTOREQPROV OFF) # Turn OFF auto-detection

    include(CPack)
endif()
