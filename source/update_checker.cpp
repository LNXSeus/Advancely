// Copyright (c) 2025 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 03.09.2025.
//

#include "update_checker.h"
#include "logger.h"
#include <cJSON.h>
#include <string>
#include <curl/curl.h>

#include "main.h" // For MAX_PATH_LENGTH
#include "path_utils.h"
// Platform-specific includes for file operations
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h> // For ShellExecuteA
#include <urlmon.h>   // For URLDownloadToFileA
#else
#include <dirent.h>
#include <unistd.h> // For execv
#include <sys/stat.h>
#endif


// Path to the CA certificate bundle
#define CERT_BUNDLE_PATH "resources/ca_certificates/cacert.pem"

// New helper function to numerically compare version strings (e.g., "v0.9.100")
static int compare_versions(const char *version1, const char *version2) {
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;

    // Parse the version strings, skipping the leading 'v'
    sscanf(version1, "v%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(version2, "v%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 < major2) return -1;
    if (major1 > major2) return 1;

    // Majors are equal, compare minors
    if (minor1 < minor2) return -1;
    if (minor1 > minor2) return 1;

    // Minors are equal, compare patches
    if (patch1 < patch2) return -1;
    if (patch1 > patch2) return 1;

    return 0; // Versions are identical
}

// Helper to recursively delete a directory
void delete_directory_recursively(const char *path) {
#ifdef _WIN32
    SHFILEOPSTRUCTA sf;
    memset(&sf, 0, sizeof(sf));
    sf.wFunc = FO_DELETE;
    sf.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    char path_double_null[MAX_PATH_LENGTH + 1] = {0};
    strncpy(path_double_null, path, MAX_PATH_LENGTH);
    path_double_null[MAX_PATH_LENGTH] = '\0';
    sf.pFrom = path_double_null;
    SHFileOperationA(&sf);
#else
    DIR *dir = opendir(path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char full_path[MAX_PATH_LENGTH];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            struct stat st;
            if (lstat(full_path, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    delete_directory_recursively(full_path);
                } else {
                    unlink(full_path);
                }
            }
        }
    }
    closedir(dir);
    rmdir(path);
#endif
}

// Callback function for libcurl to write received data into a std::string
static size_t write_callback(void *contents, size_t size, size_t nmemb, std::string *s) {
    size_t new_length = size * nmemb;
    try {
        s->append((char *) contents, new_length);
    } catch (std::bad_alloc &e) {
        // Handle memory allocation errors
        return 0;
    }
    return new_length;
}

#ifndef _WIN32 // Only for Linux and macOS
// This is a callback for curl to write downloaded data into a file
static size_t write_file_callback(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}
#endif


bool check_for_updates(const char *current_version, char *out_latest_version, size_t max_len, char *out_download_url,
                       size_t url_max_len, char *out_html_url, size_t html_url_max_len) {
    // This function will now use libcurl for all platforms, as it's only for checking the API, not downloading files.
    // The false positive is triggered by the download action itself.
    CURL *curl;
    CURLcode res;
    std::string read_buffer;
    bool is_new_version_available = false;

    // Clear output buffers initially
    if (out_latest_version && max_len > 0) out_latest_version[0] = '\0';
    if (out_download_url && url_max_len > 0) out_download_url[0] = '\0';
    if (out_html_url && html_url_max_len > 0) out_html_url[0] = '\0';


    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/LNXSeus/Advancely/releases/latest");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "AdvancelyUpdateChecker/1.0");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);

        // Use the bundled certificate file on ALL platforms for consistency.
        curl_easy_setopt(curl, CURLOPT_CAINFO, CERT_BUNDLE_PATH);
        // Also ensure peer verification is enabled.
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            cJSON *json = cJSON_Parse(read_buffer.c_str());
            if (json) {
                const cJSON *tag_name_json = cJSON_GetObjectItem(json, "tag_name");
                if (cJSON_IsString(tag_name_json) && (tag_name_json->valuestring != nullptr)) {
                    strncpy(out_latest_version, tag_name_json->valuestring, max_len - 1);
                    out_latest_version[max_len - 1] = '\0';

                    if (compare_versions(current_version, out_latest_version) < 0) {
                        is_new_version_available = true;

#if defined(_WIN32)
                        const char* os_identifier = "-Windows";
#elif defined(__APPLE__)
                        const char* os_identifier = "-macOS-Universal";
#else
                        const char* os_identifier = "-Linux";
#endif

                        const cJSON *assets = cJSON_GetObjectItem(json, "assets");
                        if (cJSON_IsArray(assets)) {
                            cJSON* asset;
                            cJSON_ArrayForEach(asset, assets) {
                                const cJSON* asset_name_json = cJSON_GetObjectItem(asset, "name");
                                if (cJSON_IsString(asset_name_json) && strstr(asset_name_json->valuestring, os_identifier)) {
                                    const cJSON *download_url_json = cJSON_GetObjectItem(asset, "browser_download_url");
                                    if (cJSON_IsString(download_url_json) && (download_url_json->valuestring != nullptr)) {
                                        strncpy(out_download_url, download_url_json->valuestring, url_max_len - 1);
                                        out_download_url[url_max_len - 1] = '\0';
                                        break;
                                    }
                                }
                            }
                        }

                        const cJSON *html_url_json = cJSON_GetObjectItem(json, "html_url");
                        if (cJSON_IsString(html_url_json) && (html_url_json->valuestring != nullptr)) {
                            strncpy(out_html_url, html_url_json->valuestring, html_url_max_len - 1);
                            out_html_url[html_url_max_len - 1] = '\0';
                        }
                    }
                }
                cJSON_Delete(json);
            } else {
                log_message(LOG_ERROR, "[UPDATE CHECKER] Failed to parse JSON response from GitHub API.\n");
            }
        } else {
            log_message(LOG_ERROR, "[UPDATE CHECKER] curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
    }
    return is_new_version_available;
}

bool download_update_zip(const char *url) {
#ifdef _WIN32
    // --- Windows-specific implementation using native API ---
    log_message(LOG_INFO, "[UPDATE] Downloading update using native Windows API from %s\n", url);
    HRESULT hr = URLDownloadToFileA(nullptr, url, "update.zip", 0, nullptr);
    if (SUCCEEDED(hr)) {
        log_message(LOG_INFO, "[UPDATE] Successfully downloaded update.zip\n");
        return true;
    } else {
        log_message(LOG_ERROR, "[UPDATE] URLDownloadToFileA failed. HRESULT: 0x%lX\n", hr);
        return false;
    }
#else
    // --- macOS and Linux implementation using libcurl ---
    CURL *curl;
    FILE *fp;
    CURLcode res;
    bool success = false;

    curl = curl_easy_init();
    if (curl) {
        fp = fopen("update.zip", "wb");
        if (fp) {
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "AdvancelyUpdateChecker/1.0");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_CAINFO, CERT_BUNDLE_PATH);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                success = true;
                log_message(LOG_INFO, "[UPDATE] Successfully downloaded update from %s\n", url);
            } else {
                log_message(LOG_ERROR, "[UPDATE] curl_easy_perform() failed during download: %s\n",
                            curl_easy_strerror(res));
            }
            fclose(fp);
        } else {
            log_message(LOG_ERROR, "[UPDATE] Failed to open update.zip for writing.\n");
        }
        curl_easy_cleanup(curl);
    }
    return success;
#endif
}

bool apply_update(const char* main_executable_path) {
    const char* temp_dir = "update_temp";
    if (!path_exists(temp_dir)) {
        log_message(LOG_ERROR, "[UPDATE] Update directory '%s' not found.\n", temp_dir);
        return false;
    }

#ifdef _WIN32
    FILE* updater_script = fopen("updater.bat", "w");
    if (!updater_script) {
        log_message(LOG_ERROR, "[UPDATE] Could not create updater.bat script.\n");
        return false;
    }

    const char* exe_filename = strrchr(main_executable_path, '\\');
    if (!exe_filename) exe_filename = strrchr(main_executable_path, '/');
    if (exe_filename) exe_filename++;
    else exe_filename = main_executable_path;

    DWORD pid = GetCurrentProcessId();

    fprintf(updater_script, "@echo off\n");
    fprintf(updater_script, "echo Waiting for Advancely to close...\n");
    fprintf(updater_script, ":wait_loop\n");
    fprintf(updater_script, "tasklist /FI \"PID eq %lu\" 2>NUL | find /I /N \"%lu\">NUL\n", pid, pid);
    fprintf(updater_script, "if \"%%ERRORLEVEL%%\"==\"0\" (timeout /t 1 /nobreak > NUL && goto :wait_loop)\n");

    fprintf(updater_script, "echo Applying update...\n");
    // Safely overwrite root-level files
    fprintf(updater_script, "copy /Y \"%s\\*.exe\" .\\\n", temp_dir);
    fprintf(updater_script, "copy /Y \"%s\\*.dll\" .\\\n", temp_dir);
    fprintf(updater_script, "copy /Y \"%s\\*.txt\" .\\\n", temp_dir);
    fprintf(updater_script, "copy /Y \"%s\\*.md\" .\\\n", temp_dir);

    // Safely merge resource subfolders using robocopy. This will overwrite existing files
    // but will NOT delete user-created files or the config/notes folders.
    fprintf(updater_script, "robocopy \"%s\\resources\\templates\" \".\\resources\\templates\" /E /IS /NFL /NDL\n", temp_dir);
    fprintf(updater_script, "robocopy \"%s\\resources\\fonts\" \".\\resources\\fonts\" /E /IS /NFL /NDL\n", temp_dir);
    fprintf(updater_script, "robocopy \"%s\\resources\\gui\" \".\\resources\\gui\" /E /IS /NFL /NDL\n", temp_dir);
    fprintf(updater_script, "robocopy \"%s\\resources\\reference_files\" \".\\resources\\reference_files\" /E /IS /NFL /NDL\n", temp_dir);
    fprintf(updater_script, "robocopy \"%s\\resources\\icons\" \".\\resources\\icons\" /E /IS /NFL /NDL\n", temp_dir);

    fprintf(updater_script, "echo Cleaning up temporary files...\n");
    fprintf(updater_script, "rmdir /S /Q \"%s\"\n", temp_dir);

    fprintf(updater_script, "echo Relaunching Advancely...\n");
    fprintf(updater_script, "start \"\" \"%s\" --updated\n", exe_filename);

    fprintf(updater_script, "del \"%%~f0\"\n");

    fclose(updater_script);
    ShellExecuteA(nullptr, "open", "updater.bat", nullptr, nullptr, SW_HIDE);

#else
    (void)main_executable_path;
    FILE* updater_script = fopen("updater.sh", "w");
    if (!updater_script) {
        log_message(LOG_ERROR, "[UPDATE] Could not create updater.sh script.\n");
        return false;
    }

    pid_t pid = getpid();

    fprintf(updater_script, "#!/bin/bash\n");
    fprintf(updater_script, "echo \"Waiting for Advancely to close...\"\n");
    fprintf(updater_script, "while ps -p %d > /dev/null; do sleep 1; done\n", pid);

    fprintf(updater_script, "echo \"Applying update...\"\n");

#if defined(__APPLE__)
    // macOS: Replace the .app bundle, but merge the "resources" folder.
    fprintf(updater_script, "rm -rf ./Advancely.app\n");
    fprintf(updater_script, "cp -R ./%s/Advancely.app ./\n", temp_dir);
    fprintf(updater_script, "cp ./%s/*.txt ./\n", temp_dir);
    fprintf(updater_script, "cp ./%s/*.md ./\n", temp_dir);
#else
    // Linux: Overwrite main executable and libraries.
    fprintf(updater_script, "cp ./%s/Advancely ./\n", temp_dir);
    fprintf(updater_script, "cp ./%s/*.so* ./\n", temp_dir);
    fprintf(updater_script, "cp ./%s/*.txt ./\n", temp_dir);
    fprintf(updater_script, "cp ./%s/*.md ./\n", temp_dir);
#endif

    // For both Linux and macOS, safely merge the resource subdirectories using rsync.
    // This overwrites official files but leaves user-created files and the config/notes folders alone.
    fprintf(updater_script, "rsync -av ./%s/resources/fonts/ ./resources/fonts/\n", temp_dir);
    fprintf(updater_script, "rsync -av ./%s/resources/gui/ ./resources/gui/\n", temp_dir);
    fprintf(updater_script, "rsync -av ./%s/resources/icons/ ./resources/icons/\n", temp_dir);
    fprintf(updater_script, "rsync -av ./%s/resources/reference_files/ ./resources/reference_files/\n", temp_dir);
    fprintf(updater_script, "rsync -av ./%s/resources/templates/ ./resources/templates/\n", temp_dir);

    fprintf(updater_script, "echo \"Cleaning up temporary files...\"\n");
    fprintf(updater_script, "rm -rf ./%s\n", temp_dir);

    fprintf(updater_script, "echo \"Relaunching Advancely...\"\n");
#if defined(__APPLE__)
    fprintf(updater_script, "open ./Advancely.app --args --updated &\n");
#else
    fprintf(updater_script, "chmod +x ./Advancely\n");
    fprintf(updater_script, "./Advancely --updated &\n");
#endif

    fprintf(updater_script, "rm -- \"$0\"\n");

    fclose(updater_script);
    chmod("updater.sh", 0755);
    system("./updater.sh &");
#endif

    log_message(LOG_INFO, "[UPDATE] Updater script created. The application will now exit.\n");
    return true;
}


bool application_restart() {
    char main_executable_path[MAX_PATH_LENGTH];
    if (!get_executable_path(main_executable_path, sizeof(main_executable_path))) {
        log_message(LOG_ERROR, "[RESTART] Could not get executable path to restart.\n");
        return false;
    }

#ifdef _WIN32
    FILE* restarter_script = fopen("restarter.bat", "w");
    if (!restarter_script) {
        log_message(LOG_ERROR, "[RESTART] Could not create restarter.bat script.\n");
        return false;
    }

    const char* exe_filename = strrchr(main_executable_path, '\\');
    if (!exe_filename) exe_filename = strrchr(main_executable_path, '/');
    exe_filename = exe_filename ? exe_filename + 1 : main_executable_path;

    DWORD pid = GetCurrentProcessId();

    fprintf(restarter_script, "@echo off\n");
    fprintf(restarter_script, "echo Waiting for Advancely to close...\n");
    fprintf(restarter_script, ":wait_loop\n");
    fprintf(restarter_script, "tasklist /FI \"PID eq %lu\" 2>NUL | find /I /N \"%lu\">NUL\n", pid, pid);
    fprintf(restarter_script, "if \"%%ERRORLEVEL%%\"==\"0\" (timeout /t 1 /nobreak > NUL && goto :wait_loop)\n");
    fprintf(restarter_script, "echo Relaunching Advancely...\n");
    fprintf(restarter_script, "start \"\" \"%s\"\n", exe_filename); // Relaunch without the --updated flag
    fprintf(restarter_script, "del \"%%~f0\"\n"); // Delete self

    fclose(restarter_script);
    ShellExecuteA(nullptr, "open", "restarter.bat", nullptr, nullptr, SW_HIDE);

#else // For macOS and Linux
    FILE* restarter_script = fopen("restarter.sh", "w");
    if (!restarter_script) {
        log_message(LOG_ERROR, "[RESTART] Could not create restarter.sh script.\n");
        return false;
    }

    pid_t pid = getpid();

    fprintf(restarter_script, "#!/bin/bash\n");
    fprintf(restarter_script, "echo \"Waiting for Advancely to close...\"\n");
    fprintf(restarter_script, "while ps -p %d > /dev/null; do sleep 1; done\n", pid);
    fprintf(restarter_script, "echo \"Relaunching Advancely...\"\n");

#if defined(__APPLE__)
    fprintf(restarter_script, "open ./Advancely.app &\n");
#else
    fprintf(restarter_script, "chmod +x ./Advancely\n");
    fprintf(restarter_script, "./Advancely &\n");
#endif

    fprintf(restarter_script, "rm -- \"$0\"\n"); // Delete self

    fclose(restarter_script);
    chmod("restarter.sh", 0755);
    system("./restarter.sh &");
#endif

    log_message(LOG_INFO, "[RESTART] Restart script created. The application will now exit.\n");
    return true;
}
