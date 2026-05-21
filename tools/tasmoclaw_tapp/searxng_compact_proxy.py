#!/usr/bin/env python3
"""Compact SearXNG JSON bridge for stock TasmoClaw.

Some stock Tasmota webclient builds struggle with larger/chunked SearXNG JSON
responses. This bridge calls a local SearXNG instance from the LAN host and
returns only the first few title/url/content fields.
"""

from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlencode, urlsplit
from urllib.request import Request, urlopen


class SearxngCompactProxy(BaseHTTPRequestHandler):
    upstream = "http://127.0.0.1:8888/search"

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        query = parse_qs(urlsplit(self.path).query)
        q = (query.get("q") or [""])[0]
        target = self.upstream + "?" + urlencode({"q": q, "format": "json"})
        status = 200
        try:
            req = Request(target, headers={"Accept": "application/json", "User-Agent": "TasmoClaw-SearXNG-Proxy/0.1"})
            raw = urlopen(req, timeout=30).read().decode(errors="replace")
            source = json.loads(raw)
            results = []
            for item in (source.get("results") or [])[:5]:
                results.append(
                    {
                        "title": item.get("title"),
                        "url": item.get("url"),
                        "content": item.get("content"),
                    }
                )
            body = json.dumps({"results": results, "query": q}).encode()
        except Exception as exc:  # pragma: no cover - operational bridge
            status = 502
            body = json.dumps({"error": str(exc)}).encode()

        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args: object) -> None:
        print("searxng-proxy " + fmt % args)


def main() -> None:
    parser = argparse.ArgumentParser(description="TasmoClaw compact SearXNG LAN proxy")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8768)
    parser.add_argument("--upstream", default="http://127.0.0.1:8888/search")
    args = parser.parse_args()

    SearxngCompactProxy.upstream = args.upstream
    ThreadingHTTPServer((args.host, args.port), SearxngCompactProxy).serve_forever()


if __name__ == "__main__":
    main()
