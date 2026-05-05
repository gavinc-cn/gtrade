"""从 GitHub 搜索量化策略代码。

使用 GitHub REST API /search/repositories。
设置环境变量 GITHUB_TOKEN 以提升速率限制（可选）。
"""

import os
import requests

GITHUB_API = "https://api.github.com"


def _headers() -> dict:
    h = {"Accept": "application/vnd.github+json"}
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        h["Authorization"] = f"Bearer {token}"
    return h


def search_strategy_repos(
    query: str,
    max_results: int = 5,
) -> list[dict]:
    """搜索包含量化策略的 GitHub 仓库。

    Returns:
        list of {repo_full_name, description, stars, url, main_language, source}
    """
    params = {
        "q": f"{query} topic:quantitative-finance language:python",
        "sort": "stars",
        "order": "desc",
        "per_page": str(max_results),
    }
    resp = requests.get(f"{GITHUB_API}/search/repositories",
                        params=params, headers=_headers(), timeout=15)
    resp.raise_for_status()
    items = resp.json().get("items", [])
    return [{
        "repo_full_name": r["full_name"],
        "description": r.get("description", ""),
        "stars": r["stargazers_count"],
        "url": r["html_url"],
        "main_language": r.get("language", ""),
        "source": f"github:{r['full_name']}",
    } for r in items]


def fetch_file_content(repo_full_name: str, file_path: str) -> str:
    """获取 GitHub 仓库中某个文件的原始内容（先试 main，再试 master）。"""
    url = f"https://raw.githubusercontent.com/{repo_full_name}/main/{file_path}"
    resp = requests.get(url, timeout=10)
    if resp.status_code == 404:
        url = url.replace("/main/", "/master/")
        resp = requests.get(url, timeout=10)
    resp.raise_for_status()
    return resp.text
