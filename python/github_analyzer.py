"""GitHub repository acquisition for CodeSight.

Validates a GitHub URL, shallow-clones the repository into a local cache,
and hands the directory to the C++ scan engine.
"""

import os
import re
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHE_DIR = ROOT / ".repo_cache"

GITHUB_RE = re.compile(
    r"^(?:https?://)?(?:www\.)?github\.com/"
    r"(?P<owner>[\w.\-]+)/(?P<repo>[\w.\-]+?)(?:\.git)?"
    r"(?:/tree/(?P<branch>[\w.\-/]+))?/?$"
)
SHORT_RE = re.compile(r"^(?P<owner>[\w.\-]+)/(?P<repo>[\w.\-]+)$")


def parse_github_url(url: str) -> tuple[str, str, str | None]:
    """Return (owner, repo, branch|None) or raise ValueError."""
    url = url.strip()
    m = GITHUB_RE.match(url) or SHORT_RE.match(url)
    if not m:
        raise ValueError(
            "Not a valid GitHub repository. Use https://github.com/owner/repo "
            "or owner/repo."
        )
    owner = m.group("owner")
    repo = m.group("repo")
    repo = repo.removesuffix(".git")
    branch = m.groupdict().get("branch")
    return owner, repo, branch


def clone_repo(owner: str, repo: str, branch: str | None = None) -> Path:
    """Shallow-clone the repo into the cache. Raises RuntimeError on failure."""
    CACHE_DIR.mkdir(exist_ok=True)

    # Keep only the three most recent clones to bound disk usage.
    existing = sorted(
        (p for p in CACHE_DIR.iterdir() if p.is_dir()),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    for old in existing[3:]:
        shutil.rmtree(old, ignore_errors=True)

    dest = CACHE_DIR / f"{owner}__{repo}__{int(time.time())}"
    cmd = ["git", "clone", "--depth", "1", "--single-branch"]
    if branch:
        cmd += ["--branch", branch]
    cmd += [f"https://github.com/{owner}/{repo}.git", str(dest)]

    env = os.environ.copy()
    env["GIT_TERMINAL_PROMPT"] = "0"  # never hang waiting for credentials

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120, env=env)
    except subprocess.TimeoutExpired:
        shutil.rmtree(dest, ignore_errors=True)
        raise RuntimeError("git clone timed out after 120s.")

    if proc.returncode != 0:
        shutil.rmtree(dest, ignore_errors=True)
        lines = (proc.stderr or "").strip().splitlines()
        detail = lines[-1] if lines else "unknown error"
        raise RuntimeError(f"git clone failed: {detail}")

    return dest
