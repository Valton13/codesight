#include "file_filter.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace codesight {

FileFilter::FileFilter() {
    // Initialize monitored file extensions
    monitored_extensions_ = {
        ".py",      // Python
        ".js",      // JavaScript
        ".ts",      // TypeScript
        ".jsx",     // React JSX
        ".tsx",     // React TSX
        ".go",      // Go
        ".cpp",     // C++
        ".c",       // C
        ".h",       // C/C++ header
        ".hpp"      // C++ header
    };

    // Initialize ignored directory names
    ignored_directories_ = {
        "node_modules",
        "__pycache__",
        ".git",
        ".svn",
        ".hg",
        "venv",
        "env",
        "build",
        "dist",
        ".cache",
        ".pytest_cache",
        "target",      // Rust/Java build output
        "bin",         // Compiled binaries
        "obj"          // Object files
    };
}

bool FileFilter::shouldMonitor(const std::string& filepath) const {
    // First check if path should be ignored
    if (shouldIgnore(filepath)) {
        return false;
    }

    // Check if file has a monitored extension
    std::string ext = getExtension(filepath);
    return monitored_extensions_.find(ext) != monitored_extensions_.end();
}

bool FileFilter::shouldIgnore(const std::string& path) const {
    return containsIgnoredDirectory(path);
}

std::string FileFilter::getExtension(const std::string& filepath) const {
    fs::path p(filepath);
    return p.extension().string();
}

bool FileFilter::containsIgnoredDirectory(const std::string& path) const {
    fs::path p(path);

    // Check each component of the path
    for (const auto& component : p) {
        std::string component_str = component.string();
        if (ignored_directories_.find(component_str) != ignored_directories_.end()) {
            return true;
        }
    }

    return false;
}

} // namespace codesight
