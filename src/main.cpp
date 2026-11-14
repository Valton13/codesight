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
#include <fstream>
#include <cstdio>
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

static bool sendParsed(const codesight::CppParsedFile &parsed)
{
    if (!grpc_client)
        return false;
    return grpc_client->sendParsedFile(parsed);
}

void onFileEvent(const codesight::FileEvent &event)
{
    std::cout << "[" << eventTypeToString(event.type) << "] " << event.filepath << "\n";
    if (event.type == codesight::EventType::CREATED ||
        event.type == codesight::EventType::MODIFIED)
    {
        codesight::Parser parser;
        auto parsed = parser.parseFile(event.filepath);
        std::cout << "  Symbols: " << parsed.symbols.size()
                  << "  Imports: " << parsed.imports.size() << "\n";
        std::cout << (sendParsed(parsed) ? "  Sent to Python backend\n"
                                         : "  Failed to send to Python backend\n");
    }
}

static std::string jsonEscape(const std::string &s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            o += "\\\"";
            break;
        case '\\':
            o += "\\\\";
            break;
        case '\n':
            o += "\\n";
            break;
        case '\r':
            o += "\\r";
            break;
        case '\t':
            o += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                o += buf;
            }
            else
            {
                o += c;
            }
        }
    }
    return o;
}

static void writeRecord(std::ofstream &out, const codesight::CppParsedFile &f, bool &first)
{
    if (!first)
        out << ",\n";
    first = false;
    out << "{\"filepath\":\"" << jsonEscape(f.filepath) << "\",\"functions\":[";
    for (size_t i = 0; i < f.functions.size(); ++i)
    {
        if (i)
            out << ",";
        out << "{\"name\":\"" << jsonEscape(f.functions[i].name)
            << "\",\"line_number\":" << f.functions[i].line_number << "}";
    }
    out << "],\"imports\":[";
    for (size_t i = 0; i < f.imports.size(); ++i)
    {
        if (i)
            out << ",";
        out << "{\"module\":\"" << jsonEscape(f.imports[i].module)
            << "\",\"alias\":\"\",\"line_number\":" << f.imports[i].line_number << "}";
    }
    out << "],\"api_routes\":[";
    for (size_t i = 0; i < f.api_routes.size(); ++i)
    {
        if (i)
            out << ",";
        out << "{\"method\":\"" << jsonEscape(f.api_routes[i].method)
            << "\",\"path\":\"" << jsonEscape(f.api_routes[i].path)
            << "\",\"handler\":\"" << jsonEscape(f.api_routes[i].handler)
            << "\",\"line_number\":" << f.api_routes[i].line_number << "}";
    }
    out << "],\"symbols\":[";
    for (size_t i = 0; i < f.symbols.size(); ++i)
    {
        if (i)
            out << ",";
        const auto &s = f.symbols[i];
        out << "{\"name\":\"" << jsonEscape(s.name) << "\",\"kind\":\"" << jsonEscape(s.kind)
            << "\",\"line_number\":" << s.line_number << ",\"end_line\":" << s.end_line
            << ",\"parent\":\"" << jsonEscape(s.parent) << "\",\"exported\":"
            << (s.exported ? "true" : "false") << ",\"complexity\":" << s.complexity << "}";
    }
    out << "],\"calls\":[";
    for (size_t i = 0; i < f.calls.size(); ++i)
    {
        if (i)
            out << ",";
        out << "{\"caller\":\"" << jsonEscape(f.calls[i].caller)
            << "\",\"callee\":\"" << jsonEscape(f.calls[i].callee)
            << "\",\"line_number\":" << f.calls[i].line_number << "}";
    }
    out << "]}";
}

static int runScan(const std::string &directory, int maxFiles)
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
            continue;
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

static int runBench(const std::string &directory, int maxFiles, const std::string &dumpPath)
{
    codesight::FileFilter filter;
    codesight::Parser parser;

    std::ofstream dump;
    bool haveDump = !dumpPath.empty();
    if (haveDump)
    {
        fs::create_directories(fs::path(dumpPath).parent_path());
        dump.open(dumpPath);
        dump << "[\n";
    }
    bool first = true;

    int files = 0;
    long long symbols = 0, imports = 0, routes = 0, calls = 0;
    auto t0 = std::chrono::steady_clock::now();

    std::error_code ec;
    auto it = fs::recursive_directory_iterator(
        directory, fs::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        std::cerr << "[BENCH_ERROR] " << ec.message() << "\n";
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
            continue;

        auto parsed = parser.parseFile(path);
        files++;
        symbols += parsed.symbols.size();
        imports += parsed.imports.size();
        routes += parsed.api_routes.size();
        calls += parsed.calls.size();
        if (haveDump)
            writeRecord(dump, parsed, first);
        if (files >= maxFiles)
            break;
    }

    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (haveDump)
    {
        dump << "\n]\n";
        dump.close();
    }

    std::cout << "[BENCH] files=" << files
              << " ms=" << ms
              << " ms_per_file=" << (files ? ms / files : 0.0)
              << " files_per_sec=" << (ms > 0 ? files * 1000.0 / ms : 0.0)
              << " symbols=" << symbols
              << " imports=" << imports
              << " routes=" << routes
              << " calls=" << calls << "\n";
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc >= 3 && std::string(argv[1]) == "--bench")
    {
        int maxFiles = (argc >= 4) ? std::atoi(argv[3]) : 300;
        if (maxFiles <= 0)
            maxFiles = 300;
        std::string dump = (argc >= 5) ? argv[4] : "";
        return runBench(argv[2], maxFiles, dump);
    }

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
                  << "       " << argv[0] << " --scan <directory> [maxFiles]\n"
                  << "       " << argv[0] << " --bench <directory> [maxFiles] [dump.json]\n";
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
