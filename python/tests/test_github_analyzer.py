import pytest
from github_analyzer import parse_github_url


@pytest.mark.parametrize(
    "url,expected",
    [
        ("https://github.com/owner/repo", ("owner", "repo", None)),
        ("https://github.com/owner/repo.git", ("owner", "repo", None)),
        ("https://github.com/owner/repo/", ("owner", "repo", None)),
        ("http://github.com/owner/repo", ("owner", "repo", None)),
        ("github.com/owner/repo", ("owner", "repo", None)),
        ("owner/repo", ("owner", "repo", None)),
        ("https://github.com/owner/repo/tree/main", ("owner", "repo", "main")),
        (
            "https://github.com/owner/repo/tree/feature/x",
            ("owner", "repo", "feature/x"),
        ),
    ],
)
def test_valid_urls(url, expected):
    assert parse_github_url(url) == expected


@pytest.mark.parametrize(
    "url",
    [
        "",
        "not a url",
        "https://gitlab.com/owner/repo",
        "https://github.com/owner",
        "https://example.com/owner/repo",
    ],
)
def test_invalid_urls(url):
    with pytest.raises(ValueError):
        parse_github_url(url)
