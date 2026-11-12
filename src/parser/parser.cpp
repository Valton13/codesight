#include "parser.h"
#include <tree_sitter/api.h>

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

extern "C"
{
    const TSLanguage *tree_sitter_python();
    const TSLanguage *tree_sitter_javascript();
    const TSLanguage *tree_sitter_typescript();
    const TSLanguage *tree_sitter_tsx();
    const TSLanguage *tree_sitter_c();
    const TSLanguage *tree_sitter_cpp();
    const TSLanguage *tree_sitter_go();
    const TSLanguage *tree_sitter_java();
    const TSLanguage *tree_sitter_c_sharp();
    const TSLanguage *tree_sitter_rust();
    const TSLanguage *tree_sitter_ruby();
    const TSLanguage *tree_sitter_php();
}

namespace codesight
{
    namespace
    {

        enum class Lang
        {
            Python,
            JavaScript,
            TypeScript,
            Tsx,
            C,
            Cpp,
            Go,
            Java,
            CSharp,
            Rust,
            Ruby,
            Php,
            Unknown
        };

        std::string nodeText(TSNode n, const std::string &src)
        {
            uint32_t s = ts_node_start_byte(n);
            uint32_t e = ts_node_end_byte(n);
            if (e > src.size())
                e = static_cast<uint32_t>(src.size());
            if (s > e)
                s = e;
            return src.substr(s, e - s);
        }

        std::string lowerExt(const std::string &path)
        {
            std::string ext = fs::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c)
                           { return std::tolower(c); });
            return ext;
        }

        bool isDataExt(const std::string &ext)
        {
            static const std::set<std::string> data = {
                ".csv", ".json", ".yaml", ".yml", ".toml", ".sql", ".xml", ".md", ".txt"};
            return data.count(ext) > 0;
        }

        std::string stripDelims(const std::string &s)
        {
            if (s.size() >= 2)
            {
                char a = s.front(), b = s.back();
                if ((a == '"' && b == '"') || (a == '\'' && b == '\'') || (a == '<' && b == '>'))
                {
                    return s.substr(1, s.size() - 2);
                }
            }
            return s;
        }

        bool isNameType(const char *t)
        {
            return !strcmp(t, "identifier") || !strcmp(t, "property_identifier") ||
                   !strcmp(t, "field_identifier") || !strcmp(t, "name") ||
                   !strcmp(t, "constant") || !strcmp(t, "type_identifier");
        }

        bool isSkipType(const char *t)
        {
            static const char *skip[] = {
                "compound_statement", "block", "body", "statement_block",
                "parameter_list", "parameters", "formal_parameters",
                "argument_list", "arguments", "annotation"};
            for (auto s : skip)
                if (!strcmp(t, s))
                    return true;
            return false;
        }

        std::string findName(TSNode n, const std::string &src, int depth)
        {
            if (depth > 4)
                return "";
            uint32_t count = ts_node_child_count(n);

            for (uint32_t i = 0; i < count; i++)
            {
                TSNode c = ts_node_child(n, i);
                const char *t = ts_node_type(c);
                if (!strcmp(t, "identifier") || !strcmp(t, "property_identifier") ||
                    !strcmp(t, "field_identifier") || !strcmp(t, "name") ||
                    !strcmp(t, "constant"))
                {
                    return nodeText(c, src);
                }
            }
            for (uint32_t i = 0; i < count; i++)
            {
                TSNode c = ts_node_child(n, i);
                if (isSkipType(ts_node_type(c)))
                    continue;
                auto r = findName(c, src, depth + 1);
                if (!r.empty())
                    return r;
            }
            return "";
        }

        bool isFunctionType(const char *t)
        {
            static const char *kinds[] = {
                "function_definition", "function_declaration", "method_definition",
                "method_declaration", "constructor_declaration", "function_item",
                "method", "singleton_method", "local_function_statement"};
            for (auto k : kinds)
                if (!strcmp(t, k))
                    return true;
            return false;
        }

        bool hasBody(TSNode n)
        {
            static const char *bodies[] = {
                "field_declaration_list", "declaration_list", "class_body",
                "interface_body", "object_type", "enum_body", "body",
                "statement_block", "enum_body_declaration"};
            uint32_t count = ts_node_child_count(n);
            for (uint32_t i = 0; i < count; i++)
            {
                const char *t = ts_node_type(ts_node_child(n, i));
                for (auto b : bodies)
                    if (!strcmp(t, b))
                        return true;
            }
            return false;
        }

        // Returns a type-declaration kind, or "" when the node is not a type declaration.
        std::string typeKindFor(const char *t, Lang lang, TSNode node)
        {
            if (!strcmp(t, "class_definition") || !strcmp(t, "class_declaration") ||
                !strcmp(t, "class_specifier") || !strcmp(t, "record_declaration") ||
                !strcmp(t, "class"))
            {
                return hasBody(node) ? "class" : "";
            }
            if (!strcmp(t, "interface_declaration"))
                return "interface";
            if (!strcmp(t, "struct_specifier") || !strcmp(t, "struct_declaration"))
            {
                return hasBody(node) ? "struct" : "";
            }
            if (!strcmp(t, "struct_item"))
                return "struct";
            if (!strcmp(t, "enum_declaration") || !strcmp(t, "enum_item"))
                return "enum";
            if (!strcmp(t, "enum_specifier"))
                return hasBody(node) ? "enum" : "";
            if (!strcmp(t, "trait_item"))
                return "trait";
            if (!strcmp(t, "trait_declaration"))
                return "trait";
            if (!strcmp(t, "module"))
                return "module";

            // Go: type_declaration -> type_spec { name, struct_type | interface_type }
            if (!strcmp(t, "type_spec") && lang == Lang::Go)
            {
                uint32_t count = ts_node_child_count(node);
                for (uint32_t i = 0; i < count; i++)
                {
                    const char *ct = ts_node_type(ts_node_child(node, i));
                    if (!strcmp(ct, "struct_type"))
                        return "struct";
                    if (!strcmp(ct, "interface_type"))
                        return "interface";
                }
            }
            return "";
        }

        int computeComplexity(TSNode n, const std::string &src, int depth)
        {
            if (depth > 40)
                return 0;
            if (ts_node_end_byte(n) - ts_node_start_byte(n) > 200000)
                return 0;

            static const std::set<std::string> decisions = {
                "if_statement", "if_expression", "elif_clause", "for_statement",
                "for_in_statement", "for_of_statement", "while_statement", "do_statement",
                "switch_case", "case_clause", "catch_clause", "except_clause",
                "conditional_expression", "ternary_expression", "match_arm",
                "when_clause", "elsif_clause", "select_case", "communication_case"};

            const char *t = ts_node_type(n);
            int score = decisions.count(t) ? 1 : 0;

            if (!strcmp(t, "binary_expression") || !strcmp(t, "boolean_operator"))
            {
                if (ts_node_child_count(n) > 1)
                {
                    std::string op = nodeText(ts_node_child(n, 1), src);
                    if (op == "&&" || op == "||" || op == "and" || op == "or")
                        score = 1;
                }
            }

            uint32_t count = ts_node_child_count(n);
            for (uint32_t i = 0; i < count; i++)
            {
                score += computeComplexity(ts_node_child(n, i), src, depth + 1);
            }
            return score;
        }

        bool computeExported(TSNode n, const std::string &name, Lang lang,
                             const std::string &src, bool ctx)
        {
            if (ctx)
                return true;

            switch (lang)
            {
            case Lang::Python:
                return !name.empty() && name[0] != '_';
            case Lang::Go:
                return !name.empty() && std::isupper(static_cast<unsigned char>(name[0]));
            case Lang::Rust:
            {
                uint32_t count = ts_node_child_count(n);
                for (uint32_t i = 0; i < count; i++)
                {
                    if (!strcmp(ts_node_type(ts_node_child(n, i)), "visibility_modifier"))
                        return true;
                }
                return false;
            }
            case Lang::Java:
            case Lang::CSharp:
            {
                uint32_t count = ts_node_child_count(n);
                for (uint32_t i = 0; i < count; i++)
                {
                    TSNode c = ts_node_child(n, i);
                    if (!strcmp(ts_node_type(c), "modifiers"))
                    {
                        return nodeText(c, src).find("public") != std::string::npos;
                    }
                }
                return false;
            }
            case Lang::Php:
            {
                uint32_t count = ts_node_child_count(n);
                for (uint32_t i = 0; i < count; i++)
                {
                    TSNode c = ts_node_child(n, i);
                    if (!strcmp(ts_node_type(c), "visibility_modifier"))
                    {
                        std::string v = nodeText(c, src);
                        if (v.find("private") != std::string::npos ||
                            v.find("protected") != std::string::npos)
                            return false;
                    }
                }
                return true;
            }
            case Lang::C:
            case Lang::Cpp:
            {
                std::string text = nodeText(n, src);
                return text.rfind("static ", 0) != 0;
            }
            default:
                return true;
            }
        }

        void collectStrings(TSNode n, const std::string &src,
                            std::vector<std::string> &out, int depth)
        {
            if (depth > 6)
                return;
            const char *t = ts_node_type(n);
            if (!strcmp(t, "string_literal") || !strcmp(t, "interpreted_string_literal") ||
                !strcmp(t, "system_lib_string") || !strcmp(t, "string") ||
                !strcmp(t, "encapsed_string"))
            {
                out.push_back(stripDelims(nodeText(n, src)));
                return;
            }
            uint32_t count = ts_node_child_count(n);
            for (uint32_t i = 0; i < count; i++)
            {
                collectStrings(ts_node_child(n, i), src, out, depth + 1);
            }
        }

        std::string trimWs(std::string s)
        {
            while (!s.empty() && isspace((unsigned char)s.front()))
                s.erase(s.begin());
            while (!s.empty() && isspace((unsigned char)s.back()))
                s.pop_back();
            return s;
        }

        std::string normalizeDirective(std::string text,
                                       const std::vector<std::string> &prefixes)
        {
            text = trimWs(text);
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (const auto &p : prefixes)
                {
                    if (text.rfind(p, 0) == 0)
                    {
                        text = trimWs(text.substr(p.size()));
                        changed = true;
                        break;
                    }
                }
            }
            if (!text.empty() && text.back() == ';')
                text.pop_back();
            return trimWs(text);
        }

    } // anonymous namespace

    struct Parser::Impl
    {
        TSParser *parser;

        Impl() { parser = ts_parser_new(); }
        ~Impl() { ts_parser_delete(parser); }

        Lang langForExt(const std::string &ext)
        {
            if (ext == ".py")
                return Lang::Python;
            if (ext == ".js" || ext == ".jsx" || ext == ".mjs" || ext == ".cjs")
                return Lang::JavaScript;
            if (ext == ".ts" || ext == ".mts" || ext == ".cts")
                return Lang::TypeScript;
            if (ext == ".tsx")
                return Lang::Tsx;
            if (ext == ".c" || ext == ".h")
                return Lang::C;
            if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
                ext == ".hpp" || ext == ".hh" || ext == ".hxx")
                return Lang::Cpp;
            if (ext == ".go")
                return Lang::Go;
            if (ext == ".java")
                return Lang::Java;
            if (ext == ".cs")
                return Lang::CSharp;
            if (ext == ".rs")
                return Lang::Rust;
            if (ext == ".rb")
                return Lang::Ruby;
            if (ext == ".php")
                return Lang::Php;
            return Lang::Unknown;
        }

        const TSLanguage *grammarFor(Lang lang)
        {
            switch (lang)
            {
            case Lang::Python:
                return tree_sitter_python();
            case Lang::JavaScript:
                return tree_sitter_javascript();
            case Lang::TypeScript:
                return tree_sitter_typescript();
            case Lang::Tsx:
                return tree_sitter_tsx();
            case Lang::C:
                return tree_sitter_c();
            case Lang::Cpp:
                return tree_sitter_cpp();
            case Lang::Go:
                return tree_sitter_go();
            case Lang::Java:
                return tree_sitter_java();
            case Lang::CSharp:
                return tree_sitter_c_sharp();
            case Lang::Rust:
                return tree_sitter_rust();
            case Lang::Ruby:
                return tree_sitter_ruby();
            case Lang::Php:
                return tree_sitter_php();
            default:
                return nullptr;
            }
        }

        std::string readFile(const std::string &filepath)
        {
            std::ifstream file(filepath, std::ios::binary);
            if (!file.is_open())
                return "";
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        void walkSymbols(TSNode node, const std::string &src, Lang lang,
                         const std::string &parent, bool exportCtx,
                         std::vector<CppSymbolInfo> &out, int depth)
        {
            if (depth > 60)
                return;

            const char *t = ts_node_type(node);
            std::string nextParent = parent;
            bool nextExport = exportCtx || !strcmp(t, "export_statement");

            int line = static_cast<int>(ts_node_start_point(node).row) + 1;
            int endLine = static_cast<int>(ts_node_end_point(node).row) + 1;

            std::string typeKind = typeKindFor(t, lang, node);

            if (!typeKind.empty())
            {
                std::string name = findName(node, src, 0);
                if (!name.empty())
                {
                    out.push_back({name, typeKind, line, endLine, parent,
                                   computeExported(node, name, lang, src, nextExport), 1});
                    nextParent = parent.empty() ? name : parent + "." + name;
                }
            }
            else if (!strcmp(t, "impl_item"))
            {
                std::string name = findName(node, src, 0);
                if (!name.empty())
                    nextParent = name;
            }
            else if (isFunctionType(t))
            {
                std::string name = findName(node, src, 0);
                if (!name.empty())
                {
                    std::string kind = parent.empty() ? "function" : "method";
                    if (!strcmp(t, "constructor_declaration") ||
                        name == "__init__" || name == "constructor")
                    {
                        kind = "constructor";
                    }
                    int complexity = 1 + computeComplexity(node, src, 0);
                    out.push_back({name, kind, line, endLine, parent,
                                   computeExported(node, name, lang, src, nextExport),
                                   complexity});
                }
            }

            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; i++)
            {
                walkSymbols(ts_node_child(node, i), src, lang, nextParent, nextExport, out, depth + 1);
            }
        }

        void extractImports(TSNode node, const std::string &source,
                            std::vector<CppImportInfo> &imports)
        {
            const char *t = ts_node_type(node);
            int line = static_cast<int>(ts_node_start_point(node).row) + 1;
            auto push = [&](const std::string &m)
            {
                if (!m.empty())
                    imports.push_back({m, "", line});
            };

            if (!strcmp(t, "import_statement") || !strcmp(t, "import_from_statement"))
            {
                push(nodeText(node, source));
            }
            else if (!strcmp(t, "preproc_include"))
            {
                std::vector<std::string> s;
                collectStrings(node, source, s, 0);
                for (auto &x : s)
                    push(x);
            }
            else if (!strcmp(t, "import_declaration"))
            {
                std::vector<std::string> s;
                collectStrings(node, source, s, 0);
                if (!s.empty())
                {
                    for (auto &x : s)
                        push(x);
                }
                else
                {
                    push(normalizeDirective(nodeText(node, source), {"import ", "static "}));
                }
            }
            else if (!strcmp(t, "using_directive"))
            {
                push(normalizeDirective(nodeText(node, source), {"using ", "static "}));
            }
            else if (!strcmp(t, "use_declaration"))
            {
                push(normalizeDirective(nodeText(node, source), {"use "}));
            }
            else if (!strcmp(t, "include_expression") || !strcmp(t, "require_expression") ||
                     !strcmp(t, "include_once_expression") || !strcmp(t, "require_once_expression"))
            {
                std::vector<std::string> s;
                collectStrings(node, source, s, 0);
                for (auto &x : s)
                    push(x);
            }
            else if (!strcmp(t, "call"))
            {
                std::string text = nodeText(node, source);
                if (text.rfind("require", 0) == 0)
                {
                    std::vector<std::string> s;
                    collectStrings(node, source, s, 0);
                    for (auto &x : s)
                        push(x);
                }
            }

            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; i++)
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

        std::string ext = lowerExt(filepath);
        if (isDataExt(ext))
            return result;

        Lang lang = pimpl_->langForExt(ext);
        const TSLanguage *language = pimpl_->grammarFor(lang);
        if (!language)
        {
            std::cerr << "Unsupported file type: " << filepath << "\n";
            return result;
        }

        std::string source = pimpl_->readFile(filepath);
        if (source.empty())
            return result;

        ts_parser_set_language(pimpl_->parser, language);

        TSTree *tree = ts_parser_parse_string(
            pimpl_->parser, nullptr, source.c_str(),
            static_cast<uint32_t>(source.length()));
        if (!tree)
        {
            std::cerr << "Failed to parse file: " << filepath << "\n";
            return result;
        }

        TSNode root = ts_tree_root_node(tree);
        pimpl_->walkSymbols(root, source, lang, "", false, result.symbols, 0);
        pimpl_->extractImports(root, source, result.imports);

        for (const auto &s : result.symbols)
        {
            if (s.kind == "function" || s.kind == "method" || s.kind == "constructor")
            {
                result.functions.push_back({s.name, s.line_number});
            }
        }

        ts_tree_delete(tree);
        return result;
    }

} // namespace codesight
