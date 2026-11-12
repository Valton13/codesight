#include "file_filter.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace codesight
{

    FileFilter::FileFilter()
    {
        monitored_extensions_ = {
            // Python
            ".py",
            // JavaScript / TypeScript
            ".js", ".jsx", ".mjs", ".cjs",
            ".ts", ".tsx", ".mts", ".cts",
            // C / C++
            ".c", ".h",
            ".cpp", ".cc", ".cxx", ".hpp", ".hh", ".hxx",
            // JVM / .NET
            ".java", ".cs",
            // Systems / scripting
            ".go", ".rs", ".rb", ".php",
            // Data assets (shown as nodes, no grammar)
            ".csv", ".json", ".yaml", ".yml", ".toml", ".sql", ".xml"};

        ignored_directories_ = {
            "node_modules", "__pycache__", ".git", ".svn", ".hg",
            "venv", "env", "build", "dist", ".cache", ".pytest_cache",
            "target", "bin", "obj", ".next", ".nuxt", "vendor",
            ".idea", ".vscode", "coverage", ".terraform"};
    }

    bool FileFilter::shouldMonitor(const std::string &filepath) const
    {
        if (shouldIgnore(filepath))
            return false;
        std::string ext = getExtension(filepath);
        return monitored_extensions_.find(ext) != monitored_extensions_.end();
    }

    bool FileFilter::shouldIgnore(const std::string &path) const
    {
        return containsIgnoredDirectory(path);
    }

    std::string FileFilter::getExtension(const std::string &filepath) const
    {
        std::string ext = fs::path(filepath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); });
        return ext;
    }

    bool FileFilter::containsIgnoredDirectory(const std::string &path) const
    {
        fs::path p(path);
        for (const auto &component : p)
        {
            if (ignored_directories_.find(component.string()) != ignored_directories_.end())
            {
                return true;
            }
        }
        return false;
    }

} // namespace codesight
