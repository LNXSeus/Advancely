//
// Created by Linus on 27.06.2025.
//

#include "path_utils.h" // includes main.h
#include "settings_utils.h"
#include "logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>


// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <cwchar>
#include <tlhelp32.h>
#include <psapi.h>
#include <winternl.h> // For PEB structures
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libproc.h>
#include <sys/sysctl.h>
#endif

// LOCAL HELPER FUNCTIONS
/**
 * @brief Converts DOUBLE backslashes in a path to SINGLE forward slashes.
 * This is a local helper function to ensure consistent path formats internally.
 * @param path The path string to modify in-place.
 */
void normalize_path(char *path) {
    // static because it's only used in this file
    if (path == nullptr) return; // In get_saves_path this function is only called on success (keeping it for safety)
    for (char *p = path; *p; p++) {
        // Convert backslash to forward slash
        if (*p == '\\') {
            *p = '/';
        }
    }
}

/**
 * @brief Automatically detects the default .minecraft saves path (platform-specific).
 * This is a local helper function that contains the OS-specific logic for finding
 * the Minecraft installation directory.
 * @param out_path A buffer to store the resulting path.
 * @param max_len The size of the out_path buffer.
 * @return true if the path was found, false otherwise.
 */
static bool get_auto_saves_path(char *out_path, size_t max_len) {
#ifdef _WIN32
    PWSTR appdata_path_wide = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata_path_wide))) {
        char appdata_path[MAX_PATH];
        wcstombs(appdata_path, appdata_path_wide, MAX_PATH);
        CoTaskMemFree(appdata_path_wide);
        snprintf(out_path, max_len, "%s/.minecraft/saves", appdata_path);
        return true;
    }
#else
    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home_dir = pw->pw_dir;
    }
    if (home_dir) {
#if __APPLE__
        snprintf(out_path, max_len, "%s/Library/Application Support/minecraft/saves", home_dir);
#else
        snprintf(out_path, max_len, "%s/.minecraft/saves", home_dir);
#endif
        return true;
    }
#endif
    return false;
}


// TODO: DEBUGGING
/**
 * @brief Attempts to find a running Minecraft instance from MultiMC/Prism and get its saves path.
 * This is a cross-platform function that inspects running processes for launch arguments.
 * It now prioritizes -Djava.library.path and falls back to --gameDir.
 * @param out_path A buffer to store the resulting path.
 * @param max_len The size of the out_path buffer.
 * @return true if a path was found, false otherwise.
 */
static bool get_active_instance_saves_path(char *out_path, size_t max_len) {
#ifdef _WIN32
    log_message(LOG_ERROR, "[DEBUG] Starting active instance scan for Windows...\n");
    HANDLE h_process_snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h_process_snap == INVALID_HANDLE_VALUE) {
        log_message(LOG_ERROR, "[DEBUG] CreateToolhelp32Snapshot failed.\n");
        return false;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(h_process_snap, &pe32)) {
        CloseHandle(h_process_snap);
        log_message(LOG_ERROR, "[DEBUG] Process32First failed.\n");
        return false;
    }

    bool found = false;
    do {
        if (stricmp(pe32.szExeFile, "javaw.exe") == 0 || stricmp(pe32.szExeFile, "java.exe") == 0) {
            log_message(LOG_ERROR, "[DEBUG] Found javaw.exe/java.exe process with PID: %lu\n", pe32.th32ProcessID);
            HANDLE h_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
            if (h_process == nullptr) {
                log_message(LOG_ERROR, "[DEBUG] OpenProcess failed for PID %lu. GetLastError() = %lu\n", pe32.th32ProcessID, GetLastError());
                continue;
            }

            PROCESS_BASIC_INFORMATION pbi;
            if (NtQueryInformationProcess(h_process, ProcessBasicInformation, &pbi, sizeof(pbi), nullptr) >= 0) {
                PEB peb;
                RTL_USER_PROCESS_PARAMETERS params;
                if (ReadProcessMemory(h_process, pbi.PebBaseAddress, &peb, sizeof(peb), nullptr) &&
                    ReadProcessMemory(h_process, peb.ProcessParameters, &params, sizeof(params), nullptr)) {

                    wchar_t *cmd_line = (wchar_t *)malloc(params.CommandLine.Length + 2);
                    if (cmd_line && ReadProcessMemory(h_process, params.CommandLine.Buffer, cmd_line, params.CommandLine.Length, nullptr)) {
                        cmd_line[params.CommandLine.Length / sizeof(wchar_t)] = L'\0';
                        log_message(LOG_ERROR, "[DEBUG] Reading command line for PID %lu...\n", pe32.th32ProcessID);

                        char instance_path_mbs[MAX_PATH_LENGTH] = {0};

                        wchar_t *lib_path_arg = wcsstr(cmd_line, L"-Djava.library.path=");
                        if (lib_path_arg) {
                            log_message(LOG_ERROR, "[DEBUG] Found '-Djava.library.path=' argument.\n");
                            wchar_t natives_path_w[MAX_PATH_LENGTH] = {0};
                            if (swscanf(lib_path_arg, L"-Djava.library.path=\"%[^\"]\"", natives_path_w) == 1 ||
                                swscanf(lib_path_arg, L"-Djava.library.path=%s", natives_path_w) == 1) {
                                wcstombs(instance_path_mbs, natives_path_w, sizeof(instance_path_mbs));
                                char *last_sep = strrchr(instance_path_mbs, '/');
                                if (!last_sep) last_sep = strrchr(instance_path_mbs, '\\');
                                if (last_sep) *last_sep = '\0';
                                log_message(LOG_ERROR, "[DEBUG] Parsed instance path: %s\n", instance_path_mbs);
                            }
                        }

                        if (instance_path_mbs[0] == '\0') {
                            wchar_t *game_dir_arg = wcsstr(cmd_line, L"--gameDir");
                            if (game_dir_arg) {
                                log_message(LOG_ERROR, "[DEBUG] Found '--gameDir' argument.\n");
                                wchar_t game_dir_path_w[MAX_PATH_LENGTH] = {0};
                                if (swscanf(game_dir_arg, L"--gameDir \"%[^\"]\"", game_dir_path_w) == 1 ||
                                    swscanf(game_dir_arg, L"--gameDir %s", game_dir_path_w) == 1) {
                                    wcstombs(instance_path_mbs, game_dir_path_w, sizeof(instance_path_mbs));
                                    log_message(LOG_ERROR, "[DEBUG] Parsed instance path: %s\n", instance_path_mbs);
                                }
                            }
                        }

                        if (instance_path_mbs[0] != '\0') {
                            char path_candidate[MAX_PATH_LENGTH];
                            snprintf(path_candidate, sizeof(path_candidate), "%s/.minecraft/saves", instance_path_mbs);
                            log_message(LOG_ERROR, "[DEBUG] Checking candidate 1: '%s'\n", path_candidate);
                            if (path_exists(path_candidate)) {
                                strncpy(out_path, path_candidate, max_len - 1);
                                found = true;
                                log_message(LOG_ERROR, "[DEBUG] Success! Found valid saves folder.\n");
                            } else {
                                snprintf(path_candidate, sizeof(path_candidate), "%s/minecraft/saves", instance_path_mbs);
                                log_message(LOG_ERROR, "[DEBUG] Checking candidate 2: '%s'\n", path_candidate);
                                if (path_exists(path_candidate)) {
                                    strncpy(out_path, path_candidate, max_len - 1);
                                    found = true;
                                    log_message(LOG_ERROR, "[DEBUG] Success! Found valid saves folder.\n");
                                }
                            }
                        } else {
                            log_message(LOG_ERROR, "[DEBUG] No suitable launch argument found for this Java process.\n");
                        }
                        free(cmd_line);
                    }
                }
            }
            CloseHandle(h_process);
        }
    } while (Process32Next(h_process_snap, &pe32) && !found);

    CloseHandle(h_process_snap);
    log_message(LOG_ERROR, "[DEBUG] Windows instance scan finished. Found: %s\n", found ? "Yes" : "No");
    return found;

#elif __APPLE__
    log_message(LOG_ERROR, "[DEBUG] Starting active instance scan for macOS...\n");
    pid_t pids[2048];
    int count = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
    if (count <= 0) return false;

    for (int i = 0; i < count; i++) {
        pid_t pid = pids[i];
        if (pid == 0) continue;

        int mib[3] = { CTL_KERN, KERN_PROCARGS2, pid };
        size_t arg_max;
        if (sysctl(mib, 3, nullptr, &arg_max, nullptr, 0) == -1) continue;

        char *arg_buf = (char *)malloc(arg_max);
        if (!arg_buf) continue;

        if (sysctl(mib, 3, arg_buf, &arg_max, nullptr, 0) == -1) {
            free(arg_buf);
            continue;
        }

        char *p = arg_buf + sizeof(int);
        if (!strstr(p, "java")) { // Check if it's a java process
            free(arg_buf);
            continue;
        }
        log_message(LOG_ERROR, "[DEBUG] Found java process with PID: %d\n", pid);

        while (p < arg_buf + arg_max && *p) p++;
        while (p < arg_buf + arg_max && !*p) p++;

        bool found = false;
        char instance_path[MAX_PATH_LENGTH] = {0};
        char *lib_path_str = nullptr;
        char *game_dir_str = nullptr;

        char *current_arg = p;
        while (current_arg < arg_buf + arg_max && *current_arg) {
            if (strncmp(current_arg, "-Djava.library.path=", 20) == 0) {
                lib_path_str = current_arg + 20;
            } else if (strcmp(current_arg, "--gameDir") == 0) {
                game_dir_str = current_arg + strlen(current_arg) + 1;
            }
            current_arg += strlen(current_arg) + 1;
        }

        if (lib_path_str) {
            log_message(LOG_ERROR, "[DEBUG] Found '-Djava.library.path=' argument.\n");
            strncpy(instance_path, lib_path_str, sizeof(instance_path) - 1);
            char *natives_suffix = strstr(instance_path, "/natives");
            if (natives_suffix && *(natives_suffix + strlen("/natives")) == '\0') {
                *natives_suffix = '\0';
            }
            log_message(LOG_ERROR, "[DEBUG] Parsed instance path: %s\n", instance_path);
        } else if (game_dir_str) {
            log_message(LOG_ERROR, "[DEBUG] Found '--gameDir' argument.\n");
            strncpy(instance_path, game_dir_str, sizeof(instance_path) - 1);
            log_message(LOG_ERROR, "[DEBUG] Parsed instance path: %s\n", instance_path);
        }

        if (instance_path[0] != '\0') {
            char path_candidate[MAX_PATH_LENGTH];
            snprintf(path_candidate, sizeof(path_candidate), "%s/.minecraft/saves", instance_path);
            log_message(LOG_ERROR, "[DEBUG] Checking candidate 1: '%s'\n", path_candidate);
            if (path_exists(path_candidate)) {
                strncpy(out_path, path_candidate, max_len - 1);
                found = true;
                log_message(LOG_ERROR, "[DEBUG] Success! Found valid saves folder.\n");
            } else {
                snprintf(path_candidate, sizeof(path_candidate), "%s/minecraft/saves", instance_path);
                log_message(LOG_ERROR, "[DEBUG] Checking candidate 2: '%s'\n", path_candidate);
                if (path_exists(path_candidate)) {
                    strncpy(out_path, path_candidate, max_len - 1);
                    found = true;
                    log_message(LOG_ERROR, "[DEBUG] Success! Found valid saves folder.\n");
                }
            }
        } else {
             log_message(LOG_ERROR, "[DEBUG] No suitable launch argument found for this Java process.\n");
        }

        free(arg_buf);
        if (found) {
            log_message(LOG_ERROR, "[DEBUG] macOS instance scan finished. Found: Yes\n");
            return true;
        }
    }
    log_message(LOG_ERROR, "[DEBUG] macOS instance scan finished. Found: No\n");
    return false;

#else // Linux
    log_message(LOG_ERROR, "[DEBUG] Starting active instance scan for Linux...\n");
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return false;

    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        pid_t pid = atoi(entry->d_name);
        if (pid == 0) continue;

        char cmdline_path[256];
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);

        FILE *f = fopen(cmdline_path, "r");
        if (!f) continue;

        char args[4096] = {0};
        size_t bytes_read = fread(args, 1, sizeof(args) - 1, f);
        fclose(f);

        if (bytes_read == 0 || !strstr(args, "java")) continue;
        log_message(LOG_ERROR, "[DEBUG] Found java process with PID: %d\n", pid);

        char instance_path[MAX_PATH_LENGTH] = {0};
        const char *p = args;

        while (p < args + bytes_read) {
            if (strncmp(p, "-Djava.library.path=", 20) == 0) {
                log_message(LOG_ERROR, "[DEBUG] Found '-Djava.library.path=' argument.\n");
                strncpy(instance_path, p + 20, sizeof(instance_path) - 1);
                char *natives_suffix = strstr(instance_path, "/natives");
                if (natives_suffix && *(natives_suffix + strlen("/natives")) == '\0') {
                    *natives_suffix = '\0';
                }
                log_message(LOG_ERROR, "[DEBUG] Parsed instance path: %s\n", instance_path);
                break;
            }
            p += strlen(p) + 1;
        }

        if (instance_path[0] == '\0') {
            p = args;
            while (p < args + bytes_read) {
                if (strcmp(p, "--gameDir") == 0) {
                    log_message(LOG_ERROR, "[DEBUG] Found '--gameDir' argument.\n");
                    const char *game_dir = p + strlen(p) + 1;
                    if (game_dir < args + bytes_read) {
                        strncpy(instance_path, game_dir, sizeof(instance_path) - 1);
                        log_message(LOG_ERROR, "[DEBUG] Parsed instance path: %s\n", instance_path);
                    }
                    break;
                }
                p += strlen(p) + 1;
            }
        }

        if (instance_path[0] != '\0') {
            char path_candidate[MAX_PATH_LENGTH];
            snprintf(path_candidate, sizeof(path_candidate), "%s/.minecraft/saves", instance_path);
            log_message(LOG_ERROR, "[DEBUG] Checking candidate 1: '%s'\n", path_candidate);
            if (path_exists(path_candidate)) {
                strncpy(out_path, path_candidate, max_len - 1);
                log_message(LOG_ERROR, "[DEBUG] Success! Found valid saves folder.\n");
                closedir(proc_dir);
                return true;
            }
            snprintf(path_candidate, sizeof(path_candidate), "%s/minecraft/saves", instance_path);
            log_message(LOG_ERROR, "[DEBUG] Checking candidate 2: '%s'\n", path_candidate);
            if (path_exists(path_candidate)) {
                strncpy(out_path, path_candidate, max_len - 1);
                log_message(LOG_ERROR, "[DEBUG] Success! Found valid saves folder.\n");
                closedir(proc_dir);
                return true;
            }
        } else {
            log_message(LOG_ERROR, "[DEBUG] No suitable launch argument found for this Java process.\n");
        }
    }
    closedir(proc_dir);
    log_message(LOG_ERROR, "[DEBUG] Linux instance scan finished. Found: No\n");
    return false;
#endif
}

// TODO: WITHOUT DEBUGGING
// /**
//  * @brief Attempts to find a running Minecraft instance from MultiMC/Prism and get its saves path.
//  * This is a cross-platform function that inspects running processes for launch arguments.
//  * It now prioritizes -Djava.library.path and falls back to --gameDir.
//  * @param out_path A buffer to store the resulting path.
//  * @param max_len The size of the out_path buffer.
//  * @return true if a path was found, false otherwise.
//  */
// static bool get_active_instance_saves_path(char *out_path, size_t max_len) {
// #ifdef _WIN32
//     // On Windows, we iterate through processes to find the instance path argument.
//     HANDLE h_process_snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
//     if (h_process_snap == INVALID_HANDLE_VALUE) return false;
//
//     PROCESSENTRY32 pe32;
//     pe32.dwSize = sizeof(PROCESSENTRY32);
//
//     if (!Process32First(h_process_snap, &pe32)) {
//         CloseHandle(h_process_snap);
//         return false;
//     }
//
//     bool found = false;
//     do {
//         if (stricmp(pe32.szExeFile, "javaw.exe") == 0 || stricmp(pe32.szExeFile, "java.exe") == 0) {
//             HANDLE h_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
//             if (h_process == nullptr) continue;
//
//             PROCESS_BASIC_INFORMATION pbi;
//             if (NtQueryInformationProcess(h_process, ProcessBasicInformation, &pbi, sizeof(pbi), nullptr) >= 0) {
//                 PEB peb;
//                 RTL_USER_PROCESS_PARAMETERS params;
//                 if (ReadProcessMemory(h_process, pbi.PebBaseAddress, &peb, sizeof(peb), nullptr) &&
//                     ReadProcessMemory(h_process, peb.ProcessParameters, &params, sizeof(params), nullptr)) {
//
//                     wchar_t *cmd_line = (wchar_t *)malloc(params.CommandLine.Length + 2);
//                     if (cmd_line && ReadProcessMemory(h_process, params.CommandLine.Buffer, cmd_line, params.CommandLine.Length, nullptr)) {
//                         cmd_line[params.CommandLine.Length / sizeof(wchar_t)] = L'\0';
//
//                         char instance_path_mbs[MAX_PATH_LENGTH] = {0};
//
//                         // 1. Prioritize -Djava.library.path
//                         wchar_t *lib_path_arg = wcsstr(cmd_line, L"-Djava.library.path=");
//                         if (lib_path_arg) {
//                             wchar_t natives_path_w[MAX_PATH_LENGTH] = {0};
//                             if (swscanf(lib_path_arg, L"-Djava.library.path=\"%[^\"]\"", natives_path_w) == 1 ||
//                                 swscanf(lib_path_arg, L"-Djava.library.path=%s", natives_path_w) == 1) {
//                                 wcstombs(instance_path_mbs, natives_path_w, sizeof(instance_path_mbs));
//                                 char *last_sep = strrchr(instance_path_mbs, '/');
//                                 if (!last_sep) last_sep = strrchr(instance_path_mbs, '\\');
//                                 if (last_sep) *last_sep = '\0'; // Remove /natives
//                             }
//                         }
//
//                         // 2. Fallback to --gameDir
//                         if (instance_path_mbs[0] == '\0') {
//                             wchar_t *game_dir_arg = wcsstr(cmd_line, L"--gameDir");
//                             if (game_dir_arg) {
//                                 wchar_t game_dir_path_w[MAX_PATH_LENGTH] = {0};
//                                 if (swscanf(game_dir_arg, L"--gameDir \"%[^\"]\"", game_dir_path_w) == 1 ||
//                                     swscanf(game_dir_arg, L"--gameDir %s", game_dir_path_w) == 1) {
//                                     wcstombs(instance_path_mbs, game_dir_path_w, sizeof(instance_path_mbs));
//                                 }
//                             }
//                         }
//
//                         // 3. Check for saves folder if an instance path was found
//                         if (instance_path_mbs[0] != '\0') {
//                             char path_candidate[MAX_PATH_LENGTH];
//                             snprintf(path_candidate, sizeof(path_candidate), "%s/.minecraft/saves", instance_path_mbs);
//                             if (path_exists(path_candidate)) {
//                                 strncpy(out_path, path_candidate, max_len - 1);
//                                 found = true;
//                             } else {
//                                 snprintf(path_candidate, sizeof(path_candidate), "%s/minecraft/saves", instance_path_mbs);
//                                 if (path_exists(path_candidate)) {
//                                     strncpy(out_path, path_candidate, max_len - 1);
//                                     found = true;
//                                 }
//                             }
//                         }
//                         free(cmd_line);
//                     }
//                 }
//             }
//             CloseHandle(h_process);
//         }
//     } while (Process32Next(h_process_snap, &pe32) && !found);
//
//     CloseHandle(h_process_snap);
//     return found;
//
// #elif __APPLE__
//     pid_t pids[2048];
//     int count = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
//     if (count <= 0) return false;
//
//     for (int i = 0; i < count; i++) {
//         pid_t pid = pids[i];
//         if (pid == 0) continue;
//
//         int mib[3] = { CTL_KERN, KERN_PROCARGS2, pid };
//         size_t arg_max;
//         if (sysctl(mib, 3, nullptr, &arg_max, nullptr, 0) == -1) continue;
//
//         char *arg_buf = (char *)malloc(arg_max);
//         if (!arg_buf) continue;
//
//         if (sysctl(mib, 3, arg_buf, &arg_max, nullptr, 0) == -1) {
//             free(arg_buf);
//             continue;
//         }
//
//         char *p = arg_buf + sizeof(int);
//         while (p < arg_buf + arg_max && *p) p++;
//         while (p < arg_buf + arg_max && !*p) p++;
//
//         bool found = false;
//         char instance_path[MAX_PATH_LENGTH] = {0};
//         char *lib_path_str = nullptr;
//         char *game_dir_str = nullptr;
//
//         // Find both arguments first
//         char *current_arg = p;
//         while (current_arg < arg_buf + arg_max && *current_arg) {
//             if (strncmp(current_arg, "-Djava.library.path=", 20) == 0) {
//                 lib_path_str = current_arg + 20;
//             } else if (strcmp(current_arg, "--gameDir") == 0) {
//                 game_dir_str = current_arg + strlen(current_arg) + 1;
//             }
//             current_arg += strlen(current_arg) + 1;
//         }
//
//         // 1. Prioritize -Djava.library.path
//         if (lib_path_str) {
//             strncpy(instance_path, lib_path_str, sizeof(instance_path) - 1);
//             char *natives_suffix = strstr(instance_path, "/natives");
//             if (natives_suffix && *(natives_suffix + strlen("/natives")) == '\0') {
//                 *natives_suffix = '\0';
//             }
//         }
//         // 2. Fallback to --gameDir
//         else if (game_dir_str) {
//             strncpy(instance_path, game_dir_str, sizeof(instance_path) - 1);
//         }
//
//         // 3. Check for saves folder if an instance path was found
//         if (instance_path[0] != '\0') {
//             char path_candidate[MAX_PATH_LENGTH];
//             snprintf(path_candidate, sizeof(path_candidate), "%s/.minecraft/saves", instance_path);
//             if (path_exists(path_candidate)) {
//                 strncpy(out_path, path_candidate, max_len - 1);
//                 found = true;
//             } else {
//                 snprintf(path_candidate, sizeof(path_candidate), "%s/minecraft/saves", instance_path);
//                 if (path_exists(path_candidate)) {
//                     strncpy(out_path, path_candidate, max_len - 1);
//                     found = true;
//                 }
//             }
//         }
//
//         free(arg_buf);
//         if (found) return true;
//     }
//     return false;
//
// #else // Linux
//     DIR *proc_dir = opendir("/proc");
//     if (!proc_dir) return false;
//
//     struct dirent *entry;
//     while ((entry = readdir(proc_dir)) != nullptr) {
//         pid_t pid = atoi(entry->d_name);
//         if (pid == 0) continue;
//
//         char cmdline_path[256];
//         snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);
//
//         FILE *f = fopen(cmdline_path, "r");
//         if (!f) continue;
//
//         char args[4096] = {0};
//         size_t bytes_read = fread(args, 1, sizeof(args) - 1, f);
//         fclose(f);
//
//         if (bytes_read == 0 || !strstr(args, "java")) continue;
//
//         char instance_path[MAX_PATH_LENGTH] = {0};
//
//         const char *p = args;
//         // 1. Prioritize -Djava.library.path
//         while (p < args + bytes_read) {
//             if (strncmp(p, "-Djava.library.path=", 20) == 0) {
//                 strncpy(instance_path, p + 20, sizeof(instance_path) - 1);
//                 char *natives_suffix = strstr(instance_path, "/natives");
//                 if (natives_suffix && *(natives_suffix + strlen("/natives")) == '\0') {
//                     *natives_suffix = '\0';
//                 }
//                 break;
//             }
//             p += strlen(p) + 1;
//         }
//
//         // 2. Fallback to --gameDir
//         if (instance_path[0] == '\0') {
//             p = args;
//             while (p < args + bytes_read) {
//                 if (strcmp(p, "--gameDir") == 0) {
//                     const char *game_dir = p + strlen(p) + 1;
//                     if (game_dir < args + bytes_read) {
//                         strncpy(instance_path, game_dir, sizeof(instance_path) - 1);
//                     }
//                     break;
//                 }
//                 p += strlen(p) + 1;
//             }
//         }
//
//         // 3. Check for saves folder if an instance path was found
//         if (instance_path[0] != '\0') {
//             char path_candidate[MAX_PATH_LENGTH];
//             snprintf(path_candidate, sizeof(path_candidate), "%s/.minecraft/saves", instance_path);
//             if (path_exists(path_candidate)) {
//                 strncpy(out_path, path_candidate, max_len - 1);
//                 closedir(proc_dir);
//                 return true;
//             }
//             snprintf(path_candidate, sizeof(path_candidate), "%s/minecraft/saves", instance_path);
//             if (path_exists(path_candidate)) {
//                 strncpy(out_path, path_candidate, max_len - 1);
//                 closedir(proc_dir);
//                 return true;
//             }
//         }
//     }
//     closedir(proc_dir);
//     return false;
// #endif
// }

// PUBLIC FUNCTIONS

bool get_saves_path(char *out_path, size_t max_len, PathMode mode, const char *manual_path) {
    if (!out_path || max_len == 0) return false;

    bool success = false;
    if (mode == PATH_MODE_AUTO) {
        success = get_auto_saves_path(out_path, max_len);
    } else if (mode == PATH_MODE_MANUAL) {
        if (manual_path && strlen(manual_path) > 0) {
            strncpy(out_path, manual_path, max_len - 1);
            out_path[max_len - 1] = '\0'; // Ensure nullptr-termination
            success = true;
        } else {
            log_message(LOG_ERROR, "[PATH UTILS] Manual path is empty or invalid.\n");
        }
    } else if (mode == PATH_MODE_INSTANCE) {
        success = get_active_instance_saves_path(out_path, max_len);
        if (!success) {
            log_message(LOG_ERROR, "[PATH UTILS] Could not find an active MultiMC/Prism instance.\n");
        }
    }

    // normalize path
    if (success) {
        normalize_path(out_path);
    } else {
        out_path[0] = '\0';
    }

    return success;
}

#ifdef _WIN32
void find_player_data_files(
    const char *saves_path,
    MC_Version version,
    bool use_stats_per_world_mod,
    const AppSettings *settings,
    char *out_world_name,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len) {
    // Clear output paths initially
    out_world_name[0] = '\0';
    out_adv_path[0] = '\0';
    out_stats_path[0] = '\0';
    out_unlocks_path[0] = '\0';

    // Find the most recently modified world directory first, as it's needed in most cases
    char latest_world_name[MAX_PATH_LENGTH] = {0};
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s/*", saves_path);

    WIN32_FIND_DATAA find_world_data;
    HANDLE h_find_world = FindFirstFileA(search_path, &find_world_data);
    if (h_find_world == INVALID_HANDLE_VALUE) {
        log_message(LOG_ERROR, "[PATH UTILS] Cannot find files in saves directory: %s\n", saves_path);
        return;
    }
    FILETIME latest_time = {0, 0};
    do {
        if ((find_world_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(find_world_data.cFileName, ".") != 0
            && strcmp(find_world_data.cFileName, "..") != 0) {
            if (CompareFileTime(&find_world_data.ftLastWriteTime, &latest_time) > 0) {
                latest_time = find_world_data.ftLastWriteTime;
                strncpy(latest_world_name, find_world_data.cFileName, sizeof(latest_world_name) - 1);
                latest_world_name[sizeof(latest_world_name) - 1] = '\0';
            }
        }
    } while (FindNextFileA(h_find_world, &find_world_data) != 0);
    FindClose(h_find_world);

    if (strlen(latest_world_name) > 0) {
        strncpy(out_world_name, latest_world_name, max_len - 1);
        out_world_name[max_len - 1] = '\0';
        if (settings) {
            log_message(LOG_INFO, "[PATH UTILS] Found latest world: %s\n", out_world_name);
        }
    } else {
        strncpy(out_world_name, "No Worlds Found", max_len - 1);
        out_world_name[max_len - 1] = '\0';
    }

    // --- Determine paths based on version and mod flag ---
    if (version <= MC_VERSION_1_6_4) {
        if (use_stats_per_world_mod) {
            // LEGACY WITH MOD: Look for .dat file inside the latest world's stats folder
            if (strlen(latest_world_name) > 0) {
                char stats_search_path[MAX_PATH_LENGTH];
                snprintf(stats_search_path, sizeof(stats_search_path), "%s/%s/stats/*.dat", saves_path,
                         latest_world_name);

                WIN32_FIND_DATAA find_file_data;
                HANDLE h_find_file = FindFirstFileA(stats_search_path, &find_file_data);
                if (h_find_file != INVALID_HANDLE_VALUE) {
                    snprintf(out_stats_path, max_len, "%s/%s/stats/%s", saves_path, latest_world_name,
                             find_file_data.cFileName);
                    if (settings) {
                        log_message(LOG_INFO, "[PATH UTILS] Found legacy per-world stats file: %s\n", out_stats_path);
                    }
                    FindClose(h_find_file);
                }
            }
        } else {
            // STANDARD LEGACY: Look for global .dat file
            char mc_root_path[MAX_PATH_LENGTH];
            strncpy(mc_root_path, saves_path, sizeof(mc_root_path) - 1);
            mc_root_path[sizeof(mc_root_path) - 1] = '\0';
            char *last_slash = strrchr(mc_root_path, '/');
            if (last_slash) *last_slash = '\0';

            char stats_search_path[MAX_PATH_LENGTH];
            snprintf(stats_search_path, sizeof(stats_search_path), "%s/stats/*.dat", mc_root_path);

            WIN32_FIND_DATAA find_file_data;
            HANDLE h_find_file = FindFirstFileA(stats_search_path, &find_file_data);
            if (h_find_file != INVALID_HANDLE_VALUE) {
                snprintf(out_stats_path, max_len, "%s/stats/%s", mc_root_path, find_file_data.cFileName);
                if (settings) {
                    log_message(LOG_INFO, "[PATH UTILS] Found legacy global stats file: %s\n", out_stats_path);
                }
                FindClose(h_find_file);
            }
        }
    } else {
        // MODERN & MID-ERA: Stats and advancements are per-world .json files
        if (strlen(latest_world_name) == 0) return; // Cannot proceed without a world

        char temp_path[MAX_PATH_LENGTH];
        char sub_search_path[MAX_PATH_LENGTH];
        WIN32_FIND_DATAA find_file_data;
        HANDLE h_find_file;

        // Find stats file
        snprintf(temp_path, max_len, "%s/%s/stats", saves_path, latest_world_name);
        snprintf(sub_search_path, sizeof(sub_search_path), "%s/*.json", temp_path);
        h_find_file = FindFirstFileA(sub_search_path, &find_file_data);
        if (h_find_file != INVALID_HANDLE_VALUE) {
            snprintf(out_stats_path, max_len, "%s/%s", temp_path, find_file_data.cFileName);
            if (settings) {
                log_message(LOG_INFO, "[PATH UTILS] Found mid/modern era stats file: %s\n", out_stats_path);
            }
            FindClose(h_find_file);
        }

        // Find advancements/unlocks for modern eras
        if (version >= MC_VERSION_1_12) {
            snprintf(temp_path, max_len, "%s/%s/advancements", saves_path, latest_world_name);
            snprintf(sub_search_path, sizeof(sub_search_path), "%s/*.json", temp_path);
            h_find_file = FindFirstFileA(sub_search_path, &find_file_data);
            if (h_find_file != INVALID_HANDLE_VALUE) {
                snprintf(out_adv_path, max_len, "%s/%s", temp_path, find_file_data.cFileName);
                FindClose(h_find_file);
            }

            if (version == MC_VERSION_25W14CRAFTMINE) {
                snprintf(temp_path, max_len, "%s/%s/unlocks", saves_path, latest_world_name);
                snprintf(sub_search_path, sizeof(sub_search_path), "%s/*.json", temp_path);
                h_find_file = FindFirstFileA(sub_search_path, &find_file_data);
                if (h_find_file != INVALID_HANDLE_VALUE) {
                    snprintf(out_unlocks_path, max_len, "%s/%s", temp_path, find_file_data.cFileName);
                    FindClose(h_find_file);
                }
            }
        }
    }
    // Normalize all found paths
    normalize_path(out_stats_path);
    normalize_path(out_adv_path);
    normalize_path(out_unlocks_path);
}
#else // LINUX/MAC
void find_player_data_files(
    const char *saves_path,
    MC_Version version,
    bool use_stats_per_world_mod,
    const AppSettings *settings,
    char *out_world_name,
    char *out_adv_path,
    char *out_stats_path,
    char *out_unlocks_path,
    size_t max_len
) {
    // Clear output paths initially
    out_world_name[0] = '\0';
    out_adv_path[0] = '\0';
    out_stats_path[0] = '\0';
    out_unlocks_path[0] = '\0';

    // Find the most recently modified world directory first
    char latest_world_name[MAX_PATH_LENGTH] = {0};
    DIR *saves_dir = opendir(saves_path);
    if (!saves_dir) {
        log_message(LOG_ERROR,"[PATH UTILS] Cannot open saves directory: %s\n", saves_path);
        return;
    }
    struct dirent *entry;
    time_t latest_time = 0;
    while ((entry = readdir(saves_dir)) != nullptr) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char world_path[MAX_PATH_LENGTH];
            snprintf(world_path, sizeof(world_path), "%s/%s", saves_path, entry->d_name);
            struct stat stat_buf;
            if (stat(world_path, &stat_buf) == 0) {
                if (stat_buf.st_mtime > latest_time) {
                    latest_time = stat_buf.st_mtime;
                    strncpy(latest_world_name, entry->d_name, sizeof(latest_world_name) - 1);
                    latest_world_name[sizeof(latest_world_name) - 1] = '\0';
                }
            }
        }
    }
    closedir(saves_dir);

    if (strlen(latest_world_name) > 0) {
        strncpy(out_world_name, latest_world_name, max_len - 1);
        out_world_name[max_len - 1] = '\0';
        if (settings) {
            log_message(LOG_INFO,"[PATH UTILS] Found latest world: %s\n", out_world_name);
        }
    } else {
        strncpy(out_world_name, "No Worlds Found", max_len - 1);
        out_world_name[max_len - 1] = '\0';
    }

    // --- Determine paths based on version and mod flag ---
    if (version <= MC_VERSION_1_6_4) {
        if (use_stats_per_world_mod) {
            // LEGACY WITH MOD: Look for .dat file inside the latest world's stats folder
            if (strlen(latest_world_name) > 0) {
                char stats_path_dir[MAX_PATH_LENGTH];
                snprintf(stats_path_dir, sizeof(stats_path_dir), "%s/%s/stats", saves_path, latest_world_name);
                DIR *stats_dir = opendir(stats_path_dir);
                if (stats_dir) {
                    while ((entry = readdir(stats_dir)) != nullptr) {
                        if (strstr(entry->d_name, ".dat")) {
                            snprintf(out_stats_path, max_len, "%s/%s", stats_path_dir, entry->d_name);
                            if (settings) {
                                log_message(LOG_INFO,"[PATH UTILS] Found legacy per-world stats file: %s\n", out_stats_path);
                            }
                            break;
                        }
                    }
                    closedir(stats_dir);
                }
            }
        } else {
            // STANDARD LEGACY: Look for global .dat file
            char mc_root_path[MAX_PATH_LENGTH];
            strncpy(mc_root_path, saves_path, sizeof(mc_root_path) - 1);
            mc_root_path[sizeof(mc_root_path) - 1] = '\0';
            char *last_slash = strrchr(mc_root_path, '/');
            if (last_slash) *last_slash = '\0';

            char stats_path_dir[MAX_PATH_LENGTH];
            snprintf(stats_path_dir, sizeof(stats_path_dir), "%s/stats", mc_root_path);
            DIR *stats_dir = opendir(stats_path_dir);
            if (stats_dir) {
                while ((entry = readdir(stats_dir)) != nullptr) {
                    if (strstr(entry->d_name, ".dat")) {
                        snprintf(out_stats_path, max_len, "%s/%s", stats_path_dir, entry->d_name);
                        if (settings) {
                            log_message(LOG_INFO,"[PATH UTILS] Found legacy global stats file: %s\n", out_stats_path);
                        }
                        break;
                    }
                }
                closedir(stats_dir);
            }
        }
    } else {
        // MODERN & MID-ERA: Stats and advancements are per-world .json files
        if (strlen(latest_world_name) == 0) return;

        char folder_path[MAX_PATH_LENGTH];
        DIR *dir;

        // Find stats file
        snprintf(folder_path, sizeof(folder_path), "%s/%s/stats", saves_path, latest_world_name);
        dir = opendir(folder_path);
        if (dir) {
            while ((entry = readdir(dir)) != nullptr) {
                if (strstr(entry->d_name, ".json")) {
                    snprintf(out_stats_path, max_len, "%s/%s", folder_path, entry->d_name);
                    if (settings) {
                        log_message(LOG_INFO,"[PATH UTILS] Found mid/modern era stats file: %s\n", out_stats_path);
                    }
                    break;
                }
            }
            closedir(dir);
        }

        // Find advancements/unlocks for modern era
        if (version >= MC_VERSION_1_12) {
            snprintf(folder_path, sizeof(folder_path), "%s/%s/advancements", saves_path, latest_world_name);
            dir = opendir(folder_path);
            if (dir) {
                while ((entry = readdir(dir)) != nullptr) {
                    if (strstr(entry->d_name, ".json")) {
                        snprintf(out_adv_path, max_len, "%s/%s", folder_path, entry->d_name);
                        break;
                    }
                }
                closedir(dir);
            }
            if (version == MC_VERSION_25W14CRAFTMINE) {
                snprintf(folder_path, sizeof(folder_path), "%s/%s/unlocks", saves_path, latest_world_name);
                dir = opendir(folder_path);
                if (dir) {
                    while ((entry = readdir(dir)) != nullptr) {
                        if (strstr(entry->d_name, ".json")) {
                            snprintf(out_unlocks_path, max_len, "%s/%s", folder_path, entry->d_name);
                            break;
                        }
                    }
                    closedir(dir);
                }
            }
        }
    }
}
#endif

bool path_exists(const char *path) {
#ifdef _WIN32
    DWORD file_attributes = GetFileAttributesA(path);
    return (file_attributes != INVALID_FILE_ATTRIBUTES);
#else
    struct stat buffer;
    return (stat(path, &buffer) == 0);
#endif
}


bool get_parent_directory(const char *original_path, char *out_path, size_t max_len, int levels) {
    if (!original_path || !out_path || max_len == 0 || levels <= 0) {
        return false;
    }

    // DEBUG: Log the initial state
    log_message(LOG_INFO, "[PATH UTILS DEBUG get_parent_directory] Start: path='%s', levels=%d\n", original_path,
                levels);

    // Copy the original path to the output buffer to work with it.
    strncpy(out_path, original_path, max_len - 1);
    out_path[max_len - 1] = '\0';

    for (int i = 0; i < levels; ++i) {
        size_t len = strlen(out_path);

        // If the path ends with a slash, remove it first to ensure we find the correct parent.
        if (len > 0 && (out_path[len - 1] == '/' || out_path[len - 1] == '\\')) {
            out_path[len - 1] = '\0';
        }

        // Find the last directory separator.
        char *last_slash = strrchr(out_path, '/');
        char *last_backslash = strrchr(out_path, '\\');
        char *separator = (last_slash > last_backslash) ? last_slash : last_backslash;

        if (separator) {
            // Truncate the string at the separator to get the parent directory.
            *separator = '\0';

            // DEBUG: Log the result of this step
            log_message(LOG_INFO, "[PATH UTILS DEBUG get_parent_directory] After level %d: path='%s'\n", i + 1,
                        out_path);

            // If the truncation resulted in an empty string (e.g., from "/saves"), it's an error.
            if (out_path[0] == '\0') {
                log_message(LOG_ERROR, "[PATH UTILS DEBUG get_parent_directory] Path became empty. Failing.\n");
                return false;
            }
        } else {
            // No more separators found, cannot go up further.
            log_message(LOG_ERROR, "[PATH UTILS DEBUG get_parent_directory] No separator found at level %d. Failing.\n",
                        i + 1);
            return false;
        }
    }

    log_message(LOG_INFO, "[PATH UTILS DEBUG get_parent_directory] Success. Final path: '%s'\n", out_path);
    return true;
}

void path_to_windows_native(char *path) {
    if (!path) return;
    for (char *p = path; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
}

// This cross-platform function gets the full path of the currently running executable.
bool get_executable_path(char *out_path, size_t max_len) {
#ifdef _WIN32
    DWORD result = GetModuleFileNameA(nullptr, out_path, (DWORD) max_len);
    if (result == 0 || result >= max_len) {
        return false;
    }
    return true;
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)max_len;
    if (_NSGetExecutablePath(out_path, &size) != 0) {
        // Buffer was too small.
        return false;
    }
    return true;
#else // For Linux
    ssize_t len = readlink("/proc/self/exe", out_path, max_len - 1);
    if (len != -1) {
        out_path[len] = '\0';
        return true;
    }
    return false;
#endif
}
