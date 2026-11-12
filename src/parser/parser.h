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
        std::string kind; // class | interface | struct | enum | trait | module | function | method | constructor
        int line_number;
        int end_line;
        std::string parent; // enclosing type, empty when top level
        bool exported;
        int complexity;
    };

    struct CppParsedFile
    {
        std::string filepath;
        std::vector<CppFunctionInfo> functions;
        std::vector<CppImportInfo> imports;
        std::vector<CppApiRouteInfo> api_routes;
        std::vector<CppSymbolInfo> symbols;
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
