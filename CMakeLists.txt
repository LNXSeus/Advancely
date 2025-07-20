# Minimum CMake version required
cmake_minimum_required(VERSION 3.20)

# Project name and language
project(Advancely C)

# Enable the RC (Resource Compiler) language for Windows executables, IMPORTANT FOR EXE ICON, WORKS IN RELEASE MODE
enable_language(RC)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

# --- 1. DEFINE PATHS & SOURCE FILES ---
set(EXECUTABLE_NAME "Advancely")
set(SOURCE_FILES
        "source/main.c"
        "source/init_sdl.c"
        "source/tracker.c"
        "source/overlay.c"
        "source/global_event_handler.c"
        "source/settings.c"
        "source/path_utils.c"
        "source/settings_utils.c"
        "source/file_utils.c"
        "source/temp_create_utils.c"
        # External libraries included as source
        "source/external/cJSON.c"
        # dmon.h is header-only and included in main.c, so it doesn't need to be listed here.
        # Windows-specific resource file
        "icon.rc"
)

# Define the path to your local libraries folder
set(LOCAL_LIBS_PATH "${CMAKE_SOURCE_DIR}/external_libs")
set(LOCAL_INCLUDE_PATH "${LOCAL_LIBS_PATH}/include")
set(LOCAL_LIB_PATH "${LOCAL_LIBS_PATH}/lib")
set(LOCAL_BIN_PATH "${LOCAL_LIBS_PATH}/bin")



# --- 2. CREATE THE EXECUTABLE ---

# Use the WIN32 keyword for graphical apps on Windows to hide the console.
if(WIN32)
    add_executable(${EXECUTABLE_NAME} WIN32 ${SOURCE_FILES})
else()
    add_executable(${EXECUTABLE_NAME} ${SOURCE_FILES})
endif()

# Tell the compiler where to find your project's header files.
target_include_directories(Advancely PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
        # Add the path to your vendored libraries like cJSON and dmon
        "${CMAKE_CURRENT_SOURCE_DIR}/source/external"
)


# The WIN32 keyword is critical for graphical apps on Windows to solve the "WinMain" error.
#add_executable(${EXECUTABLE_NAME} WIN32 ${SOURCE_FILES}) # This creates Advancely.exe
#
## Tell the compiler where to find header files for your project and external libs
#target_include_directories(Advancely PRIVATE
#        "${CMAKE_CURRENT_SOURCE_DIR}/source"
#        "${CMAKE_CURRENT_SOURCE_DIR}/external_libs"  # Points to where cJSON.h is
#)
#
## Give executable an icon
#set_property(TARGET ${EXECUTABLE_NAME} PROPERTY WIN32_EXECUTABLE true)
#
## Set the icon file path
#set_property(TARGET ${EXECUTABLE_NAME} PROPERTY
#        WIN32_EXECUTABLE_ICON_VALUE "${CMAKE_SOURCE_DIR}/resources/gui/Advancely_Icon.ico")




# --- 3. FIND DEPENDENCIES (Cross-Platform) ---
# Use the modern find_package command. This will find system-installed libraries.
# You will need to install these dependencies on your system (see notes below).
find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(SDL3_ttf REQUIRED)

# The official SDL3_mixer is not yet released. If you are using an unofficial build
# or the older SDL2_mixer, you would find it here. For now, it's commented out.
# find_package(SDL2_mixer REQUIRED)


# --- 4. LINK LIBRARIES (Cross-Platform) ---
target_link_libraries(${EXECUTABLE_NAME} PRIVATE
        # Link against the imported targets from find_package
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        # SDL2::mixer # If you were using SDL2_mixer
)

# Add platform-specific system libraries
if(WIN32)
    # Link against common Windows libraries needed by SDL
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE gdi32 user32 winmm shell32 version)
elseif(APPLE)
    # On macOS, link against required system frameworks
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE "-framework Cocoa" "-framework Metal" "-framework IOKit")
endif()
# On Linux, find_package usually handles all necessary system library dependencies.

# --- 5. POST-BUILD COMMANDS ---
if(WIN32)
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
            COMMENT "[CMAKE] Copying DLLs to build directory"
    )
endif()

# This command is needed on ALL platforms to copy your game's resources.
add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "${CMAKE_BINARY_DIR}/resources"
        COMMENT "[CMAKE] Copying resources to build directory"
)



# --- 6. COMPILER FLAGS & SANITIZERS ---
# These flags add strict warnings to help you write better code.
# They apply to all build types (Debug, Release, etc.).
target_compile_options(${EXECUTABLE_NAME} PRIVATE
        -Wall                   # Enable all standard warnings
        -Wextra                 # Enable even more warnings
        -Wpedantic              # Warn on non-standard language extensions
        -Werror                 # Treat all warnings as errors, forcing you to fix them
        -Wno-format-truncation  # Ignore truncation warnings
        -Wno-stringop-truncation # To ignore potentially unsafe string operations
)

# Add sanitizers for Debug builds on non-Windows platforms
if(NOT WIN32)
    # This injects code to detect memory errors (like leaks, buffer overflows) at runtime.
    target_compile_options(${EXECUTABLE_NAME} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)

    # Link the AddressSanitizer library for Debug builds.
    target_link_options(${EXECUTABLE_NAME} PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
endif()