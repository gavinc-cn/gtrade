"""从 arxiv.org 搜索量化策略相关论文。

使用 arxiv API v2（Atom/XML）：
  http://export.arxiv.org/api/query?search_query=...&max_results=N
"""

import xml.etree.ElementTree as ET
import requests
from urllib.parse import quote

ARXIV_API = "http://export.arxiv.org/api/query"
NS = {"atom": "http://www.w3.org/2005/Atom"}


def search_papers(
    query: str,
    max_results: int = 10,
    categories: list[str] | None = None,
) -> list[dict]:
    """搜索 arxiv 论文。

    Args:
        query: 搜索关键词，如 "cryptocurrency trading strategy momentum"
        max_results: 最多返回结果数
        categories: arxiv 分类过滤，如 ["q-fin.TR", "cs.AI"]

    Returns:
        list of {arxiv_id, title, abstract, pdf_url, published, source}
    """
    cat_filter = ""
    if categories:
        cat_filter = " AND (" + " OR ".join(f"cat:{c}" for c in categories) + ")"

    full_query = f"all:{quote(query)}{cat_filter}"
    params = {
        "search_query": full_query,
        "max_results": str(max_results),
        "sortBy": "submittedDate",
        "sortOrder": "descending",
    }
    resp = requests.get(ARXIV_API, params=params, timeout=15)
    resp.raise_for_status()

    root = ET.fromstring(resp.text)
    results = []
    for entry in root.findall("atom:entry", NS):
        arxiv_id = entry.find("atom:id", NS).text.split("/abs/")[-1]
        title = entry.find("atom:title", NS).text.strip().replace("\n", " ")
        abstract = entry.find("atom:summary", NS).text.strip().replace("\n", " ")
        published = entry.find("atom:published", NS).text[:10]
        pdf_url = f"https://arxiv.org/pdf/{arxiv_id}"
        results.append({
            "arxiv_id": arxiv_id,
            "title": title,
            "abstract": abstract,
            "pdf_url": pdf_url,
            "published": published,
            "source": f"arxiv:{arxiv_id}",
        })
    return results
