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
    # Link against common Windows libraries needed by SDL
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE gdi32 user32 winmm shell32 version)
elseif (APPLE)
    # On macOS, link against required system frameworks
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE "-framework Cocoa" "-framework Metal" "-framework IOKit")

    # creating .app bundle
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_ICON_FILE "${CMAKE_SOURCE_DIR}/resources/gui/Advancely_Logo_NoText.icns" # icon f. macOS bundle
    )

    add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>/../Resources" # This path is specific to .app bundles
            COMMENT "[CMAKE] Copying resources into macOS app bundle"
    )
endif ()
# On Linux, find_package usually handles all necessary system library dependencies.

# --- 5. POST-BUILD COMMANDS ---

if (WIN32)
    # This block will only run if the compiler is gcc (MinGW)
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # Get the directory where the MinGW compiler is located
        get_filename_component(MINGW_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)

        # This copies the .dll files to the build folder so the .exe can run.
        # This is only needed if SDL was built as a shared library.
        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3_image::SDL3_image>"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3_ttf::SDL3_ttf>"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                # --- Copy MinGW Runtime DLLs (Add these lines) ---
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libstdc++-6.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libfreetype-6.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libharfbuzz-0.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libintl-8.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libbz2-1.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libiconv-2.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libbrotlidec.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libpng16-16.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libglib-2.0-0.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libgraphite2.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libpcre2-8-0.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libbrotlicommon.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/zlib1.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>


                # Libcurl
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libcurl-4.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libnghttp2-14.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libngtcp2-16.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libidn2-0.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libnghttp3-9.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libngtcp2_crypto_ossl.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libpsl-5.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libssh2-1.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libzstd.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libcrypto-3-x64.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libunistring-5.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libssl-3-x64.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>


                # For 1.11.2 Biomes not displaying
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libwebpdemux-2.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libwebp-7.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libsharpyuv-0.dll"
                $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
                # END of 1.11.2

                COMMENT "[CMAKE] Copying DLLs to build directory"
        )
    endif () # End of MinGW specific post-build commands
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

# TODO: Remove
## Move the _PLEASE_READ_ME.txt file to the output directory
#add_custom_command(TARGET Advancely POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy
#        "${CMAKE_SOURCE_DIR}/_PLEASE_READ_ME.txt"
#        "$<TARGET_FILE_DIR:Advancely>"
#        COMMENT "[CMAKE] Copying _PLEASE_READ_ME.txt to output directory..."
#)
#
#add_custom_command(TARGET Advancely POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy
#        "${CMAKE_SOURCE_DIR}/LICENSES.txt"
#        "$<TARGET_FILE_DIR:Advancely>"
#        COMMENT "[CMAKE] Copying LICENSES.txt to output directory..."
#)
#
#add_custom_command(TARGET Advancely POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy
#        "${CMAKE_SOURCE_DIR}/README.md"
#        "$<TARGET_FILE_DIR:Advancely>"
#        COMMENT "[CMAKE] Copying README.md to output directory..."
#)


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
    )
else ()
    # Apply GCC/Clang-specific warning flags
    target_compile_options(${EXECUTABLE_NAME} PRIVATE
            -Wall                   # Enable all standard warnings
            -Wextra                 # Enable even more warnings
            -Wpedantic              # Warn on non-standard language extensions
            -Werror                 # Treat all warnings as errors, forcing you to fix them
            -Wno-format-truncation  # Ignore truncation warnings
    )

    # Add GCC-specific warning suppressions only when using the GCC compiler
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${EXECUTABLE_NAME} PRIVATE -Wno-stringop-truncation)
    endif ()
endif ()

# Add sanitizers for Debug builds on non-Windows platforms (Linux, macOS)
if (NOT WIN32)
    # This injects code to detect memory errors (like leaks, buffer overflows) at runtime.
    target_compile_options(${EXECUTABLE_NAME} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)

    # Link the AddressSanitizer library for Debug builds.
    target_link_options(${EXECUTABLE_NAME} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
endif ()


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

if (UNIX AND NOT APPLE)
    # Rules for Linux desktop integration
    install(FILES "packaging/linux/advancely.desktop" DESTINATION share/applications)
    install(FILES "resources/gui/Advancely_Logo_NoText.png" DESTINATION share/icons/hicolor/256x256/apps RENAME advancely.png)
    install(FILES "packaging/linux/dev.LNXSeus.Advancely.metainfo.xml" DESTINATION share/metainfo)
endif ()

# --- CPack Configuration for creating a .deb package ---
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "advancely")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}") # version directly from main.h
set(CPACK_PACKAGE_VENDOR "LNXSeus")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "LNXSeus <https://youtube.com/@lnxs>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A highly customizable and interactive tool to track your Minecraft progress beyond just Advancements.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/LNXSeus/Advancely")

# This is important: it lists the runtime libraries the .deb package depends on
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl3-0 (>= 3.0.0), libcurl4 (>= 7.68)")

include(CPack)