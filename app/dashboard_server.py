"""Local browser dashboard for the deterministic host simulator."""

from __future__ import annotations

import argparse
import csv
import io
import json
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
HTML_PATH = Path(__file__).resolve().with_name("dashboard.html")
SIMULATOR = ROOT / "build" / ("whm_simulator.exe" if sys.platform.startswith("win") else "whm_simulator")
ALLOWED_MODES = {"nominal", "motion", "nocontact"}
STATE_LABELS = {
    0: "初始化",
    1: "等待接触",
    2: "建立基线",
    3: "有效测量",
    4: "低质量",
    5: "充电",
    6: "故障",
}


def _number(value: str, as_int: bool = False):
    return int(value) if as_int else float(value)


def parse_simulator_csv(csv_text: str) -> dict:
    rows = []
    for raw in csv.DictReader(io.StringIO(csv_text)):
        row = {
            "mode": raw["mode"],
            "sequence": int(raw["sequence"]),
            "timestamp_ms": int(raw["timestamp_ms"]),
            "state": int(raw["state"]),
            "state_label": STATE_LABELS.get(int(raw["state"]), "未知状态"),
            "heart_rate_bpm": float(raw["heart_rate_bpm"]),
            "quality": int(raw["quality"]),
            "motion_mg": int(raw["motion_mg"]),
            "battery_mv": int(raw["battery_mv"]),
            "flags": int(raw["flags"]),
        }
        rows.append(row)
    if not rows:
        raise ValueError("仿真程序没有返回结果包")
    latest = rows[-1]
    return {
        "mode": latest["mode"],
        "state_labels": STATE_LABELS,
        "latest": latest,
        "rows": rows,
    }


def build_dashboard_payload(mode: str = "nominal", seconds: int = 12) -> dict:
    if mode not in ALLOWED_MODES:
        raise ValueError(f"不支持的仿真模式: {mode}")
    if seconds < 8 or seconds > 60:
        raise ValueError("仿真时长应在 8 到 60 秒之间")
    if not SIMULATOR.exists():
        raise FileNotFoundError("未找到 build/whm_simulator.exe，请先运行 make simulator")
    completed = subprocess.run(
        [str(SIMULATOR), "--seconds", str(seconds), "--mode", mode],
        cwd=ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=True,
    )
    return parse_simulator_csv(completed.stdout)


class DashboardHandler(BaseHTTPRequestHandler):
    def _send(self, status: int, content_type: str, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self._send(200, "text/html; charset=utf-8", HTML_PATH.read_bytes())
            return
        if parsed.path == "/api/run":
            query = parse_qs(parsed.query)
            mode = query.get("mode", ["nominal"])[0]
            try:
                seconds = int(query.get("seconds", ["12"])[0])
                payload = build_dashboard_payload(mode, seconds)
                self._send(200, "application/json; charset=utf-8", json.dumps(payload, ensure_ascii=False).encode("utf-8"))
            except (ValueError, FileNotFoundError, subprocess.CalledProcessError) as exc:
                self._send(400, "application/json; charset=utf-8", json.dumps({"error": str(exc)}, ensure_ascii=False).encode("utf-8"))
            return
        self._send(404, "text/plain; charset=utf-8", b"Not found")

    def log_message(self, fmt: str, *args) -> None:
        print(f"[dashboard] {fmt % args}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the local wearable health monitor dashboard")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), DashboardHandler)
    print(f"Dashboard: http://{args.host}:{args.port}/")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nDashboard stopped.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
