// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 04.07.2025.
//

#ifndef TEMP_CREATOR_UTILS_H
#define TEMP_CREATOR_UTILS_H

#include <cstddef> // For size_t
#include "template_scanner.h" // For the DiscoveredTemplate struct
#include "settings_utils.h" // For VERSION_STRINGS_COUNT
#include "file_utils.h" // For cJSON

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

// Struct for the unlocks import feature
struct ImportableUnlock {
    std::string root_name;
    bool is_selected = false;
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
bool parse_player_stats_for_import(const char *file_path, MC_Version version, std::vector<ImportableStat> &out_stats,
                                   char *error_message, size_t error_msg_size);

/**
 * @brief Parses a player's advancements.json file into a structure suitable for the import UI.
 * @param file_path The path to the player's advancements.json or legacy stats.json file.
 * @param version The MC_Version to determine which parsing format to use.
 * @param out_advancements A vector to be populated with the parsed advancements.
 * @param error_message A buffer to store an error message on failure.
 * @param error_msg_size The size of the error_message buffer.
 * @return true on success, false on failure.
 */
bool parse_player_advancements_for_import(const char *file_path, MC_Version version,
                                          std::vector<ImportableAdvancement> &out_advancements, char *error_message,
                                          size_t error_msg_size);

/**
 * @brief Parses a player's unlocks.json file into a simple list of unlock root names from 'obtained' array.
 * @param file_path The path to the player's unlocks.json file.
 * @param out_unlocks A vector to be populated with the parsed unlock names.
 * @param error_message A buffer to store an error message on failure.
 * @param error_msg_size The size of the error_message buffer.
 * @return true on success, false on failure.
 */
bool parse_player_unlocks_for_import(const char *file_path, std::vector<ImportableUnlock> &out_unlocks,
                                     char *error_message, size_t error_msg_size);

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
 * @brief Imports a template from a zip, optionally extracting bundled icon files.
 *
 * @param zip_path       Path to the source zip.
 * @param version        User-confirmed version for the imported template.
 * @param category       User-confirmed category.
 * @param flag           User-confirmed flag.
 * @param import_icons   If true and the zip contains icons/, extract them to resources/icons/
 *                       and relink the icon paths in the extracted template JSON.
 * @param error_message  Output buffer for error text.
 * @param msg_size       Size of error_message.
 * @return true on success.
 */
bool execute_import_from_zip(const char *zip_path, const char *version, const char *category,
                             const char *flag, bool import_icons,
                             char *error_message, size_t msg_size);

/**
 * @brief Exports a template to a zip, optionally bundling icon files.
 *
 * @param selected_template  Template metadata.
 * @param version            Version string.
 * @param include_icons      If true, referenced icon files are copied into the zip under icons/.
 * @param status_message     Output buffer for success/error text.
 * @param msg_size           Size of status_message.
 * @return true on success.
 */
bool handle_export_template(const DiscoveredTemplate &selected_template, const char *version,
                            bool include_icons,
                            char *status_message, size_t msg_size);

/**
* @brief Opens the file explorer and highlights the selected language file.
* @param version The version string of the parent template.
* @param category The category of the parent template.
* @param flag The optional flag of the parent template.
* @param lang_flag_to_export The language flag of the file to show.
*/
void handle_export_language(const char *version, const char *category, const char *flag,
                            const char *lang_flag_to_export);


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
bool execute_import_language_file(const char *version, const char *category, const char *flag, const char *source_path,
                                  const char *new_lang_flag, char *error_message, size_t error_msg_size);

/**
 * @brief Returns indices into import_advs whose root_name is not present in template_root_names.
 * Used by the "Add new advancements" import helper to compute the set difference between an
 * imported player file and the current template.
 * @param template_root_names Root names already in the template (advancement/achievement keys).
 * @param import_advs Parsed advancements from the imported player file.
 * @param include_recipes When false, entries whose root_name contains ":recipes/" are excluded
 *                        from the result (matches the editor's recipe-detection convention).
 * @return Indices into import_advs that represent new (not-in-template) entries.
 */
std::vector<int> compute_new_advancement_indices(
    const std::vector<std::string> &template_root_names,
    const std::vector<ImportableAdvancement> &import_advs,
    bool include_recipes);

/**
 * @brief A single rename candidate: an importable advancement that could be the new name
 * for a template advancement missing from the imported file.
 */
struct RenameCandidate {
    int import_index;           // Index into the importable_advancements vector.
    int criteria_overlap;       // Number of criterion root_names shared between template and import sides.
    int smaller_criteria_size;  // Min of template/import criteria counts on this pair (0 if either side has no criteria).
    bool basename_match;        // True when the substring after the final '/' is identical on both sides.
    bool overlap_match;         // True when accepted via the >=80% criteria-overlap rule.
};

/**
 * @brief A template advancement row in the rename modal, paired with its ranked candidate list.
 */
struct RenameRow {
    int template_index;                       // Index into the template advancement vector supplied by the caller.
    std::vector<RenameCandidate> candidates;  // Sorted by descending criteria similarity, then basename-priority.
};

/**
 * @brief Computes rename suggestions for every template advancement missing from the imported file.
 *
 * Each template advancement whose root_name does not appear in the import is paired with importable
 * advancements (also missing from the template) according to two independent rules:
 *  - If @p match_basename is true, pairs sharing a basename (substring after the final '/') match.
 *  - If @p match_overlap is true, pairs whose shared criterion count is at least 80% of the smaller
 *    side's criterion count match.
 * A candidate is included if either rule fires. Recipe entries on the import side are filtered out
 * unless @p include_recipes is true.
 *
 * Typical wiring: mid-era (1.7-1.11) uses overlap only; modern (1.12+) uses both.
 *
 * @param template_root_names  Root names already in the template (advancement keys).
 * @param template_criteria    Parallel to @p template_root_names: criterion root names per template advancement.
 * @param import_advs          Parsed advancements from the imported player file.
 * @param include_recipes      When false, imports whose root_name contains ":recipes/" are excluded.
 * @param match_basename       Enable basename-based matching.
 * @param match_overlap        Enable >=80% criteria-overlap matching.
 * @return One RenameRow per template advancement that has at least one candidate, in template order.
 */
std::vector<RenameRow> compute_rename_candidates(
    const std::vector<std::string> &template_root_names,
    const std::vector<std::vector<std::string>> &template_criteria,
    const std::vector<ImportableAdvancement> &import_advs,
    bool include_recipes,
    bool match_basename,
    bool match_overlap);

/**
 * @brief A single criterion delta entry: either a new criterion from the import side
 * that doesn't exist in the template, or a stale template criterion not present in the import.
 */
struct CriterionDelta {
    std::string root_name;
    bool is_new;       // true: present in import, missing in template (add candidate).
                       // false: present in template, missing in import (stale candidate).
    bool is_selected;  // Default: false. The user picks what to stage.
};

/**
 * @brief A template advancement row whose criteria differ from the matching import advancement.
 */
struct CriteriaDeltaRow {
    int template_index;                 // Index into the template advancement vector supplied by the caller.
    std::vector<CriterionDelta> deltas; // Mixed list, new entries first then stale.
};

/**
 * @brief Computes per-advancement criterion deltas between the template and the import side.
 *
 * For every template advancement whose root_name is present in @p import_advs, this function
 * lists criteria that exist only on one side. The result contains a row only when at least one
 * delta exists. Recipe entries on the template side are skipped unless @p include_recipes is true.
 *
 * @param template_root_names  Root names already in the template (advancement keys).
 * @param template_criteria    Parallel to @p template_root_names: criterion root names per template advancement.
 * @param import_advs          Parsed advancements from the imported player file.
 * @param include_recipes      When false, template entries whose root_name contains ":recipes/" are skipped.
 * @return One CriteriaDeltaRow per template advancement with at least one differing criterion.
 */
std::vector<CriteriaDeltaRow> compute_criteria_deltas(
    const std::vector<std::string> &template_root_names,
    const std::vector<std::vector<std::string>> &template_criteria,
    const std::vector<ImportableAdvancement> &import_advs,
    bool include_recipes);

/**
 * @brief Scans a template JSON file and collects all unique icon paths referenced in it.
 * Paths are relative to the icons/ directory, e.g. "vanilla/stick.png".
 * @param template_json_path Full path to the template .json file.
 * @param out_icon_paths Vector to be populated with unique relative icon paths.
 * @return true if the file was read successfully (even if no icons found).
 */
bool collect_icon_paths_from_template(const char *template_json_path,
                                      std::vector<std::string> &out_icon_paths);

/**
* @brief Returns true if the zip contains any files under an "icons/" prefix.
* Call after get_info_from_zip to decide whether to show the import-icons checkbox.
* @param zip_path Path to the zip file.
* @return true if at least one icon file is present in the zip.
*/
bool zip_contains_icons(const char *zip_path);

/**
 * @brief Reads the main template JSON inside a zip and returns the parsed cJSON tree.
 * @param zip_path Path to the zip.
 * @param error_message Buffer for an error message on failure.
 * @param msg_size Size of the buffer.
 * @return Parsed cJSON root (caller must cJSON_Delete) or nullptr on failure.
 */
cJSON *read_template_json_from_zip(const char *zip_path, char *error_message, size_t msg_size);

/**
 * @brief Reads a language JSON inside the zip and returns the parsed cJSON tree.
 * Pass an empty/null @p flag to load the default _lang.json. A non-empty flag matches
 * a _lang_<flag>.json file. Returns nullptr when no such file exists (not an error).
 * @param zip_path Path to the zip.
 * @param flag Optional language flag (e.g. "ger"). Empty/null means the default file.
 * @return Parsed cJSON root (caller must cJSON_Delete) or nullptr.
 */
cJSON *read_lang_json_from_zip(const char *zip_path, const char *flag = nullptr);

/**
 * @brief Lists every language flag present in the zip. The default _lang.json (no flag)
 * is reported as an empty string. The returned vector preserves zip-iteration order.
 */
std::vector<std::string> list_lang_flags_in_zip(const char *zip_path);

/**
 * @brief Extracts the named icons from a zip's icons/ directory to resources/icons/.
 * Each path in @p icon_paths is relative to the icons/ root (e.g. "vanilla/stick.png").
 * Files that already exist on disk are skipped. Returns the number of files actually
 * written.
 * @param zip_path Path to the zip.
 * @param icon_paths Relative icon paths to extract.
 * @return Count of icons written to disk.
 */
int extract_zip_icons_by_paths(const char *zip_path, const std::vector<std::string> &icon_paths);

#ifdef __cplusplus
}
#endif

#endif //TEMP_CREATOR_UTILS_H
