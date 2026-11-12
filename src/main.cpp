#include "watcher/watcher.h"
#include "parser/parser.h"
#include "ipc/grpc_client.h"
#include "utils/file_filter.h"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <filesystem>
#include <vector>
#include <utility>
#include <cstdlib>

namespace fs = std::filesystem;

std::atomic<bool> g_running(true);
std::unique_ptr<codesight::GrpcClient> grpc_client;

void signalHandler(int signum)
{
    std::cout << "\nInterrupt signal (" << signum << ") received. Shutting down...\n";
    g_running = false;
}

std::string eventTypeToString(codesight::EventType type)
{
    switch (type)
    {
    case codesight::EventType::CREATED:
        return "CREATED";
    case codesight::EventType::MODIFIED:
        return "MODIFIED";
    case codesight::EventType::DELETED:
        return "DELETED";
    case codesight::EventType::RENAMED:
        return "RENAMED";
    default:
        return "UNKNOWN";
    }
}

// Serialize a parsed file into the gRPC request and send it to the Python backend.
static bool sendParsed(const codesight::CppParsedFile &parsed)
{
    if (!grpc_client)
        return false;

    std::vector<std::pair<std::string, int>> functions;
    for (const auto &f : parsed.functions)
        functions.emplace_back(f.name, f.line_number);

    std::vector<std::string> imports;
    for (const auto &i : parsed.imports)
        imports.push_back(i.module);

    return grpc_client->sendParsedFile(parsed.filepath, functions, imports);
}

void onFileEvent(const codesight::FileEvent &event)
{
    std::cout << "[" << eventTypeToString(event.type) << "] " << event.filepath << "\n";

    if (event.type == codesight::EventType::CREATED ||
        event.type == codesight::EventType::MODIFIED)
    {
        codesight::Parser parser;
        codesight::CppParsedFile parsed = parser.parseFile(event.filepath);
        std::cout << "  Functions: " << parsed.functions.size()
                  << "  Imports: " << parsed.imports.size() << "\n";
        std::cout << (sendParsed(parsed) ? "  Sent to Python backend\n"
                                         : "  Failed to send to Python backend\n");
    }
}

// One-shot mode: walk a directory (e.g. a freshly cloned GitHub repo),
// parse every supported file, stream results over gRPC, then exit.
int runScan(const std::string &directory, int maxFiles)
{
    grpc_client = std::make_unique<codesight::GrpcClient>("localhost:50051");
    codesight::FileFilter filter;
    codesight::Parser parser;

    int scanned = 0, sent = 0;
    std::error_code ec;
    auto it = fs::recursive_directory_iterator(
        directory, fs::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        std::cerr << "[SCAN_ERROR] " << ec.message() << "\n";
        return 1;
    }

    for (auto end = fs::recursive_directory_iterator(); it != end; it.increment(ec))
    {
        if (ec)
        {
            ec.clear();
            continue;
        }

        if (it->is_directory())
        {
            // Prune ignored trees (node_modules, .git, ...) instead of walking them.
            if (filter.shouldIgnore(it->path().string()))
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file())
            continue;

        std::string path = it->path().string();
        if (!filter.shouldMonitor(path))
            continue;

        std::error_code sec;
        auto size = it->file_size(sec);
        if (sec || size > 256 * 1024)
            continue; // skip unreadable / huge files

        auto parsed = parser.parseFile(path);
        scanned++;
        if (sendParsed(parsed))
            sent++;
        std::cout << "[SCAN] " << path << std::endl;

        if (scanned >= maxFiles)
        {
            std::cout << "[SCAN_LIMIT] reached " << maxFiles << " files\n";
            break;
        }
    }

    std::cout << "[SCAN_DONE] " << sent << std::endl;
    return 0;
}

int main(int argc, char *argv[])
{
    // Scan mode: codesight --scan <dir> [maxFiles]
    if (argc >= 3 && std::string(argv[1]) == "--scan")
    {
        int maxFiles = (argc >= 4) ? std::atoi(argv[3]) : 300;
        if (maxFiles <= 0)
            maxFiles = 300;
        return runScan(argv[2], maxFiles);
    }

    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <directory>\n"
                  << "       " << argv[0] << " --scan <directory> [maxFiles]\n";
        return 1;
    }

    std::string directory = argv[1];
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "CodeSight - Live Code Architecture Visualizer\n";
    std::cout << "Watching directory: " << directory << "\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    auto watcher = codesight::createFileWatcher();
    if (!watcher)
    {
        std::cerr << "Failed to create file watcher for this platform.\n";
        return 1;
    }

    grpc_client = std::make_unique<codesight::GrpcClient>("localhost:50051");
    std::cout << "Connected to Python backend at localhost:50051\n\n";

    if (!watcher->startWatching(directory, onFileEvent))
    {
        std::cerr << "Failed to start watching directory.\n";
        return 1;
    }

    std::cout << "Watcher started successfully. Monitoring for changes...\n\n";
    while (g_running && watcher->isWatching())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    watcher->stopWatching();
    std::cout << "Watcher stopped. Goodbye.\n";
    return 0;
}
