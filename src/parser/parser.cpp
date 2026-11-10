#include "parser.h"
#include <tree_sitter/api.h>
#include <fstream>
#include <sstream>
#include <iostream>

extern "C"
{
    const TSLanguage *tree_sitter_python();
    const TSLanguage *tree_sitter_javascript();
}

namespace codesight
{

    struct Parser::Impl
    {
        TSParser *parser;

        Impl()
        {
            parser = ts_parser_new();
        }

        ~Impl()
        {
            ts_parser_delete(parser);
        }

        const TSLanguage *getLanguageForFile(const std::string &filepath)
        {
            if (filepath.find(".py") != std::string::npos)
            {
                return tree_sitter_python();
            }
            else if (filepath.find(".js") != std::string::npos ||
                     filepath.find(".jsx") != std::string::npos)
            {
                return tree_sitter_javascript();
            }
            return nullptr;
        }

        std::string readFile(const std::string &filepath)
        {
            std::ifstream file(filepath);
            if (!file.is_open())
            {
                return "";
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        void extractFunctions(TSNode node, const std::string &source,
                              std::vector<CppFunctionInfo> &functions)
        {
            const char *type = ts_node_type(node);

            if (std::string(type) == "function_definition" ||
                std::string(type) == "function_declaration" ||
                std::string(type) == "method_definition")
            {

                uint32_t child_count = ts_node_child_count(node);
                for (uint32_t i = 0; i < child_count; i++)
                {
                    TSNode child = ts_node_child(node, i);
                    const char *child_type = ts_node_type(child);

                    if (std::string(child_type) == "identifier" ||
                        std::string(child_type) == "property_identifier")
                    {

                        uint32_t start = ts_node_start_byte(child);
                        uint32_t end = ts_node_end_byte(child);
                        std::string name = source.substr(start, end - start);

                        uint32_t line = ts_node_start_point(child).row + 1;

                        functions.push_back({name, static_cast<int>(line)});
                        break;
                    }
                }
            }

            uint32_t child_count = ts_node_child_count(node);
            for (uint32_t i = 0; i < child_count; i++)
            {
                extractFunctions(ts_node_child(node, i), source, functions);
            }
        }

        void extractImports(TSNode node, const std::string &source,
                            std::vector<CppImportInfo> &imports)
        {
            const char *type = ts_node_type(node);

            if (std::string(type) == "import_statement" ||
                std::string(type) == "import_from_statement")
            {

                uint32_t start = ts_node_start_byte(node);
                uint32_t end = ts_node_end_byte(node);
                std::string import_text = source.substr(start, end - start);

                uint32_t line = ts_node_start_point(node).row + 1;

                imports.push_back({import_text, "", static_cast<int>(line)});
            }

            uint32_t child_count = ts_node_child_count(node);
            for (uint32_t i = 0; i < child_count; i++)
            {
                extractImports(ts_node_child(node, i), source, imports);
            }
        }
    };

    Parser::Parser() : pimpl_(std::make_unique<Impl>()) {}

    Parser::~Parser() = default;

    CppParsedFile Parser::parseFile(const std::string &filepath)
    {
        CppParsedFile result;
        result.filepath = filepath;

        const TSLanguage *language = pimpl_->getLanguageForFile(filepath);
        if (!language)
        {
            std::cerr << "Unsupported file type: " << filepath << "\n";
            return result;
        }

        std::string source = pimpl_->readFile(filepath);
        if (source.empty())
        {
            std::cerr << "Failed to read file: " << filepath << "\n";
            return result;
        }

        ts_parser_set_language(pimpl_->parser, language);

        TSTree *tree = ts_parser_parse_string(
            pimpl_->parser,
            nullptr,
            source.c_str(),
            source.length());

        if (!tree)
        {
            std::cerr << "Failed to parse file: " << filepath << "\n";
            return result;
        }

        TSNode root_node = ts_tree_root_node(tree);

        pimpl_->extractFunctions(root_node, source, result.functions);
        pimpl_->extractImports(root_node, source, result.imports);

        ts_tree_delete(tree);

        return result;
    }

} // namespace codesight
