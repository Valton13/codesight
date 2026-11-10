#pragma once

#include <string>
#include <vector>
#include <unordered_set>

namespace codesight {

/**
 * FileFilter: Determines which files should be monitored
 *
 * Responsibilities:
 * - Check if a file has a monitored extension (.py, .js, etc.)
 * - Check if a path should be ignored (node_modules, .git, etc.)
 */
class FileFilter {
public:
    FileFilter();

    /**
     * Check if a file should be monitored
     * @param filepath Full path to the file
     * @return true if file should be monitored, false otherwise
     */
    bool shouldMonitor(const std::string& filepath) const;

    /**
     * Check if a path should be ignored (directory or file)
     * @param path Path to check
     * @return true if path should be ignored, false otherwise
     */
    bool shouldIgnore(const std::string& path) const;

private:
    // File extensions we care about
    std::unordered_set<std::string> monitored_extensions_;

    // Directory names to ignore
    std::unordered_set<std::string> ignored_directories_;

    /**
     * Extract file extension from path
     * @param filepath Full path to file
     * @return Extension (e.g., ".py") or empty string if none
     */
    std::string getExtension(const std::string& filepath) const;

    /**
     * Check if path contains any ignored directory
     * @param path Path to check
     * @return true if path contains ignored directory
     */
    bool containsIgnoredDirectory(const std::string& path) const;
};

} // namespace codesight
