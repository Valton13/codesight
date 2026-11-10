#include "watcher/watcher.h"
#include "parser/parser.h"
#include "ipc/grpc_client.h"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>

std::atomic<bool> g_running(true);

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

std::unique_ptr<codesight::GrpcClient> grpc_client;

void onFileEvent(const codesight::FileEvent &event)
{
    std::cout << "[" << eventTypeToString(event.type) << "] "
              << event.filepath << "\n";

    if (event.type == codesight::EventType::CREATED ||
        event.type == codesight::EventType::MODIFIED)
    {

        codesight::Parser parser;
        codesight::CppParsedFile parsed = parser.parseFile(event.filepath);

        std::cout << "  Functions: " << parsed.functions.size() << "\n";
        std::cout << "  Imports: " << parsed.imports.size() << "\n";

        if (grpc_client)
        {
            std::vector<std::pair<std::string, int>> functions;
            for (const auto &func : parsed.functions)
            {
                functions.emplace_back(func.name, func.line_number);
            }

            std::vector<std::string> imports;
            for (const auto &imp : parsed.imports)
            {
                imports.push_back(imp.module);
            }

            bool success = grpc_client->sendParsedFile(
                parsed.filepath,
                functions,
                imports);

            if (success)
            {
                std::cout << "  Sent to Python backend\n";
            }
            else
            {
                std::cerr << "  Failed to send to Python backend\n";
            }
        }
        std::cout << "\n";
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <directory>\n";
        return 1;
    }

    std::string directory = argv[1];

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "CodeSight - Live Code Architecture Visualizer\n";
    std::cout << "Watching directory: " << directory << "\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    std::unique_ptr<codesight::FileWatcher> watcher = codesight::createFileWatcher();

    if (!watcher)
    {
        std::cerr << "Failed to create file watcher for this platform.\n";
        return 1;
    }

    // Initialize gRPC client
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
