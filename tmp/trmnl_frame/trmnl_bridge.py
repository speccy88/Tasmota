#!/usr/bin/env python3
"""Small local TRMNL bridge for Tasmota boards with limited HTTPS support."""

from __future__ import annotations

import argparse
import json
import os
import socket
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request
from io import BytesIO
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


DISPLAY_URL = "https://trmnl.com/api/display"

site_packages = Path(sys.executable).resolve().parents[1] / "lib" / f"python{sys.version_info.major}.{sys.version_info.minor}" / "site-packages"
if site_packages.exists() and str(site_packages) not in sys.path:
    sys.path.append(str(site_packages))


def tls_context() -> ssl.SSLContext:
    try:
        import certifi

        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def lan_ip() -> str:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    finally:
        sock.close()


def load_config(path: Path | None) -> dict[str, str]:
    cfg: dict[str, str] = {}
    if path and path.exists():
        with path.open("r", encoding="utf-8") as handle:
            loaded = json.load(handle)
        if not isinstance(loaded, dict):
            raise ValueError(f"{path} must contain a JSON object")
        cfg.update({str(k): str(v) for k, v in loaded.items()})

    env_map = {
        "id": "TRMNL_ID",
        "token": "TRMNL_TOKEN",
    }
    for key, env_name in env_map.items():
        value = os.environ.get(env_name)
        if value:
            cfg[key] = value

    if not cfg.get("id") or not cfg.get("token"):
        raise ValueError("TRMNL id/token are required in config or TRMNL_ID/TRMNL_TOKEN")
    return cfg


class BridgeHandler(BaseHTTPRequestHandler):
    server: "BridgeServer"

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stderr.write("%s - %s\n" % (self.log_date_time_string(), fmt % args))

    def send_plain(self, code: int, message: str) -> None:
        body = message.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        parsed = urllib.parse.urlsplit(self.path)
        if parsed.path == "/api/display":
            self.handle_display()
        elif parsed.path == "/image":
            self.handle_image(parsed.query)
        elif parsed.path == "/health":
            self.send_plain(200, "ok\n")
        else:
            self.send_plain(404, "not found\n")

    def handle_display(self) -> None:
        headers = {
            "User-Agent": "curl/8.7.1",
            "Accept": "*/*",
            "ID": self.server.trmnl_id,
            "Access-Token": self.server.trmnl_token,
            "Refresh-Rate": self.headers.get("Refresh-Rate", "300"),
            "Battery-Voltage": self.headers.get("Battery-Voltage", "4.2"),
            "FW-Version": self.headers.get("FW-Version", "tasmota-lvgl-trmnl"),
            "RSSI": self.headers.get("RSSI", "100"),
            "Width": self.headers.get("Width", "800"),
            "Height": self.headers.get("Height", "600"),
        }
        request = urllib.request.Request(DISPLAY_URL, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=25, context=self.server.tls) as response:
                body = response.read()
        except urllib.error.HTTPError as exc:
            self.send_plain(exc.code, exc.read().decode("utf-8", "replace"))
            return
        except OSError as exc:
            self.send_plain(502, f"display api error: {exc}\n")
            return

        try:
            payload = json.loads(body.decode("utf-8"))
            image_url = payload.get("image_url")
            if image_url:
                payload["image_url"] = self.server.public_image_url(image_url)
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        except (UnicodeDecodeError, json.JSONDecodeError, TypeError):
            pass

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def handle_image(self, query: str) -> None:
        params = urllib.parse.parse_qs(query)
        image_url = params.get("url", [None])[0]
        if not image_url:
            self.send_plain(400, "missing url\n")
            return

        request = urllib.request.Request(image_url, headers={"User-Agent": "tasmota-trmnl-bridge"})
        try:
            with urllib.request.urlopen(request, timeout=30, context=self.server.tls) as response:
                body = response.read()
                content_type = response.headers.get("Content-Type", "image/png")
        except urllib.error.HTTPError as exc:
            self.send_plain(exc.code, exc.read().decode("utf-8", "replace"))
            return
        except OSError as exc:
            self.send_plain(502, f"image proxy error: {exc}\n")
            return

        if content_type.startswith("image/png"):
            try:
                from PIL import Image

                image = Image.open(BytesIO(body)).convert("1")
                image = image.resize((max(1, image.width // 2), max(1, image.height // 2)))
                body = f"P4\n{image.width} {image.height}\n".encode("ascii") + image.tobytes()
                content_type = "image/x-portable-bitmap"
            except Exception as exc:
                self.send_plain(502, f"image conversion error: {exc}\n")
                return

        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class BridgeServer(ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], handler, cfg: dict[str, str], public_host: str):
        super().__init__(address, handler)
        self.trmnl_id = cfg["id"]
        self.trmnl_token = cfg["token"]
        self.public_host = public_host
        self.tls = tls_context()

    def public_image_url(self, image_url: str) -> str:
        encoded = urllib.parse.urlencode({"url": image_url})
        return f"http://{self.public_host}/image?{encoded}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", default=8765, type=int)
    parser.add_argument("--public-host", default=None, help="host[:port] reachable by the ESP")
    parser.add_argument("--config", type=Path, default=Path.home() / ".config/trmnl_tasmota/config.json")
    args = parser.parse_args()

    cfg = load_config(args.config)
    public_host = args.public_host or f"{lan_ip()}:{args.port}"
    server = BridgeServer((args.host, args.port), BridgeHandler, cfg, public_host)
    print(f"TRMNL bridge listening on http://{public_host}/api/display", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
