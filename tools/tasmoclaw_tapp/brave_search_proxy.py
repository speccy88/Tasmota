#!/usr/bin/env python3
"""Small LAN Brave Search bridge for stock TasmoClaw.

Stock Tasmota webclient builds can fail against Brave's HTTPS endpoint even
when local HTTP works. This proxy keeps the API key on the LAN host or accepts
the device-sent X-Subscription-Token header, calls Brave over host HTTPS, and
returns a compact Brave-shaped JSON response.
"""

from __future__ import annotations

import argparse
import json
import os
import ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlencode, urlsplit
from urllib.request import Request, urlopen


class BraveProxy(BaseHTTPRequestHandler):
    upstream = "https://api.search.brave.com/res/v1/web/search"
    api_key = ""
    verify_tls = True

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        query = parse_qs(urlsplit(self.path).query)
        q = (query.get("q") or [""])[0]
        count = (query.get("count") or ["5"])[0]
        token = self.api_key or self.headers.get("X-Subscription-Token", "")

        target = self.upstream + "?" + urlencode({"q": q, "count": count})
        status = 200
        content_type = "application/json"
        try:
            req = Request(
                target,
                headers={
                    "Accept": "application/json",
                    "X-Subscription-Token": token,
                    "User-Agent": "TasmoClaw-Brave-Proxy/0.1",
                },
            )
            context = None if self.verify_tls else ssl._create_unverified_context()
            raw = urlopen(req, timeout=20, context=context).read().decode(errors="replace")
            source = json.loads(raw)
            results = []
            for item in (source.get("web", {}).get("results") or [])[:5]:
                results.append(
                    {
                        "title": item.get("title"),
                        "url": item.get("url"),
                        "description": item.get("description"),
                    }
                )
            body = json.dumps({"web": {"results": results}, "query": source.get("query", {})}).encode()
        except Exception as exc:  # pragma: no cover - operational bridge
            status = 502
            body = json.dumps({"error": str(exc)}).encode()

        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args: object) -> None:
        print("brave-proxy " + fmt % args)


def main() -> None:
    parser = argparse.ArgumentParser(description="TasmoClaw Brave Search LAN proxy")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8767)
    parser.add_argument("--api-key", default=os.environ.get("BRAVE_SEARCH_API_KEY", ""))
    parser.add_argument("--no-verify-tls", action="store_true")
    args = parser.parse_args()

    BraveProxy.api_key = args.api_key
    BraveProxy.verify_tls = not args.no_verify_tls
    ThreadingHTTPServer((args.host, args.port), BraveProxy).serve_forever()


if __name__ == "__main__":
    main()
