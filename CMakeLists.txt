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
        # ADD MORE SOURCE FILES HERE
        "icon.rc" # For the EXE ICON
        "source/main.h"
        "source/main.c"
        "source/init_sdl.h"
        "source/init_sdl.c"
        "source/tracker.h"
        "source/tracker.c"
        "source/overlay.h"
        "source/overlay.c"
        "source/global_event_handler.c"
        "source/global_event_handler.h"
)

# Define the path to your local libraries folder
set(LOCAL_LIBS_PATH "${CMAKE_SOURCE_DIR}/external_libs")
set(LOCAL_INCLUDE_PATH "${LOCAL_LIBS_PATH}/include")
set(LOCAL_LIB_PATH "${LOCAL_LIBS_PATH}/lib")
set(LOCAL_BIN_PATH "${LOCAL_LIBS_PATH}/bin")


# --- 2. CREATE THE EXECUTABLE ---
# The WIN32 keyword is critical for graphical apps on Windows to solve the "WinMain" error.
add_executable(${EXECUTABLE_NAME} WIN32 ${SOURCE_FILES})


# Give executable an icon
set_property(TARGET ${EXECUTABLE_NAME} PROPERTY WIN32_EXECUTABLE true)

# Set the icon file path
set_property(TARGET ${EXECUTABLE_NAME} PROPERTY
        WIN32_EXECUTABLE_ICON_VALUE "${CMAKE_SOURCE_DIR}/resources/gui/Advancely_Icon.ico")




# --- 3. FIND LIBRARIES ---
# Find the full path to each required library file (.a) by its name.
# .a files
# The HINTS argument explicitly tells CMake where to look.
# find_library(LIB_SDL3MAIN NAMES libSDL3 libSDL3main SDL3main SDL_main REQUIRED HINTS ${LOCAL_LIB_PATH})
# f1ind_library(LIB_SDL3MAIN NAMES libSDL_main REQUIRED HINTS ${LOCAL_LIB_PATH})
find_library(LIB_SDL3 NAMES SDL3 REQUIRED HINTS ${LOCAL_LIB_PATH})
find_library(LIB_SDL3_IMAGE NAMES SDL3_image REQUIRED HINTS ${LOCAL_LIB_PATH})
find_library(LIB_SDL3_TTF NAMES SDL3_ttf.dll REQUIRED HINTS ${LOCAL_LIB_PATH})

# TODO: Keep this SLD2 for now
find_library(LIB_SDL3_MIXER NAMES SDL2_mixer REQUIRED HINTS ${LOCAL_LIB_PATH})


# --- 4. CONFIGURE THE EXECUTABLE ---

# Add the include path so the compiler can find <SDL3/SDL.h>
target_include_directories(${EXECUTABLE_NAME} PRIVATE
        ${LOCAL_INCLUDE_PATH}
)

# Link all the found libraries and system libraries to our target
target_link_libraries(${EXECUTABLE_NAME} PRIVATE
        # --- Our Found Libraries ---
        # ${LIB_SDL3MAIN}
        ${LIB_SDL3}
        ${LIB_SDL3_IMAGE}
        ${LIB_SDL3_TTF}
        ${LIB_SDL3_MIXER}

        # --- Windows System Libraries ---
        gdi32 user32 winmm imm32 ole32 oleaut32 shell32 version uuid
)


# --- 5. POST-BUILD: COPY DLLS FOR DISTRIBUTION ---
# This copies the .dll files from your libs/bin to the build folder so the .exe can run
add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${LOCAL_BIN_PATH}"
        $<TARGET_FILE_DIR:${EXECUTABLE_NAME}>
        COMMENT "Copying DLLs to build directory"
)

# IMPORTANT NEEDS resources folder
add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "${CMAKE_BINARY_DIR}/resources"
        COMMENT "Copying resources to build directory"
)



# --- 6. COMPILER FLAGS & SANITIZERS ---
# These flags add strict warnings to help you write better code.
# They apply to all build types (Debug, Release, etc.).
target_compile_options(${EXECUTABLE_NAME} PRIVATE
        -Wall             # Enable all standard warnings
        -Wextra           # Enable even more warnings
        -Wpedantic        # Warn on non-standard language extensions
        -Werror           # Treat all warnings as errors, forcing you to fix them
)

# DOESN'T WORK FOR WINDOWS
## Add the AddressSanitizer for Debug builds ONLY.
## This injects code to detect memory errors (like leaks, buffer overflows) at runtime.
#target_compile_options(${EXECUTABLE_NAME} PRIVATE
#        $<$<CONFIG:Debug>:-fsanitize=address>
#)
#
## Link the AddressSanitizer library for Debug builds ONLY.
#target_link_options(${EXECUTABLE_NAME} PRIVATE
#        # For MinGW on Windows, -static-libasan is often required to resolve linker errors.
#        $<$<CONFIG:Debug>:-fsanitize=address -Wl,-Bstatic -lasan -Wl,-Bdynamic>
#)
