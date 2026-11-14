#include "parser/parser.h"
#include "utils/file_filter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace codesight;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        ++g_checks;                                                     \
        if (!(cond))                                                    \
        {                                                               \
            ++g_failures;                                               \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " \
                      << #cond << "\n";                                 \
        }                                                               \
    } while (0)

static fs::path g_base;
static int g_counter = 0;

static std::string writeTemp(const std::string &ext, const std::string &content)
{
    fs::create_directories(g_base);
    fs::path p = g_base / ("case_" + std::to_string(++g_counter) + ext);
    std::ofstream out(p, std::ios::binary);
    out << content;
    out.close();
    return p.string();
}

static const CppSymbolInfo *findSymbol(const CppParsedFile &f,
                                       const std::string &name,
                                       const std::string &kind = "")
{
    for (const auto &s : f.symbols)
    {
        if (s.name == name && (kind.empty() || s.kind == kind))
            return &s;
    }
    return nullptr;
}

static bool hasImport(const CppParsedFile &f, const std::string &sub)
{
    for (const auto &i : f.imports)
        if (i.module.find(sub) != std::string::npos)
            return true;
    return false;
}

static bool hasRoute(const CppParsedFile &f, const std::string &method, const std::string &path)
{
    for (const auto &r : f.api_routes)
        if (r.method == method && r.path == path)
            return true;
    return false;
}

static bool hasCall(const CppParsedFile &f, const std::string &sub)
{
    for (const auto &c : f.calls)
        if (c.callee.find(sub) != std::string::npos)
            return true;
    return false;
}

static void testPython()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".py",
                                   "import os\n"
                                   "from fastapi import FastAPI\n"
                                   "app = FastAPI()\n"
                                   "def helper():\n"
                                   "    return 1\n"
                                   "class Service:\n"
                                   "    def run(self):\n"
                                   "        self.helper()\n"
                                   "        return helper()\n"
                                   "@app.get(\"/users\")\n"
                                   "def list_users():\n"
                                   "    return helper()\n"
                                   "@app.route(\"/legacy\", methods=[\"GET\",\"POST\"])\n"
                                   "def legacy():\n"
                                   "    return 0\n"));
    CHECK(findSymbol(f, "helper", "function"));
    CHECK(findSymbol(f, "Service", "class"));
    CHECK(findSymbol(f, "run", "method"));
    CHECK(hasImport(f, "fastapi"));
    CHECK(hasRoute(f, "GET", "/users"));
    CHECK(hasRoute(f, "GET", "/legacy"));
    CHECK(hasRoute(f, "POST", "/legacy"));
    CHECK(hasCall(f, "helper"));
    CHECK(hasCall(f, "self.helper"));
}

static void testJavaScript()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".js",
                                   "import { a } from './m';\n"
                                   "const express = require('express');\n"
                                   "const app = express();\n"
                                   "function helper(){ return 1; }\n"
                                   "class Svc { run(){ return helper(); } }\n"
                                   "app.get('/x', helper);\n"
                                   "router.post('/y', (req,res)=>{});\n"));
    CHECK(findSymbol(f, "helper", "function"));
    CHECK(findSymbol(f, "Svc", "class"));
    CHECK(findSymbol(f, "run", "method"));
    CHECK(hasImport(f, "./m"));
    CHECK(hasImport(f, "express"));
    CHECK(hasRoute(f, "GET", "/x"));
    CHECK(hasRoute(f, "POST", "/y"));
    CHECK(hasCall(f, "helper"));
}

static void testTypeScript()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".ts",
                                   "import { a } from './m';\n"
                                   "interface I { m(): void; }\n"
                                   "function helper(){ return 1; }\n"
                                   "class Svc implements I { m(){ return helper(); } }\n"
                                   "app.get('/ts', helper);\n"));
    CHECK(findSymbol(f, "I", "interface"));
    CHECK(findSymbol(f, "Svc", "class"));
    CHECK(findSymbol(f, "m", "method"));
    CHECK(findSymbol(f, "helper", "function"));
    CHECK(hasImport(f, "./m"));
    CHECK(hasRoute(f, "GET", "/ts"));
    CHECK(hasCall(f, "helper"));
}

static void testGo()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".go",
                                   "package main\n"
                                   "import \"fmt\"\n"
                                   "type S struct { X int }\n"
                                   "type I interface { M() }\n"
                                   "func helper() int { return 1 }\n"
                                   "func (s S) Run() int { return helper() }\n"
                                   "func main() { r.GET(\"/go\", helper) }\n"));
    CHECK(findSymbol(f, "S", "struct"));
    CHECK(findSymbol(f, "I", "interface"));
    CHECK(findSymbol(f, "helper", "function"));
    CHECK(findSymbol(f, "Run", "method"));
    CHECK(hasImport(f, "fmt"));
    CHECK(hasRoute(f, "GET", "/go"));
    CHECK(hasCall(f, "helper"));
}

static void testJava()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".java",
                                   "package a;\n"
                                   "import java.util.List;\n"
                                   "interface I { void m(); }\n"
                                   "enum E { A }\n"
                                   "class C { public void m(){ helper(); } private int helper(){ return 1; } }\n"
                                   "@RestController\n"
                                   "@RequestMapping(\"/api\")\n"
                                   "class Ctrl { @GetMapping(\"/x\") public String x(){ return helper(); } }\n"));
    CHECK(findSymbol(f, "I", "interface"));
    CHECK(findSymbol(f, "E", "enum"));
    CHECK(findSymbol(f, "C", "class"));
    CHECK(findSymbol(f, "m", "method"));
    CHECK(findSymbol(f, "helper", "method"));
    CHECK(hasImport(f, "java.util.List"));
    CHECK(hasRoute(f, "GET", "/api/x"));
    CHECK(hasCall(f, "helper"));
}

static void testCSharp()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".cs",
                                   "using System;\n"
                                   "interface I { void M(); }\n"
                                   "struct S { public int X; }\n"
                                   "enum E { A }\n"
                                   "class C { public void M(){ Helper(); } private int Helper(){ return 1; } }\n"));
    CHECK(findSymbol(f, "I", "interface"));
    CHECK(findSymbol(f, "S", "struct"));
    CHECK(findSymbol(f, "E", "enum"));
    CHECK(findSymbol(f, "C", "class"));
    CHECK(findSymbol(f, "M", "method"));
    CHECK(findSymbol(f, "Helper", "method"));
    CHECK(hasImport(f, "System"));
    CHECK(hasCall(f, "Helper"));
}

static void testC()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".c",
                                   "#include <stdio.h>\n"
                                   "#include \"local.h\"\n"
                                   "struct S { int x; };\n"
                                   "enum E { A };\n"
                                   "int helper(void){ return 1; }\n"
                                   "int main(void){ return helper(); }\n"));
    CHECK(findSymbol(f, "S", "struct"));
    CHECK(findSymbol(f, "E", "enum"));
    CHECK(findSymbol(f, "helper", "function"));
    CHECK(findSymbol(f, "main", "function"));
    CHECK(hasImport(f, "stdio.h"));
    CHECK(hasImport(f, "local.h"));
    CHECK(hasCall(f, "helper"));
}

static void testCpp()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".cpp",
                                   "#include <vector>\n"
                                   "#include \"local.hpp\"\n"
                                   "class C { public: void m(){ helper(); } };\n"
                                   "struct S { int x; };\n"
                                   "enum E { A };\n"
                                   "int helper(){ return 1; }\n"
                                   "int main(){ return helper(); }\n"));
    CHECK(findSymbol(f, "C", "class"));
    CHECK(findSymbol(f, "S", "struct"));
    CHECK(findSymbol(f, "E", "enum"));
    CHECK(findSymbol(f, "m", "method"));
    CHECK(findSymbol(f, "helper", "function"));
    CHECK(hasImport(f, "vector"));
    CHECK(hasImport(f, "local.hpp"));
    CHECK(hasCall(f, "helper"));
}

static void testRust()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".rs",
                                   "use crate::m::f;\n"
                                   "struct S { x: i32 }\n"
                                   "enum E { A }\n"
                                   "trait T { fn m(&self); }\n"
                                   "impl S { fn m(&self){ helper(); } }\n"
                                   "fn helper() -> i32 { 1 }\n"));
    CHECK(findSymbol(f, "S", "struct"));
    CHECK(findSymbol(f, "E", "enum"));
    CHECK(findSymbol(f, "T", "trait"));
    CHECK(findSymbol(f, "m", "method"));
    CHECK(findSymbol(f, "helper", "function"));
    CHECK(hasImport(f, "crate::m::f"));
    CHECK(hasCall(f, "helper"));
}

static void testRuby()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".rb",
                                   "require 'sinatra'\n"
                                   "class C\n"
                                   "  def m\n"
                                   "    helper()\n"
                                   "  end\n"
                                   "end\n"
                                   "module M\n"
                                   "end\n"
                                   "def helper\n"
                                   "  1\n"
                                   "end\n"
                                   "get '/rb', to: 'c#m'\n"));
    CHECK(findSymbol(f, "C", "class"));
    CHECK(findSymbol(f, "M", "module"));
    CHECK(findSymbol(f, "m", "method"));
    CHECK(findSymbol(f, "helper", "function"));
    CHECK(hasImport(f, "sinatra"));
    CHECK(hasRoute(f, "GET", "/rb"));
    CHECK(hasCall(f, "helper"));
}

static void testPhp()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".php",
                                   "<?php\n"
                                   "use App\\Util;\n"
                                   "require 'vendor/autoload.php';\n"
                                   "interface I { public function m(); }\n"
                                   "trait T { }\n"
                                   "enum E { case A; }\n"
                                   "class C { public function m(){ $this->helper(); } private function helper(){ return 1; } }\n"
                                   "Route::get('/php', [C::class, 'm']);\n"));
    CHECK(findSymbol(f, "I", "interface"));
    CHECK(findSymbol(f, "T", "trait"));
    CHECK(findSymbol(f, "E", "enum"));
    CHECK(findSymbol(f, "C", "class"));
    CHECK(findSymbol(f, "m", "method"));
    CHECK(findSymbol(f, "helper", "method"));
    CHECK(hasImport(f, "App\\Util"));
    CHECK(hasImport(f, "vendor/autoload.php"));
    CHECK(hasRoute(f, "GET", "/php"));
    CHECK(hasCall(f, "helper"));
}

static void testDataAsset()
{
    Parser p;
    auto f = p.parseFile(writeTemp(".csv", "a,b\n1,2\n"));
    CHECK(f.symbols.empty());
    CHECK(f.imports.empty());
    CHECK(f.api_routes.empty());
    CHECK(f.calls.empty());
}

static void testFileFilter()
{
    FileFilter filter;
    CHECK(filter.shouldMonitor("proj/main.py"));
    CHECK(filter.shouldMonitor("proj/app.tsx"));
    CHECK(filter.shouldMonitor("proj/lib.go"));
    CHECK(!filter.shouldMonitor("proj/node_modules/x.py"));
    CHECK(!filter.shouldMonitor("proj/.git/x.py"));
    CHECK(!filter.shouldMonitor("proj/notes.unknown"));
    CHECK(filter.shouldIgnore("proj/__pycache__/x.pyc"));
}

int main()
{
    g_base = fs::temp_directory_path() /
             ("codesight_tests_" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    testPython();
    testJavaScript();
    testTypeScript();
    testGo();
    testJava();
    testCSharp();
    testC();
    testCpp();
    testRust();
    testRuby();
    testPhp();
    testDataAsset();
    testFileFilter();

    std::error_code ec;
    fs::remove_all(g_base, ec);

    std::cout << (g_failures ? "FAILED " : "PASSED ")
              << (g_checks - g_failures) << "/" << g_checks << " checks\n";
    return g_failures ? 1 : 0;
}
