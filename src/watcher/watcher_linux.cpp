#include "watcher.h"
#include "../utils/file_filter.h"

#include <sys/inotify.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <cerrno>
#include <chrono>
#include <memory>
#include <filesystem>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUFFER_SIZE (1024 * (EVENT_SIZE + 16))

namespace fs = std::filesystem;

namespace codesight
{

    class LinuxFileWatcher : public FileWatcher
    {
    public:
        LinuxFileWatcher() : inotify_fd_(-1), running_(false) {}

        ~LinuxFileWatcher() override
        {
            stopWatching();
        }

        bool startWatching(const std::string &directory, EventCallback callback) override
        {
            if (running_)
            {
                std::cerr << "Watcher is already running\n";
                return false;
            }

            // Initialize inotify
            inotify_fd_ = inotify_init1(IN_NONBLOCK);
            if (inotify_fd_ < 0)
            {
                std::cerr << "Failed to initialize inotify\n";
                return false;
            }

            callback_ = callback;
            root_directory_ = fs::absolute(directory).string();

            // Recursively add watches for all subdirectories
            if (!addWatchesRecursive(root_directory_))
            {
                std::cerr << "Failed to add watches\n";
                close(inotify_fd_);
                inotify_fd_ = -1;
                return false;
            }

            running_ = true;
            monitor_thread_ = std::thread(&LinuxFileWatcher::monitorLoop, this);

            return true;
        }

        void stopWatching() override
        {
            if (!running_)
            {
                return;
            }

            running_ = false;

            // Remove all watches
            for (auto &[wd, path] : watch_descriptors_)
            {
                inotify_rm_watch(inotify_fd_, wd);
            }
            watch_descriptors_.clear();

            // Close inotify fd
            if (inotify_fd_ >= 0)
            {
                close(inotify_fd_);
                inotify_fd_ = -1;
            }

            // Wait for monitor thread to finish
            if (monitor_thread_.joinable())
            {
                monitor_thread_.join();
            }
        }

        bool isWatching() const override
        {
            return running_;
        }

    private:
        int inotify_fd_;
        std::atomic<bool> running_;
        std::thread monitor_thread_;
        EventCallback callback_;
        std::string root_directory_;
        FileFilter filter_;

        // Map watch descriptor -> directory path
        std::unordered_map<int, std::string> watch_descriptors_;

        // Map directory path -> watch descriptor (reverse lookup)
        std::unordered_map<std::string, int> path_to_wd_;

        bool addWatchesRecursive(const std::string &directory)
        {
            // Check if directory should be ignored
            if (filter_.shouldIgnore(directory))
            {
                return true; // Skip but don't fail
            }

            // Add watch for this directory
            int wd = inotify_add_watch(
                inotify_fd_,
                directory.c_str(),
                IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);

            if (wd < 0)
            {
                std::cerr << "Failed to add watch for: " << directory << "\n";
                return false;
            }

            // Store mapping
            watch_descriptors_[wd] = directory;
            path_to_wd_[directory] = wd;

            // Recursively watch subdirectories
            try
            {
                for (const auto &entry : fs::directory_iterator(directory))
                {
                    if (entry.is_directory())
                    {
                        if (!addWatchesRecursive(entry.path().string()))
                        {
                            return false;
                        }
                    }
                }
            }
            catch (const fs::filesystem_error &e)
            {
                std::cerr << "Error iterating directory: " << e.what() << "\n";
                return false;
            }

            return true;
        }

        void monitorLoop()
        {
            char buffer[BUFFER_SIZE] __attribute__((aligned(__alignof__(struct inotify_event))));

            while (running_)
            {
                ssize_t length = read(inotify_fd_, buffer, BUFFER_SIZE);

                if (length < 0)
                {
                    if (errno == EAGAIN)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    else
                    {
                        std::cerr << "Error reading inotify events\n";
                        break;
                    }
                }

                for (char *ptr = buffer; ptr < buffer + length;)
                {
                    const struct inotify_event *event = reinterpret_cast<const struct inotify_event *>(ptr);
                    processEvent(event);
                    ptr += EVENT_SIZE + event->len;
                }
            }
        }

        void processEvent(const struct inotify_event *event)
        {
            // Get the directory that this watch belongs to
            auto it = watch_descriptors_.find(event->wd);
            if (it == watch_descriptors_.end())
            {
                return;
            }

            std::string directory = it->second;
            std::string filename = event->name;
            std::string filepath = directory + "/" + filename;

            // Handle new directories (add watch for them)
            if (event->mask & IN_ISDIR)
            {
                if (event->mask & (IN_CREATE | IN_MOVED_TO))
                {
                    // New directory created, add watch recursively
                    addWatchesRecursive(filepath);
                }
                else if (event->mask & (IN_DELETE | IN_MOVED_FROM))
                {
                    // Directory deleted, remove watch
                    auto path_it = path_to_wd_.find(filepath);
                    if (path_it != path_to_wd_.end())
                    {
                        watch_descriptors_.erase(path_it->second);
                        path_to_wd_.erase(path_it);
                    }
                }
                return;
            }

            // Check if file should be monitored
            if (!filter_.shouldMonitor(filepath))
            {
                return;
            }

            // Determine event type
            EventType type;
            if (event->mask & IN_CREATE)
            {
                type = EventType::CREATED;
            }
            else if (event->mask & IN_MODIFY)
            {
                type = EventType::MODIFIED;
            }
            else if (event->mask & IN_DELETE)
            {
                type = EventType::DELETED;
            }
            else if (event->mask & (IN_MOVED_FROM | IN_MOVED_TO))
            {
                type = EventType::RENAMED;
            }
            else
            {
                return;
            }

            // Call callback
            FileEvent file_event{type, filepath};
            callback_(file_event);
        }
    };

    std::unique_ptr<FileWatcher> createFileWatcher()
    {
        return std::make_unique<LinuxFileWatcher>();
    }

} // namespace codesight
