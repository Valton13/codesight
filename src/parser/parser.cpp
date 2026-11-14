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
#include <regex>
#include <tuple>

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

        bool isNull(TSNode n) { return ts_node_is_null(n); }

        TSNode fieldChild(TSNode n, const char *name)
        {
            return ts_node_child_by_field_name(n, name, static_cast<uint32_t>(strlen(name)));
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

        bool isSkipType(const char *t)
        {
            static const char *skip[] = {
                "compound_statement", "block", "body", "statement_block",
                "parameter_list", "parameters", "formal_parameters",
                "argument_list", "arguments", "annotation",
                "type_identifier", "scoped_type_identifier", "generic_type",
                "array_type", "primitive_type", "class_body", "interface_body",
                "field_declaration_list", "declaration_list", "enum_body"};
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

        // Prefer grammar field names; fall back to a bounded structural search.
        std::string declName(TSNode n, const std::string &src)
        {
            TSNode name = fieldChild(n, "name");
            if (!isNull(name))
                return nodeText(name, src);
            TSNode decl = fieldChild(n, "declarator");
            if (!isNull(decl))
                return findName(decl, src, 0);
            return findName(n, src, 0);
        }

        std::string firstOfType(TSNode n, const char *type, const std::string &src, int depth = 0)
        {
            if (depth > 5)
                return "";
            if (!strcmp(ts_node_type(n), type))
                return nodeText(n, src);
            uint32_t count = ts_node_child_count(n);
            for (uint32_t i = 0; i < count; i++)
            {
                auto r = firstOfType(ts_node_child(n, i), type, src, depth + 1);
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

        bool isCallType(const char *t, Lang lang)
        {
            switch (lang)
            {
            case Lang::Python:
                return !strcmp(t, "call");
            case Lang::JavaScript:
            case Lang::TypeScript:
            case Lang::Tsx:
            case Lang::C:
            case Lang::Cpp:
            case Lang::Go:
            case Lang::Rust:
                return !strcmp(t, "call_expression");
            case Lang::Java:
                return !strcmp(t, "method_invocation");
            case Lang::CSharp:
                return !strcmp(t, "invocation_expression");
            case Lang::Ruby:
                return !strcmp(t, "call") || !strcmp(t, "method_call");
            case Lang::Php:
                return !strcmp(t, "function_call_expression") ||
                       !strcmp(t, "member_call_expression");
            default:
                return false;
            }
        }

        std::string calleeText(TSNode n, Lang lang, const std::string &src)
        {
            if (lang == Lang::Java)
            {
                TSNode name = fieldChild(n, "name");
                TSNode obj = fieldChild(n, "object");
                std::string nm = isNull(name) ? "" : nodeText(name, src);
                if (!isNull(obj))
                    return nodeText(obj, src) + "." + nm;
                return nm;
            }
            if (lang == Lang::Ruby)
            {
                TSNode m = fieldChild(n, "method");
                TSNode r = fieldChild(n, "receiver");
                std::string nm = isNull(m) ? "" : nodeText(m, src);
                if (!isNull(r))
                    return nodeText(r, src) + "." + nm;
                return nm;
            }
            if (lang == Lang::Php && !strcmp(ts_node_type(n), "member_call_expression"))
            {
                TSNode obj = fieldChild(n, "object");
                TSNode nm = fieldChild(n, "name");
                std::string name = isNull(nm) ? "" : nodeText(nm, src);
                if (!isNull(obj))
                    return nodeText(obj, src) + "." + name;
                return name;
            }
            TSNode f = fieldChild(n, "function");
            if (isNull(f))
                f = fieldChild(n, "name");
            if (isNull(f) && ts_node_child_count(n) > 0)
                f = ts_node_child(n, 0);
            return isNull(f) ? "" : nodeText(f, src);
        }

        bool hasBody(TSNode n)
        {
            if (!isNull(fieldChild(n, "body")))
                return true;
            static const char *bodies[] = {
                "field_declaration_list", "declaration_list", "class_body",
                "interface_body", "object_type", "enum_body", "body", "block",
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

        std::string typeKindFor(const char *t, Lang lang, TSNode node)
        {
            // C/C++ forward declarations must not create type nodes; other grammars
            // only emit declaration nodes for real definitions.
            bool requireBody = (lang == Lang::C || lang == Lang::Cpp);
            bool ok = !requireBody || hasBody(node);

            if (!strcmp(t, "class_definition") || !strcmp(t, "class_declaration") ||
                !strcmp(t, "class_specifier") || !strcmp(t, "record_declaration") ||
                !strcmp(t, "class"))
            {
                return ok ? "class" : "";
            }
            if (!strcmp(t, "interface_declaration"))
                return ok ? "interface" : "";
            if (!strcmp(t, "struct_specifier") || !strcmp(t, "struct_declaration") ||
                !strcmp(t, "struct_item"))
            {
                return ok ? "struct" : "";
            }
            if (!strcmp(t, "enum_declaration") || !strcmp(t, "enum_item") ||
                !strcmp(t, "enum_specifier"))
            {
                return ok ? "enum" : "";
            }
            if (!strcmp(t, "trait_item") || !strcmp(t, "trait_declaration"))
                return ok ? "trait" : "";
            if (!strcmp(t, "module") && lang == Lang::Ruby)
                return ok ? "module" : "";

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

        std::string upperString(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c)
                           { return std::toupper(c); });
            return s;
        }

        std::vector<std::string> splitLines(const std::string &src)
        {
            std::vector<std::string> lines;
            std::istringstream stream(src);
            std::string line;
            while (std::getline(stream, line))
                lines.push_back(line);
            return lines;
        }

        std::string firstQuoted(const std::string &s)
        {
            static const std::regex re("[\"'`]([^\"'`]+)[\"'`]");
            std::smatch m;
            if (std::regex_search(s, m, re))
                return m[1].str();
            return "";
        }

        std::string joinRoutePath(const std::string &base, const std::string &path)
        {
            std::string b = trimWs(base);
            std::string p = trimWs(path);
            while (!b.empty() && b.back() == '/')
                b.pop_back();
            while (!p.empty() && p.front() == '/')
                p.erase(p.begin());
            std::string combined = b.empty() ? p : (p.empty() ? b : b + "/" + p);
            if (combined.empty())
                return "/";
            if (combined.front() != '/')
                combined = "/" + combined;
            return combined;
        }

        std::string findNextName(const std::vector<std::string> &lines, size_t start,
                                 const std::regex &re, size_t lookahead = 12)
        {
            size_t end = std::min(lines.size(), start + lookahead);
            for (size_t j = start + 1; j < end; ++j)
            {
                std::string line = trimWs(lines[j]);
                if (line.empty() || line[0] == '@' || line.rfind("//", 0) == 0 ||
                    line.rfind("#", 0) == 0 || line.rfind("/*", 0) == 0)
                    continue;
                std::smatch m;
                if (std::regex_search(line, m, re))
                    return m[1].str();
            }
            return "";
        }

        std::vector<std::string> parseMethodsList(const std::string &text)
        {
            std::vector<std::string> methods;
            static const std::regex methodsRe(R"(methods\s*=\s*\[([^\]]*)\])", std::regex_constants::icase);
            std::smatch m;
            if (std::regex_search(text, m, methodsRe))
            {
                std::string inner = m[1].str();
                static const std::regex wordRe("[\"']([A-Za-z]+)[\"']");
                auto begin = std::sregex_iterator(inner.begin(), inner.end(), wordRe);
                auto end = std::sregex_iterator();
                for (auto it = begin; it != end; ++it)
                    methods.push_back(upperString((*it)[1].str()));
            }
            return methods;
        }

        std::string handlerAfterPath(const std::string &line, const std::string &path)
        {
            size_t pos = line.find(path);
            if (pos == std::string::npos)
                return "";
            std::string rest = line.substr(pos + path.size());
            static const std::regex re(R"(,\s*([A-Za-z_$][A-Za-z0-9_$\.]*))");
            std::smatch m;
            if (std::regex_search(rest, m, re))
            {
                std::string h = m[1].str();
                if (h == "async" || h == "function" || h == "req" || h == "res")
                    return "";
                return h;
            }
            return "";
        }

        void extractRoutes(const std::string &src, Lang lang, std::vector<CppApiRouteInfo> &out)
        {
            auto lines = splitLines(src);
            std::set<std::tuple<std::string, std::string, std::string, int>> seen;

            auto add = [&](const std::string &method, const std::string &path,
                           const std::string &handler, int line)
            {
                if (path.empty())
                    return;
                std::string m = upperString(method);
                std::string p = path.front() == '/' ? path : "/" + path;
                auto key = std::make_tuple(m, p, handler, line);
                if (seen.insert(key).second)
                    out.push_back({m, p, handler, line});
            };

            if (lang == Lang::Python)
            {
                static const std::regex decoRe(
                    R"(@\s*[A-Za-z_]\w*\s*\.\s*(get|post|put|patch|delete|head|options|route|api_route|websocket)\s*\()",
                    std::regex_constants::icase);
                static const std::regex defRe(R"((?:async\s+)?def\s+([A-Za-z_]\w*)\s*\()");
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    std::smatch m;
                    if (!std::regex_search(lines[i], m, decoRe))
                        continue;
                    std::string verb = m[1].str();
                    std::string path = firstQuoted(lines[i]);
                    std::string handler = findNextName(lines, i, defRe);
                    int line = static_cast<int>(i) + 1;
                    if (verb == "websocket")
                    {
                        add("WS", path, handler, line);
                    }
                    else if (verb == "route" || verb == "api_route")
                    {
                        auto methods = parseMethodsList(lines[i]);
                        if (methods.empty())
                            methods.push_back("GET");
                        for (const auto &mm : methods)
                            add(mm, path, handler, line);
                    }
                    else
                    {
                        add(verb, path, handler, line);
                    }
                }
            }

            if (lang == Lang::JavaScript || lang == Lang::TypeScript || lang == Lang::Tsx)
            {
                static const std::regex expressRe(
                    R"(\b(?:app|router|route|r|server|api)\s*\.\s*(get|post|put|patch|delete|all|options|head)\s*\(\s*['"`]([^'"`]+)['"`])",
                    std::regex_constants::icase);
                static const std::regex nestRe(
                    R"(@\s*(Get|Post|Put|Patch|Delete|Head|Options|All)\s*\()",
                    std::regex_constants::icase);
                static const std::regex controllerRe(R"(@\s*Controller\s*\()");
                static const std::regex methodRe(R"((?:async\s+)?([A-Za-z_$][A-Za-z0-9_$]*)\s*\()");

                std::string controllerBase;
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    const auto &line = lines[i];
                    if (std::regex_search(line, controllerRe))
                    {
                        controllerBase = firstQuoted(line);
                        continue;
                    }
                    std::smatch m;
                    if (std::regex_search(line, m, expressRe))
                    {
                        std::string verb = m[1].str();
                        std::string path = m[2].str();
                        std::string handler = handlerAfterPath(line, path);
                        add(verb, path, handler, static_cast<int>(i) + 1);
                        continue;
                    }
                    if (std::regex_search(line, m, nestRe))
                    {
                        std::string verb = m[1].str();
                        std::string path = firstQuoted(line);
                        std::string handler = findNextName(lines, i, methodRe);
                        add(verb, joinRoutePath(controllerBase, path), handler, static_cast<int>(i) + 1);
                    }
                }
            }

            if (lang == Lang::Java)
            {
                static const std::regex mappingRe(
                    R"(@(Get|Post|Put|Delete|Patch|Request)Mapping\s*\()");
                static const std::regex methodRe(R"(([A-Za-z_]\w*)\s*\()");
                static const std::regex requestMethodRe(R"(RequestMethod\.([A-Z]+))");
                std::string basePath;
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    const auto &line = lines[i];
                    if (line.find("@RequestMapping") != std::string::npos &&
                        i + 1 < lines.size() && lines[i + 1].find("class ") != std::string::npos)
                    {
                        basePath = firstQuoted(line);
                        continue;
                    }
                    std::smatch m;
                    if (!std::regex_search(line, m, mappingRe))
                        continue;
                    std::string ann = m[1].str();
                    std::string path = firstQuoted(line);
                    std::string verb = ann == "Request" ? "GET" : ann;
                    if (ann == "Request")
                    {
                        std::smatch rm;
                        if (std::regex_search(line, rm, requestMethodRe))
                            verb = rm[1].str();
                    }
                    std::string handler = findNextName(lines, i, methodRe);
                    add(verb, joinRoutePath(basePath, path), handler, static_cast<int>(i) + 1);
                }
            }

            if (lang == Lang::Go)
            {
                static const std::regex verbRe(
                    R"RE(\.\s*(GET|POST|PUT|PATCH|DELETE|HEAD|OPTIONS|Get|Post|Put|Patch|Delete|Head|Options)\s*\(\s*"([^"]+)")RE");
                static const std::regex handleFuncRe(R"RE(HandleFunc\s*\(\s*"([^"]+)")RE");
                static const std::regex methodsRe(R"RE(Methods\s*\(\s*"([A-Z]+)")RE");
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    const auto &line = lines[i];
                    std::smatch m;
                    if (std::regex_search(line, m, verbRe))
                    {
                        std::string verb = m[1].str();
                        std::string path = m[2].str();
                        add(verb, path, handlerAfterPath(line, path), static_cast<int>(i) + 1);
                        continue;
                    }
                    if (std::regex_search(line, m, handleFuncRe))
                    {
                        std::string path = m[1].str();
                        std::string verb = "GET";
                        std::smatch mm;
                        if (std::regex_search(line, mm, methodsRe))
                            verb = mm[1].str();
                        add(verb, path, handlerAfterPath(line, path), static_cast<int>(i) + 1);
                    }
                }
            }

            if (lang == Lang::Ruby)
            {
                static const std::regex verbRe(R"(^\s*(get|post|put|patch|delete|match)\s+['"]([^'"]+)['"])",
                                               std::regex_constants::icase);
                static const std::regex toRe(R"(to:\s*['"]([^'"]+)['"])");
                static const std::regex rootRe(R"(^\s*root\s+['"]([^'"]+)['"])");
                static const std::regex resourcesRe(R"(^\s*resources\s+:(\w+))");
                static const std::regex viaRe(R"(:(get|post|put|patch|delete))", std::regex_constants::icase);
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    const auto &line = lines[i];
                    std::smatch m;
                    int ln = static_cast<int>(i) + 1;
                    if (std::regex_search(line, m, rootRe))
                    {
                        add("GET", "/", m[1].str(), ln);
                        continue;
                    }
                    if (std::regex_search(line, m, resourcesRe))
                    {
                        std::string c = m[1].str();
                        std::string base = "/" + c;
                        add("GET", base, c + "#index", ln);
                        add("POST", base, c + "#create", ln);
                        add("GET", base + "/:id", c + "#show", ln);
                        add("PATCH", base + "/:id", c + "#update", ln);
                        add("PUT", base + "/:id", c + "#update", ln);
                        add("DELETE", base + "/:id", c + "#destroy", ln);
                        continue;
                    }
                    if (std::regex_search(line, m, verbRe))
                    {
                        std::string verb = m[1].str();
                        std::string path = m[2].str();
                        std::string handler;
                        std::smatch tm;
                        if (std::regex_search(line, tm, toRe))
                            handler = tm[1].str();
                        if (verb == "match")
                        {
                            auto begin = std::sregex_iterator(line.begin(), line.end(), viaRe);
                            auto end = std::sregex_iterator();
                            bool any = false;
                            for (auto it = begin; it != end; ++it)
                            {
                                add((*it)[1].str(), path, handler, ln);
                                any = true;
                            }
                            if (!any)
                                add("GET", path, handler, ln);
                        }
                        else
                        {
                            add(verb, path, handler, ln);
                        }
                    }
                }
            }

            if (lang == Lang::Php)
            {
                static const std::regex routeRe(
                    R"(Route::(get|post|put|patch|delete|options)\s*\(\s*['"]([^'"]+)['"])",
                    std::regex_constants::icase);
                static const std::regex handlerRe(R"(([A-Za-z_\\][A-Za-z0-9_\\]*)::class\s*,\s*['"](\w+)['"])");
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    const auto &line = lines[i];
                    std::smatch m;
                    if (!std::regex_search(line, m, routeRe))
                        continue;
                    std::string verb = m[1].str();
                    std::string path = m[2].str();
                    std::string handler;
                    std::smatch hm;
                    if (std::regex_search(line, hm, handlerRe))
                        handler = hm[1].str() + "::" + hm[2].str();
                    add(verb, path, handler, static_cast<int>(i) + 1);
                }
            }
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
                         std::vector<CppSymbolInfo> &out,
                         std::vector<CppCallInfo> &calls,
                         const std::string &currentCaller, int depth)
        {
            if (depth > 60)
                return;

            const char *t = ts_node_type(node);
            std::string nextParent = parent;
            std::string nextCaller = currentCaller;
            bool nextExport = exportCtx || !strcmp(t, "export_statement");

            int line = static_cast<int>(ts_node_start_point(node).row) + 1;
            int endLine = static_cast<int>(ts_node_end_point(node).row) + 1;

            if (isCallType(t, lang))
            {
                std::string callee = calleeText(node, lang, src);
                if (!callee.empty() && callee.size() < 120)
                {
                    calls.push_back({currentCaller, callee, line});
                }
            }

            std::string typeKind = typeKindFor(t, lang, node);

            if (!typeKind.empty())
            {
                std::string name = declName(node, src);
                if (!name.empty())
                {
                    out.push_back({name, typeKind, line, endLine, parent,
                                   computeExported(node, name, lang, src, nextExport), 1});
                    nextParent = parent.empty() ? name : parent + "." + name;
                }
            }
            else if (!strcmp(t, "impl_item"))
            {
                TSNode ty = fieldChild(node, "type");
                std::string name = !isNull(ty) ? nodeText(ty, src)
                                               : firstOfType(node, "type_identifier", src);
                if (!name.empty())
                    nextParent = name;
            }
            else if (isFunctionType(t))
            {
                std::string name = declName(node, src);
                if (!name.empty())
                {
                    std::string owner = parent;
                    // Go methods carry their receiver type explicitly.
                    if (lang == Lang::Go && !strcmp(t, "method_declaration"))
                    {
                        TSNode recv = fieldChild(node, "receiver");
                        if (!isNull(recv))
                        {
                            std::string rn = firstOfType(recv, "type_identifier", src);
                            if (!rn.empty())
                                owner = rn;
                        }
                    }
                    std::string kind = owner.empty() ? "function" : "method";
                    if (!strcmp(t, "constructor_declaration") ||
                        name == "__init__" || name == "constructor")
                    {
                        kind = "constructor";
                    }
                    int complexity = 1 + computeComplexity(node, src, 0);
                    out.push_back({name, kind, line, endLine, owner,
                                   computeExported(node, name, lang, src, nextExport),
                                   complexity});
                    nextCaller = owner.empty() ? name : owner + "." + name;
                    nextParent = owner;
                }
            }

            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; i++)
            {
                walkSymbols(ts_node_child(node, i), src, lang, nextParent, nextExport,
                            out, calls, nextCaller, depth + 1);
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
            else if (!strcmp(t, "use_declaration") || !strcmp(t, "namespace_use_declaration"))
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
            else if (!strcmp(t, "call") || !strcmp(t, "call_expression"))
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
        pimpl_->walkSymbols(root, source, lang, "", false, result.symbols, result.calls, "", 0);
        pimpl_->extractImports(root, source, result.imports);
        extractRoutes(source, lang, result.api_routes);

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
