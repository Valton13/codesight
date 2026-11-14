import json

from repo_insights import analyze_repo


def build_repo(root):
    (root / "package.json").write_text(
        json.dumps(
            {
                "name": "demo",
                "scripts": {
                    "dev": "vite",
                    "build": "vite build",
                    "test": "vitest",
                    "lint": "eslint .",
                },
                "dependencies": {"react": "^18.0.0", "express": "^4.18.0"},
                "devDependencies": {"vite": "^5.0.0"},
            }
        ),
        encoding="utf-8",
    )
    (root / "requirements.txt").write_text("fastapi\nuvicorn\n", encoding="utf-8")
    (root / "main.py").write_text("import fastapi\n", encoding="utf-8")
    (root / "tests").mkdir()
    (root / "tests" / "test_main.py").write_text(
        "def test_x():\n    assert True\n", encoding="utf-8"
    )
    (root / "README.md").write_text("# demo\n", encoding="utf-8")
    (root / "LICENSE").write_text("MIT License\n", encoding="utf-8")
    (root / ".github" / "workflows").mkdir(parents=True)
    (root / ".github" / "workflows" / "ci.yml").write_text(
        "name: ci\n", encoding="utf-8"
    )


def test_overview_fingerprint(tmp_path):
    build_repo(tmp_path)
    ov = analyze_repo(tmp_path)

    assert ov["files"] > 0
    assert any(l["name"] == "Python" for l in ov["languages"])

    frameworks = {f["name"] for f in ov["frameworks"]}
    assert {"React", "Express", "FastAPI"} <= frameworks

    assert "npm" in ov["package_managers"]
    assert any("pip" in m for m in ov["package_managers"])

    assert "main.py" in ov["entry_points"]

    assert ov["commands"]["run"]
    assert ov["commands"]["build"]
    assert ov["commands"]["test"]
    assert ov["commands"]["lint"]

    assert any("ci.yml" in c for c in ov["ci"])
    assert ov["license"] == "MIT"
    assert ov["docs"]["readme"] is True
    assert ov["tests"]["files"] >= 1
    assert ov["dependencies"]["direct"] >= 2
    assert ov["dependencies"]["dev"] >= 1


def test_monorepo_detection(tmp_path):
    (tmp_path / "package.json").write_text(
        json.dumps(
            {
                "name": "root",
                "private": True,
                "workspaces": ["packages/*"],
            }
        ),
        encoding="utf-8",
    )
    ov = analyze_repo(tmp_path)
    assert ov["monorepo"]["detected"] is True
    assert ov["monorepo"]["tool"] == "npm workspaces"
