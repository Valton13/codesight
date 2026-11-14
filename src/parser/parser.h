#pragma once

#include <string>
#include <vector>
#include <memory>

namespace codesight
{

    struct CppFunctionInfo
    {
        std::string name;
        int line_number;
    };

    struct CppImportInfo
    {
        std::string module;
        std::string alias;
        int line_number;
    };

    struct CppApiRouteInfo
    {
        std::string method;
        std::string path;
        std::string handler;
        int line_number;
    };

    struct CppSymbolInfo
    {
        std::string name;
        std::string kind;
        int line_number;
        int end_line;
        std::string parent;
        bool exported;
        int complexity;
    };

    struct CppCallInfo
    {
        std::string caller;
        std::string callee;
        int line_number;
    };

    struct CppParsedFile
    {
        std::string filepath;
        std::vector<CppFunctionInfo> functions;
        std::vector<CppImportInfo> imports;
        std::vector<CppApiRouteInfo> api_routes;
        std::vector<CppSymbolInfo> symbols;
        std::vector<CppCallInfo> calls;
    };

    class Parser
    {
    public:
        Parser();
        ~Parser();

        CppParsedFile parseFile(const std::string &filepath);

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

} // namespace codesight
