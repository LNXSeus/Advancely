//
// Created by Linus on 03.09.2025.
//

#include "update_checker.h"
#include "logger.h"
#include <cJSON.h>
#include <curl/curl.h>
#include <string>

#include "main.h" // For MAX_PATH_LENGTH
#include "path_utils.h"
// Platform-specific includes for file operations
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h> // For ShellExecuteA
#else
#include <dirent.h>
#include <unistd.h> // For execv
#include <sys/stat.h>
#endif


// Path to the CA certificate bundle
#define CERT_BUNDLE_PATH "resources/ca_certificates/cacert.pem"

// --- HELPER FUNCTION to recursively delete a directory ---
void delete_directory_recursively(const char* path) {
#ifdef _WIN32
    SHFILEOPSTRUCTA sf;
    memset(&sf, 0, sizeof(sf));
    sf.wFunc = FO_DELETE;
    sf.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    char path_double_null[MAX_PATH_LENGTH + 1] = {0};
    strncpy(path_double_null, path, MAX_PATH_LENGTH);
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

// This is a callback for curl to write downloaded data into a file
static size_t write_file_callback(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}


bool check_for_updates(const char* current_version, char* out_latest_version, size_t max_len, char* out_download_url, size_t url_max_len) {
    CURL *curl;
    CURLcode res;
    std::string read_buffer;
    bool is_new_version_available = false;

    // Clear output buffers initially
    if (out_latest_version && max_len > 0) out_latest_version[0] = '\0';
    if (out_download_url && url_max_len > 0) out_download_url[0] = '\0';


    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/LNXSeus/Advancely/releases/latest");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "AdvancelyUpdateChecker/1.0");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
        curl_easy_setopt(curl, CURLOPT_CAINFO, CERT_BUNDLE_PATH);


        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            cJSON *json = cJSON_Parse(read_buffer.c_str());
            if (json) {
                const cJSON *tag_name_json = cJSON_GetObjectItem(json, "tag_name");
                if (cJSON_IsString(tag_name_json) && (tag_name_json->valuestring != nullptr)) {
                    strncpy(out_latest_version, tag_name_json->valuestring, max_len - 1);
                    out_latest_version[max_len - 1] = '\0';

                    // Compare versions, check if current version is less than the new version (lexicographically)
                    if (strcmp(current_version, out_latest_version) < 0) {
                        is_new_version_available = true;

                        // Find the download URL for the zip asset
                        const cJSON *assets = cJSON_GetObjectItem(json, "assets");
                        if (cJSON_IsArray(assets) && cJSON_GetArraySize(assets) > 0) {
                            cJSON *first_asset = cJSON_GetArrayItem(assets, 0); // Assuming the first asset is the zip
                            const cJSON *download_url_json = cJSON_GetObjectItem(first_asset, "browser_download_url");
                            if (cJSON_IsString(download_url_json) && (download_url_json->valuestring != nullptr)) {
                                strncpy(out_download_url, download_url_json->valuestring, url_max_len - 1);
                                out_download_url[url_max_len - 1] = '\0';
                            }
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

bool download_update_zip(const char* url) {
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
            // Required for GitHub redirects
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                success = true;
                log_message(LOG_INFO, "[UPDATE] Successfully downloaded update from %s\n", url);
            } else {
                log_message(LOG_ERROR, "[UPDATE] curl_easy_perform() failed during download: %s\n", curl_easy_strerror(res));
            }
            fclose(fp);
        } else {
            log_message(LOG_ERROR, "[UPDATE] Failed to open update.zip for writing.\n");
        }
        curl_easy_cleanup(curl);
    }
    return success;
}

bool apply_update(const char* main_executable_path) {
    const char* temp_dir = "update_temp";
    if (!path_exists(temp_dir)) {
        log_message(LOG_ERROR, "[UPDATE] Update directory '%s' not found.\n", temp_dir);
        return false;
    }

#ifdef _WIN32
        // On Windows, create a batch file to perform the update.
    FILE* updater_script = fopen("updater.bat", "w");
    if (!updater_script) {
        log_message(LOG_ERROR, "[UPDATE] Could not create updater.bat script.\n");
        return false;
    }

    const char* exe_filename = strrchr(main_executable_path, '\\');
    if (!exe_filename) {
        exe_filename = strrchr(main_executable_path, '/');
    }
    if (exe_filename) {
        exe_filename++; // Move pointer past the slash
    } else {
        exe_filename = main_executable_path; // Fallback if no slash is found
    }

    DWORD pid = GetCurrentProcessId();

    fprintf(updater_script, "@echo off\n");
    fprintf(updater_script, "echo Waiting for Advancely to close...\n");
    fprintf(updater_script, ":wait_loop\n");
    fprintf(updater_script, "tasklist /FI \"PID eq %lu\" 2>NUL | find /I /N \"%lu\">NUL\n", pid, pid);
    fprintf(updater_script, "if \"%%ERRORLEVEL%%\"==\"0\" (timeout /t 1 /nobreak > NUL && goto :wait_loop)\n");

    fprintf(updater_script, "echo Applying update...\n");
    // Replace the main executable
    fprintf(updater_script, "move /Y \"%s\\%s\" .\\\n", temp_dir, exe_filename);

    // More robust, explicit folder-by-folder copy
    // Ensure destination directories exist before copying.
    fprintf(updater_script, "if not exist \".\\resources\" mkdir \".\\resources\"\n");
    fprintf(updater_script, "if not exist \".\\resources\\templates\" mkdir \".\\resources\\templates\"\n");
    fprintf(updater_script, "if not exist \".\\resources\\fonts\" mkdir \".\\resources\\fonts\"\n");
    fprintf(updater_script, "if not exist \".\\resources\\gui\" mkdir \".\\resources\\gui\"\n");
    fprintf(updater_script, "if not exist \".\\resources\\icons\" mkdir \".\\resources\\icons\"\n");

    // Copy contents of each subfolder, applying exclusions only where needed.
    // /L -> List only, /NFL -> No file list, /NDL -> No dir list -> makes robocopy silent on success
    fprintf(updater_script, "robocopy \"%s\\resources\\templates\" \".\\resources\\templates\" /E /IS /XF *_notes.txt /L /NFL /NDL\n", temp_dir);
    fprintf(updater_script, "robocopy \"%s\\resources\\fonts\" \".\\resources\\fonts\" /E /IS /L /NFL /NDL\n", temp_dir);
    fprintf(updater_script, "robocopy \"%s\\resources\\gui\" \".\\resources\\gui\" /E /IS /L /NFL /NDL\n", temp_dir);
    fprintf(updater_script, "robocopy \"%s\\resources\\icons\" \".\\resources\\icons\" /E /IS /L /NFL /NDL\n", temp_dir);


    // Copy other root files like README, LICENSE etc.
    fprintf(updater_script, "copy /Y \"%s\\*.txt\" .\\\n", temp_dir);
    fprintf(updater_script, "copy /Y \"%s\\*.md\" .\\\n", temp_dir);

    fprintf(updater_script, "echo Cleaning up temporary files...\n");
    fprintf(updater_script, "rmdir /S /Q \"%s\"\n", temp_dir);

    fprintf(updater_script, "echo Relaunching Advancely...\n");
    fprintf(updater_script, "start \"\" \"%s\"\n", main_executable_path);

    // Self-delete the script
    fprintf(updater_script, "del \"%%~f0\"\n");

    fclose(updater_script);

    // Execute the script silently.
    ShellExecuteA(nullptr, "open", "updater.bat", nullptr, nullptr, SW_HIDE);

#else
    // On Linux/macOS, create a shell script.
    FILE* updater_script = fopen("updater.sh", "w");
    if (!updater_script) {
        log_message(LOG_ERROR, "[UPDATE] Could not create updater.sh script.\n");
        return false;
    }

    pid_t pid = getpid();

    fprintf(updater_script, "#!/bin/bash\n");
    fprintf(updater_script, "echo \"Waiting for Advancely to close...\"\n");
    // Wait for the main process to exit
    fprintf(updater_script, "while ps -p %d > /dev/null; do sleep 1; done\n", pid);
    // Copy new files, preserving user data
    fprintf(updater_script, "echo \"Applying update...\"\n");
    // Use rsync to smartly overwrite, excluding user data
    fprintf(updater_script, "rsync -av --exclude 'resources/config/settings.json' --exclude 'resources/templates/*_notes.txt' ./%s/ .\n", temp_dir);
    // Cleanup
    fprintf(updater_script, "echo \"Cleaning up temporary files...\"\n");
    fprintf(updater_script, "rm -rf ./%s\n", temp_dir);
    fprintf(updater_script, "rm update.zip\n");
    // Relaunch
    fprintf(updater_script, "echo \"Relaunching Advancely...\"\n");
    fprintf(updater_script, "./%s &\n", main_executable_path);
    // Remove script
    fprintf(updater_script, "rm -- \"$0\"\n");

    fclose(updater_script);

    // Make the script executable and run it
    chmod("updater.sh", 0755);
    system("./updater.sh &");
#endif

    log_message(LOG_INFO, "[UPDATE] Updater script created. The application will now exit.\n");
    return true; // Signal to the main loop to exit
}
