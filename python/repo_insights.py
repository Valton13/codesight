"""Repository fingerprinting: languages, frameworks, entry points, commands, CI, docs."""

import json
import os
import re
from pathlib import Path

try:
    import tomllib
except ImportError:  # pragma: no cover
    tomllib = None

IGNORE_DIRS = {
    "node_modules",
    "__pycache__",
    ".git",
    ".svn",
    ".hg",
    "venv",
    "env",
    "build",
    "dist",
    ".cache",
    ".pytest_cache",
    "target",
    "bin",
    "obj",
    ".next",
    ".nuxt",
    "vendor",
    ".idea",
    ".vscode",
    "coverage",
    ".terraform",
}

LANG_BY_EXT = {
    ".py": "Python",
    ".js": "JavaScript",
    ".jsx": "JavaScript",
    ".mjs": "JavaScript",
    ".cjs": "JavaScript",
    ".ts": "TypeScript",
    ".tsx": "TypeScript",
    ".mts": "TypeScript",
    ".cts": "TypeScript",
    ".c": "C",
    ".h": "C",
    ".cpp": "C++",
    ".cc": "C++",
    ".cxx": "C++",
    ".hpp": "C++",
    ".hh": "C++",
    ".hxx": "C++",
    ".go": "Go",
    ".java": "Java",
    ".cs": "C#",
    ".rs": "Rust",
    ".rb": "Ruby",
    ".php": "PHP",
    ".sql": "SQL",
    ".sh": "Shell",
    ".bash": "Shell",
    ".zsh": "Shell",
    ".css": "CSS",
    ".scss": "CSS",
    ".html": "HTML",
    ".csv": "Data",
    ".json": "Data",
    ".yaml": "Config",
    ".yml": "Config",
    ".toml": "Config",
    ".xml": "Config",
    ".md": "Docs",
}

FRAMEWORKS = [
    ("fastapi", "FastAPI", "backend"),
    ("flask", "Flask", "backend"),
    ("django", "Django", "backend"),
    ("starlette", "Starlette", "backend"),
    ("express", "Express", "backend"),
    ("@nestjs", "NestJS", "backend"),
    ("koa", "Koa", "backend"),
    ("fastify", "Fastify", "backend"),
    ("spring-boot", "Spring Boot", "backend"),
    ("gin-gonic", "Gin", "backend"),
    ("labstack/echo", "Echo", "backend"),
    ("gofiber", "Fiber", "backend"),
    ("rails", "Rails", "backend"),
    ("laravel", "Laravel", "backend"),
    ("actix-web", "Actix Web", "backend"),
    ("axum", "Axum", "backend"),
    ("react", "React", "frontend"),
    ("next", "Next.js", "frontend"),
    ("vue", "Vue", "frontend"),
    ("svelte", "Svelte", "frontend"),
    ("@angular/core", "Angular", "frontend"),
    ("vite", "Vite", "frontend"),
    ("tailwindcss", "Tailwind", "frontend"),
    ("sqlalchemy", "SQLAlchemy", "data"),
    ("prisma", "Prisma", "data"),
    ("typeorm", "TypeORM", "data"),
    ("mongoose", "Mongoose", "data"),
    ("psycopg", "PostgreSQL", "data"),
    ("pymongo", "MongoDB", "data"),
    ("redis", "Redis", "data"),
    ("celery", "Celery", "data"),
    ("kafka", "Kafka", "data"),
    ("pika", "RabbitMQ", "data"),
    ("torch", "PyTorch", "ml"),
    ("tensorflow", "TensorFlow", "ml"),
    ("scikit-learn", "scikit-learn", "ml"),
    ("pandas", "pandas", "ml"),
    ("grpc", "gRPC", "infra"),
    ("docker", "Docker", "infra"),
    ("kubernetes", "Kubernetes", "infra"),
    ("boto3", "AWS", "infra"),
]

ENTRY_NAMES = {
    "main.py",
    "app.py",
    "server.py",
    "manage.py",
    "asgi.py",
    "wsgi.py",
    "__main__.py",
    "index.js",
    "index.ts",
    "server.js",
    "server.ts",
    "app.js",
    "app.ts",
    "main.ts",
    "main.go",
    "main.c",
    "main.cpp",
    "main.rs",
    "lib.rs",
    "Main.java",
    "Application.java",
    "Program.cs",
    "index.php",
    "artisan",
}

TEST_RE = re.compile(
    r"(^|/)(tests?|__tests__|spec)(/|$)|(^|/)test_[^/]+$|_test\.[a-z]+$|"
    r"\.(test|spec)\.[a-z]+$|Test\.java$|Tests\.cs$|_test\.go$",
    re.IGNORECASE,
)


def iter_files(root: Path):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [
            d for d in dirnames if d not in IGNORE_DIRS and not d.startswith(".")
        ]
        for name in filenames:
            yield Path(dirpath) / name


def _read(path: Path, limit: int = 1_000_000) -> str:
    try:
        if path.stat().st_size > limit:
            return ""
        return path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return ""


def _detect_license(root: Path) -> str | None:
    for name in ("LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING", "license"):
        p = root / name
        if p.exists():
            head = _read(p, 4000).lower()
            for key, label in (
                ("mit license", "MIT"),
                ("apache license", "Apache-2.0"),
                ("gnu affero", "AGPL-3.0"),
                ("gnu general public", "GPL"),
                ("gnu lesser", "LGPL"),
                ("bsd", "BSD"),
                ("mozilla public", "MPL-2.0"),
                ("unlicense", "Unlicense"),
            ):
                if key in head:
                    return label
            return "Present"
    return None


def analyze_repo(
    root: Path, imports_by_file: dict[str, list[str]] | None = None
) -> dict:
    root = Path(root)
    files = list(iter_files(root))

    lang_files: dict[str, int] = {}
    lang_loc: dict[str, int] = {}
    test_files = 0
    total_loc = 0

    for path in files:
        ext = path.suffix.lower()
        lang = LANG_BY_EXT.get(ext)
        rel = str(path.relative_to(root))
        if TEST_RE.search(rel):
            test_files += 1
        if not lang:
            continue
        lang_files[lang] = lang_files.get(lang, 0) + 1
        if lang in ("Docs", "Data", "Config"):
            continue
        text = _read(path)
        loc = text.count("\n") + 1 if text else 0
        lang_loc[lang] = lang_loc.get(lang, 0) + loc
        total_loc += loc

    languages = [
        {
            "name": name,
            "files": count,
            "loc": lang_loc.get(name, 0),
            "percent": round(100 * lang_loc.get(name, 0) / total_loc, 1)
            if total_loc
            else 0.0,
        }
        for name, count in sorted(lang_files.items(), key=lambda kv: -kv[1])
    ]

    # ---- manifests / package managers ----
    managers: list[str] = []
    dep_names: list[str] = []
    dev_count = 0
    scripts: dict[str, str] = {}
    workspaces: list[str] = []

    pkg = root / "package.json"
    if pkg.exists():
        managers.append("npm")
        try:
            data = json.loads(_read(pkg) or "{}")
            deps = list((data.get("dependencies") or {}).keys())
            dev = list((data.get("devDependencies") or {}).keys())
            dep_names += deps + dev
            dev_count += len(dev)
            scripts = data.get("scripts") or {}
            ws = data.get("workspaces")
            if isinstance(ws, list):
                workspaces += ws
            elif isinstance(ws, dict):
                workspaces += ws.get("packages") or []
        except json.JSONDecodeError:
            pass

    for manifest, manager in (
        ("pyproject.toml", "poetry/pip"),
        ("requirements.txt", "pip"),
        ("go.mod", "go modules"),
        ("Cargo.toml", "cargo"),
        ("pom.xml", "maven"),
        ("build.gradle", "gradle"),
        ("composer.json", "composer"),
        ("Gemfile", "bundler"),
    ):
        if (root / manifest).exists():
            managers.append(manager)

    req = root / "requirements.txt"
    if req.exists():
        for line in _read(req).splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                dep_names.append(re.split(r"[=<>!\[]", line)[0].strip())

    pyproject = root / "pyproject.toml"
    if pyproject.exists() and tomllib:
        try:
            data = tomllib.loads(_read(pyproject) or "")
            project = data.get("project") or {}
            dep_names += [
                re.split(r"[=<>!\[]", d)[0].strip()
                for d in project.get("dependencies", [])
            ]
            poetry = (data.get("tool") or {}).get("poetry") or {}
            dep_names += list((poetry.get("dependencies") or {}).keys())
        except Exception:
            pass

    gomod = root / "go.mod"
    if gomod.exists():
        dep_names += re.findall(r"^\s*([\w./\-]+)\s+v", _read(gomod), re.MULTILINE)

    cargo = root / "Cargo.toml"
    if cargo.exists() and tomllib:
        try:
            data = tomllib.loads(_read(cargo) or "")
            dep_names += list((data.get("dependencies") or {}).keys())
            if (data.get("workspace") or {}).get("members"):
                workspaces += data["workspace"]["members"]
        except Exception:
            pass

    # ---- frameworks ----
    haystack = " ".join(dep_names).lower()
    if imports_by_file:
        sample = []
        for mods in list(imports_by_file.values())[:400]:
            sample.extend(mods)
        haystack += " " + " ".join(sample).lower()

    frameworks = []
    seen = set()
    for needle, label, kind in FRAMEWORKS:
        if needle in haystack and label not in seen:
            seen.add(label)
            frameworks.append({"name": label, "kind": kind})

    # ---- entry points ----
    entry_points = []
    for path in files:
        rel = str(path.relative_to(root))
        if path.name in ENTRY_NAMES or re.match(r"^cmd/[^/]+/main\.go$", rel):
            entry_points.append(rel)
    entry_points = sorted(entry_points)[:20]

    # ---- commands ----
    commands = {"run": [], "build": [], "test": [], "lint": []}
    for name, cmd in scripts.items():
        low = name.lower()
        if low in ("dev", "start", "serve"):
            commands["run"].append(f"npm run {name}")
        elif "build" in low:
            commands["build"].append(f"npm run {name}")
        elif "test" in low or low == "jest":
            commands["test"].append(f"npm run {name}")
        elif "lint" in low or "format" in low:
            commands["lint"].append(f"npm run {name}")

    makefile = root / "Makefile"
    if makefile.exists():
        for target in re.findall(
            r"^([a-zA-Z0-9_.\-]+):", _read(makefile), re.MULTILINE
        ):
            if target in ("build", "test", "lint", "run", "dev"):
                commands.setdefault(target, []).append(f"make {target}")

    if (
        (root / "pytest.ini").exists()
        or (root / "tox.ini").exists()
        or any(p.name.startswith("test_") for p in files[:500])
    ):
        commands["test"].append("pytest")
    if (root / "go.mod").exists():
        commands["build"].append("go build ./...")
        commands["test"].append("go test ./...")
    if (root / "Cargo.toml").exists():
        commands["build"].append("cargo build")
        commands["test"].append("cargo test")
    if (root / "pom.xml").exists():
        commands["build"].append("mvn package")
    if (root / "manage.py").exists():
        commands["run"].append("python manage.py runserver")

    for key in commands:
        commands[key] = sorted(set(commands[key]))[:6]

    # ---- CI ----
    ci = []
    workflows = root / ".github" / "workflows"
    if workflows.exists():
        ci += [str(p.relative_to(root)) for p in workflows.glob("*.y*ml")]
    for name in (".gitlab-ci.yml", "Jenkinsfile", ".travis.yml", "azure-pipelines.yml"):
        if (root / name).exists():
            ci.append(name)

    # ---- monorepo ----
    mono_tool = None
    for marker, tool in (
        ("pnpm-workspace.yaml", "pnpm workspaces"),
        ("turbo.json", "Turborepo"),
        ("nx.json", "Nx"),
        ("lerna.json", "Lerna"),
        ("go.work", "Go workspace"),
    ):
        if (root / marker).exists():
            mono_tool = tool
            break
    if not mono_tool and workspaces:
        mono_tool = "npm workspaces"

    return {
        "root": str(root),
        "files": len(files),
        "loc": total_loc,
        "languages": languages[:12],
        "frameworks": frameworks,
        "package_managers": sorted(set(managers)),
        "entry_points": entry_points,
        "commands": commands,
        "ci": ci,
        "license": _detect_license(root),
        "docs": {
            "readme": any(p.name.lower().startswith("readme") for p in files),
            "docs_dir": (root / "docs").is_dir(),
            "examples": (root / "examples").is_dir(),
        },
        "monorepo": {
            "detected": bool(mono_tool),
            "tool": mono_tool,
            "workspaces": workspaces[:12],
        },
        "tests": {"files": test_files},
        "dependencies": {
            "direct": max(0, len(set(dep_names)) - dev_count),
            "dev": dev_count,
            "names": sorted(set(dep_names))[:60],
        },
    }
