#!/usr/bin/env python3
"""Combined LAN search proxy for stock TasmoClaw.

Routes:
- /brave?q=...   -> Brave-shaped {"web":{"results":[...]}}
- /searxng?q=... -> SearXNG-shaped {"results":[...]}

This keeps stock ESP32 webclient traffic small and plain HTTP while the host
handles HTTPS and larger local SearXNG responses.
"""

from __future__ import annotations

import argparse
import json
import os
import ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlencode, urlsplit
from urllib.request import Request, urlopen


class TasmoClawSearchProxy(BaseHTTPRequestHandler):
    brave_key = ""
    brave_upstream = "https://api.search.brave.com/res/v1/web/search"
    searxng_upstream = "http://127.0.0.1:8888/search"
    verify_tls = True

    def query_text(self) -> str:
        return (parse_qs(urlsplit(self.path).query).get("q") or [""])[0]

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        path = urlsplit(self.path).path
        if path.startswith("/brave"):
            status, body = self.brave(self.query_text())
        elif path.startswith("/searxng") or path.startswith("/search"):
            status, body = self.searxng(self.query_text())
        else:
            status, body = 404, {"error": "Use /brave?q=... or /searxng?q=..."}

        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def brave(self, q: str) -> tuple[int, dict]:
        try:
            target = self.brave_upstream + "?" + urlencode({"q": q, "count": "5"})
            context = None if self.verify_tls else ssl._create_unverified_context()
            req = Request(
                target,
                headers={
                    "Accept": "application/json",
                    "X-Subscription-Token": self.brave_key,
                    "User-Agent": "TasmoClaw-Search-Proxy/0.1",
                },
            )
            raw = urlopen(req, timeout=20, context=context).read().decode(errors="replace")
            source = json.loads(raw)
            results = [
                {"title": item.get("title"), "url": item.get("url"), "description": item.get("description")}
                for item in (source.get("web", {}).get("results") or [])[:5]
            ]
            return 200, {"web": {"results": results}, "query": source.get("query", {})}
        except Exception as exc:  # pragma: no cover - operational bridge
            return 502, {"error": str(exc)}

    def searxng(self, q: str) -> tuple[int, dict]:
        try:
            target = self.searxng_upstream + "?" + urlencode({"q": q, "format": "json"})
            req = Request(target, headers={"Accept": "application/json", "User-Agent": "TasmoClaw-Search-Proxy/0.1"})
            raw = urlopen(req, timeout=30).read().decode(errors="replace")
            source = json.loads(raw)
            results = [
                {"title": item.get("title"), "url": item.get("url"), "content": item.get("content")}
                for item in (source.get("results") or [])[:5]
            ]
            return 200, {"results": results, "query": q}
        except Exception as exc:  # pragma: no cover - operational bridge
            return 502, {"error": str(exc)}

    def log_message(self, fmt: str, *args: object) -> None:
        print("search-proxy " + fmt % args)


def main() -> None:
    parser = argparse.ArgumentParser(description="TasmoClaw combined LAN search proxy")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--brave-key", default=os.environ.get("BRAVE_SEARCH_API_KEY", ""))
    parser.add_argument("--searxng-upstream", default="http://127.0.0.1:8888/search")
    parser.add_argument("--no-verify-tls", action="store_true")
    args = parser.parse_args()

    TasmoClawSearchProxy.brave_key = args.brave_key
    TasmoClawSearchProxy.searxng_upstream = args.searxng_upstream
    TasmoClawSearchProxy.verify_tls = not args.no_verify_tls
    ThreadingHTTPServer((args.host, args.port), TasmoClawSearchProxy).serve_forever()


if __name__ == "__main__":
    main()
