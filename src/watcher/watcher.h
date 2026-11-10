#pragma once

#include <string>
#include <functional>
#include <memory>

namespace codesight {

enum class EventType {
    CREATED,
    MODIFIED,
    DELETED,
    RENAMED
};

struct FileEvent {
    EventType type;
    std::string filepath;
};

using EventCallback = std::function<void(const FileEvent&)>;

/*
    FileWatcher is an abstract interface.

    main.cpp only knows this interface.
    Platform-specific classes like LinuxFileWatcher stay hidden inside .cpp files.
*/
class FileWatcher {
public:
    virtual ~FileWatcher() = default;

    virtual bool startWatching(const std::string& directory, EventCallback callback) = 0;
    virtual void stopWatching() = 0;
    virtual bool isWatching() const = 0;
};

/*
    Factory function.

    On Linux, this will return a LinuxFileWatcher.
    Later, on Windows/macOS, this can return a different implementation.
*/
std::unique_ptr<FileWatcher> createFileWatcher();

} // namespace codesight
