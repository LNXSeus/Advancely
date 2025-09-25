//
// Created by Linus on 04.07.2025.
//

#ifndef TEMP_CREATOR_UTILS_H
#define TEMP_CREATOR_UTILS_H

#include <cstddef> // For size_t
#include "template_scanner.h" // For the DiscoveredTemplate struct
#include "settings_utils.h" // For VERSION_STRINGS_COUNT

#ifdef __cplusplus
extern "C" {
#endif

// Struct for the stats import feature
struct ImportableStat {
    std::string root_name;
    bool is_selected = false;
};

// Helper structs for the import feature
struct ImportableCriterion {
    std::string root_name;
    bool is_selected = false;
};

struct ImportableAdvancement {
    std::string root_name;
    bool is_done = false;
    bool is_selected = false;
    std::vector<ImportableCriterion> criteria;
};

/**
 * @brief Parses a player's stats file (.json or .dat) into a simple list of stat root names.
 * @param file_path The path to the player's stats file.
 * @param version The MC_Version to determine which parsing format to use.
 * @param out_stats A vector to be populated with the parsed stat names.
 * @param error_message A buffer to store an error message on failure.
 * @param error_msg_size The size of the error_message buffer.
 * @return true on success, false on failure.
 */
bool parse_player_stats_for_import(const char* file_path, MC_Version version, std::vector<ImportableStat>& out_stats, char* error_message, size_t error_msg_size);

/**
 * @brief Parses a player's advancements.json file into a structure suitable for the import UI.
 * @param file_path The path to the player's advancements.json file.
 * @param out_advancements A vector to be populated with the parsed advancements.
 * @param error_message A buffer to store an error message on failure.
 * @param error_msg_size The size of the error_message buffer.
 * @return true on success, false on failure.
 */
bool parse_player_advancements_for_import(const char* file_path, std::vector<ImportableAdvancement>& out_advancements, char* error_message, size_t error_msg_size);

/**
 * @brief Ensures that the directory for a given file path exists, creating it if necessary.
 * This function is cross-platform.
 * @param path The full path to a file, e.g., "resources/templates/1.21/my_cat/1_21_my_cat_flag.json".
 */
void fs_ensure_directory_exists(const char *path);

/**
 * @brief Creates a new, empty template JSON file with a basic skeleton.
 * @param path The full path where the file should be created.
 */
void fs_create_empty_template_file(const char *path);

/**
 * @brief Creates a new, empty language JSON file.
 * @param path The full path where the file should be created.
 */
void fs_create_empty_lang_file(const char *path);

/**
* @brief Validates inputs and creates a new template with its language file.
* This is the main function called by the UI to perform the creation logic.
* It validates the actual filepath and not ambiguous combinations of the category and flag.
* @param version The version string, e.g., "1.16.1".
* @param category The category name, e.g., "all_advancements".
* @param flag The optional flag, e.g., "_pre1.9".
* @param error_message A buffer to store any error message if the function fails.
* @param error_msg_size The size of the error_message buffer.
* @return true on success, false on failure.
*/
bool validate_and_create_template(const char *version, const char *category, const char *flag, char *error_message,
                                  size_t error_msg_size);

/**
* @brief Copies an existing template and its language file to a new destination.
* @param src_version The version of the template to copy.
* @param src_category The category of the template to copy.
* @param src_flag The optional flag of the template to copy.
* @param dest_version The version for the new template.
* @param dest_category The category for the new template.
* @param dest_flag The optional flag for the new template.
* @param error_message A buffer to store any error message.
* @param error_msg_size The size of the error_message buffer.
* @return true on success, false on failure.
*/
bool copy_template_files(const char *src_version, const char *src_category, const char *src_flag,
                         const char *dest_version, const char *dest_category, const char *dest_flag,
                         char *error_message, size_t error_msg_size);

/**
 * @brief Deletes a template and its corresponding language files.
 * @param version The version of the template to delete.
 * @param category The category of the template to delete.
 * @param flag The optional flag of the template to delete.
 * @return true on success, false on failure.
 */
bool delete_template_files(const char *version, const char *category, const char *flag);

/**
 * @brief Enum to represent the result of a language file copy operation.
*/
typedef enum {
    COPY_LANG_FAIL = 0, // The copy operation failed.
    COPY_LANG_SUCCESS_DIRECT, // The copy was successful using the specified source.
    COPY_LANG_SUCCESS_FALLBACK // The copy was successful but used the default language as a fallback.
} CopyLangResult;

/**
* @brief Validates inputs and creates a new, empty language file for an existing template.
* @param version The version string of the parent template.
* @param category The category of the parent template.
* @param flag The optional flag of the parent template.
* @param new_lang_flag The new language flag to create (e.g., "eng", "de"). Cannot be empty.
* @param error_message A buffer to store any error message.
* @param error_msg_size The size of the error_message buffer.
* @return true on success, false on failure.
*/
bool validate_and_create_lang_file(const char *version, const char *category, const char *flag,
                                   const char *new_lang_flag, char *error_message, size_t error_msg_size);

/**
* @brief Copies an existing language file to a new language file for the same template.
* @param version The version string of the parent template.
* @param category The category of the parent template.
* @param flag The optional flag of the parent template.
* @param src_lang_flag The source language flag to copy from (e.g., "" for default, "eng").
* @param dest_lang_flag The new destination language flag (e.g., "de").
* @param error_message A buffer to store any error message.
* @param error_msg_size The size of the error_message buffer.
* @return CopyLangResult enum value indicating the result of the operation.
*/
CopyLangResult copy_lang_file(const char *version, const char *category, const char *flag, const char *src_lang_flag,
                              const char *dest_lang_flag, char *error_message, size_t error_msg_size);

/**
* @brief Deletes a specific, non-default language file for a template.
* @param version The version string of the parent template.
* @param category The category of the parent template.
* @param flag The optional flag of the parent template.
* @param lang_flag_to_delete The language flag to delete (e.g., "eng"). Cannot be the default.
* @param error_message A buffer to store any error message.
* @param error_msg_size The size of the error_message buffer.
* @return true on success, false on failure.
*/
bool delete_lang_file(const char *version, const char *category, const char *flag, const char *lang_flag_to_delete,
                      char *error_message, size_t error_msg_size);

/**
 * @brief Reads a .zip file and attempts to parse template info from its contents.
 * This function does NOT extract any files.
 * @param zip_path Path to the .zip file.
 * @param out_version Buffer to store the parsed version string.
 * @param out_category Buffer to store the parsed category string.
 * @param out_flag Buffer to store the parsed flag string.
 * @param error_message Buffer to store an error message on failure.
 * @param msg_size Size of the error_message buffer.
 * @return true on success, false on failure.
 */
bool get_info_from_zip(const char *zip_path, char *out_version, char *out_category, char *out_flag, char *error_message,
                       size_t msg_size);

/**
 * @brief Performs the final import by extracting a zip file to a specified template location.
 * The import is only performed successfully if the exact template name does not already exist
 * for the selected version.
 * @param zip_path Path to the source .zip file.
 * @param version The user-confirmed version for the new template.
 * @param category The user-confirmed category for the new template.
 * @param flag The user-confirmed flag for the new template.
 * @param error_message Buffer to store an error message on failure.
 * @param msg_size Size of the error_message buffer.
 * @return true on success, false on failure.
 */
bool execute_import_from_zip(const char *zip_path, const char *version, const char *category, const char *flag,
                             char *error_message, size_t msg_size);

/**
 * @brief Opens a save dialog and handles the export of a selected template to a .zip file.
 * @param selected_template The template info to be exported.
 * @param version The version of the selected template.
 * @param status_message A buffer to store the success or error message.
 * @param msg_size The size of the status_message buffer.
 * @return true on success, false on failure or cancellation.
 */
bool handle_export_template(const DiscoveredTemplate &selected_template, const char *version, char *status_message,
                            size_t msg_size);

/**
* @brief Opens the file explorer and highlights the selected language file.
* @param version The version string of the parent template.
* @param category The category of the parent template.
* @param flag The optional flag of the parent template.
* @param lang_flag_to_export The language flag of the file to show.
*/
void handle_export_language(const char *version, const char *category, const char *flag, const char *lang_flag_to_export);


/**
* @brief Imports a user-selected language file for a template.
* @param version The version string of the parent template.
* @param category The category of the parent template.
* @param flag The optional flag of the parent template.
* @param source_path The path to the .json file being imported.
* @param new_lang_flag The new language flag for the destination file.
* @param error_message A buffer to store any error message.
* @param error_msg_size The size of the error_message buffer.
* @return true on success, false on failure.
*/
bool execute_import_language_file(const char *version, const char *category, const char *flag, const char *source_path, const char *new_lang_flag, char *error_message, size_t error_msg_size);

#ifdef __cplusplus
}
#endif

#endif //TEMP_CREATOR_UTILS_H
