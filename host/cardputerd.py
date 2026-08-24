#!/usr/bin/env python3
"""cardputerd — the Mac-side half of CardputerOS.

Gives the handheld three things it cannot do alone:

  * Claude on your Max subscription, via the `claude` CLI, instead of API credits
  * Claude Code with real tools, running in a real project directory
  * your Obsidian vault, as an actual filesystem it can read and append to

Plus local speech-to-text when whisper.cpp or faster-whisper is installed,
falling back to the OpenAI API.

    python3 cardputerd.py                 # uses config.json next to this file
    python3 cardputerd.py --config path   # or point it somewhere else

The daemon advertises itself over Bonjour as _cardputerd._tcp so the device's
Settings > "Find Mac" can locate it without you typing an IP.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

VERSION = "0.1.0"
HERE = Path(__file__).resolve().parent

DEFAULTS = {
    "port": 8787,
    "claude_bin": "claude",
    "default_project": str(Path.home()),
    "vault": "",                       # absolute path to your Obsidian vault
    "vault_subdir": "Cardputer",       # device notes land here
    "daily_dir": "Daily",              # where daily notes live inside the vault
    "daily_format": "%Y-%m-%d",
    "system_prompt": (
        "You are the assistant inside CardputerOS on a pocket handheld with a "
        "240x135 screen. Reply in PLAIN TEXT only - no markdown, no code fences, "
        "no bullet lists. Be sharp and concise."
    ),
    "claude_timeout": 120,
    "code_timeout": 900,
    "stt": "auto",                     # auto | whisper_cpp | faster_whisper | openai
    "whisper_cpp_bin": "whisper-cli",
    "whisper_cpp_model": "",           # e.g. ~/models/ggml-base.en.bin
    "faster_whisper_model": "base.en",
    "openai_api_key": "",              # or set OPENAI_API_KEY in the environment
    "advertise": True,
}


def load_config(path: Path) -> dict:
    cfg = dict(DEFAULTS)
    if path.exists():
        cfg.update(json.loads(path.read_text()))
    cfg["claude_bin"] = shutil.which(cfg["claude_bin"]) or cfg["claude_bin"]
    if not cfg["openai_api_key"]:
        cfg["openai_api_key"] = os.environ.get("OPENAI_API_KEY", "")
    return cfg


CFG: dict = dict(DEFAULTS)


def log(*a):
    print(dt.datetime.now().strftime("%H:%M:%S"), *a, flush=True)


# --------------------------------------------------------------------------
# Claude
# --------------------------------------------------------------------------
def run_claude(prompt: str, cwd: str, timeout: int, extra_args: list[str] | None = None) -> str:
    cmd = [CFG["claude_bin"], "-p", prompt]
    if extra_args:
        cmd[1:1] = extra_args
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    except FileNotFoundError:
        return f"[claude CLI not found at {CFG['claude_bin']}]"
    except subprocess.TimeoutExpired:
        return f"[timed out after {timeout}s]"
    out = (r.stdout or "").strip()
    if out:
        return out
    err = (r.stderr or "").strip()
    return f"[claude produced no output] {err[:200]}"


def handle_ask(data: dict) -> dict:
    prompt = (data.get("prompt") or "").strip()
    if not prompt:
        return {"response": "(empty prompt)"}
    system = data.get("system") or CFG["system_prompt"]
    full = f"{system}\n\n{prompt}"
    log(f"ask> {prompt[:80]}")
    # Chat runs read-only in a scratch dir: no tools, no surprises.
    text = run_claude(full, tempfile.gettempdir(), CFG["claude_timeout"])
    log(f"ask< {text[:80]}")
    return {"response": text}


def handle_code(data: dict) -> dict:
    prompt = (data.get("prompt") or "").strip()
    if not prompt:
        return {"response": "(empty prompt)"}
    project = (data.get("project") or "").strip() or CFG["default_project"]
    project_path = Path(project).expanduser()
    if not project_path.is_dir():
        return {"error": f"no such directory: {project}"}
    log(f"code> [{project_path}] {prompt[:70]}")
    text = run_claude(prompt, str(project_path), CFG["code_timeout"])
    log(f"code< {text[:80]}")
    return {"response": text}


# --------------------------------------------------------------------------
# Speech to text
# --------------------------------------------------------------------------
def stt_whisper_cpp(wav: Path) -> str | None:
    binary = shutil.which(CFG["whisper_cpp_bin"])
    model = CFG["whisper_cpp_model"]
    if not binary or not model or not Path(model).expanduser().exists():
        return None
    try:
        r = subprocess.run(
            [binary, "-m", str(Path(model).expanduser()), "-f", str(wav), "-nt", "-np"],
            capture_output=True, text=True, timeout=180,
        )
        return (r.stdout or "").strip() or None
    except Exception as e:  # noqa: BLE001 - any failure just means "try the next backend"
        log("whisper.cpp failed:", e)
        return None


def stt_faster_whisper(wav: Path) -> str | None:
    try:
        from faster_whisper import WhisperModel  # type: ignore
    except ImportError:
        return None
    global _FW_MODEL
    try:
        if _FW_MODEL is None:
            log("loading faster-whisper", CFG["faster_whisper_model"])
            _FW_MODEL = WhisperModel(CFG["faster_whisper_model"], device="auto", compute_type="int8")
        segments, _ = _FW_MODEL.transcribe(str(wav), language="en")
        return " ".join(s.text.strip() for s in segments).strip() or None
    except Exception as e:  # noqa: BLE001
        log("faster-whisper failed:", e)
        return None


_FW_MODEL = None


def stt_openai(wav: Path) -> str | None:
    key = CFG["openai_api_key"]
    if not key:
        return None
    import urllib.request

    boundary = "----cardputerd-boundary"
    body = bytearray()

    def field(name: str, value: str):
        body.extend(f"--{boundary}\r\n".encode())
        body.extend(f'Content-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n'.encode())

    field("model", "whisper-1")
    field("response_format", "text")
    body.extend(f"--{boundary}\r\n".encode())
    body.extend(b'Content-Disposition: form-data; name="file"; filename="r.wav"\r\n')
    body.extend(b"Content-Type: audio/wav\r\n\r\n")
    body.extend(wav.read_bytes())
    body.extend(f"\r\n--{boundary}--\r\n".encode())

    req = urllib.request.Request(
        "https://api.openai.com/v1/audio/transcriptions",
        data=bytes(body),
        headers={
            "Authorization": f"Bearer {key}",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            return resp.read().decode("utf-8", "replace").strip() or None
    except Exception as e:  # noqa: BLE001
        log("openai stt failed:", e)
        return None


def transcribe(wav_bytes: bytes) -> tuple[str | None, str]:
    """Returns (text, backend_name). Tries local engines before paying for one."""
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        f.write(wav_bytes)
        wav = Path(f.name)
    try:
        order = {
            "auto": [("whisper_cpp", stt_whisper_cpp),
                     ("faster_whisper", stt_faster_whisper),
                     ("openai", stt_openai)],
            "whisper_cpp": [("whisper_cpp", stt_whisper_cpp)],
            "faster_whisper": [("faster_whisper", stt_faster_whisper)],
            "openai": [("openai", stt_openai)],
        }[CFG["stt"]]
        for label, fn in order:
            text = fn(wav)
            if text:
                return text, label
        return None, "none"
    finally:
        wav.unlink(missing_ok=True)


# --------------------------------------------------------------------------
# Obsidian vault
# --------------------------------------------------------------------------
def vault_root() -> Path | None:
    v = CFG["vault"]
    if not v:
        return None
    p = Path(v).expanduser()
    return p if p.is_dir() else None


def safe_vault_path(rel: str) -> Path:
    """Resolve `rel` inside the vault, refusing anything that escapes it."""
    root = vault_root()
    if root is None:
        raise ValueError("no vault configured")
    target = (root / rel.lstrip("/")).resolve()
    if not str(target).startswith(str(root.resolve())):
        raise ValueError("path escapes the vault")
    return target


def handle_vault_note(data: dict) -> dict:
    rel = (data.get("path") or "").strip()
    content = data.get("content") or ""
    append = bool(data.get("append"))
    if not rel:
        return {"error": "no path"}
    if not rel.endswith(".md"):
        rel += ".md"
    try:
        target = safe_vault_path(rel)
    except ValueError as e:
        return {"error": str(e)}
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("a" if append else "w", encoding="utf-8") as f:
        f.write(content)
    log(f"vault {'append' if append else 'write'}: {target}")
    return {"response": f"saved {target.name}"}


def handle_vault_daily(data: dict) -> dict:
    content = data.get("content") or ""
    root = vault_root()
    if root is None:
        return {"error": "no vault configured"}
    name = dt.date.today().strftime(CFG["daily_format"]) + ".md"
    rel = f"{CFG['daily_dir']}/{name}" if CFG["daily_dir"] else name
    target = safe_vault_path(rel)
    target.parent.mkdir(parents=True, exist_ok=True)
    new = not target.exists()
    with target.open("a", encoding="utf-8") as f:
        if new:
            f.write(f"# {dt.date.today().isoformat()}\n\n")
        f.write(content if content.endswith("\n") else content + "\n")
    log(f"daily append: {target}")
    return {"response": f"appended to {name}"}


def handle_vault_read(data: dict) -> dict:
    rel = (data.get("path") or "").strip()
    try:
        target = safe_vault_path(rel)
    except ValueError as e:
        return {"error": str(e)}
    if not target.is_file():
        return {"error": "not found"}
    return {"response": target.read_text(encoding="utf-8", errors="replace")[:20000]}


def handle_vault_list(qs: dict) -> dict:
    root = vault_root()
    if root is None:
        return {"error": "no vault configured", "files": []}
    sub = (qs.get("dir", [""])[0] or "").lstrip("/")
    base = (root / sub) if sub else root
    if not base.is_dir():
        return {"files": []}
    files = sorted(p.relative_to(root).as_posix()
                   for p in base.rglob("*.md") if not p.name.startswith("."))
    return {"files": files[:500]}


# --------------------------------------------------------------------------
# HTTP
# --------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = f"cardputerd/{VERSION}"

    def _send(self, obj: dict, code: int = 200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _json_body(self) -> dict:
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length).decode("utf-8", "replace")
        return json.loads(raw) if raw else {}

    def do_GET(self):  # noqa: N802
        url = urlparse(self.path)
        if url.path == "/ping":
            return self._send({
                "ok": True,
                "version": VERSION,
                "claude": bool(shutil.which(CFG["claude_bin"]) or Path(CFG["claude_bin"]).exists()),
                "vault": vault_root() is not None,
                "stt": CFG["stt"],
            })
        if url.path == "/vault/list":
            return self._send(handle_vault_list(parse_qs(url.query)))
        self._send({"error": "not found"}, 404)

    def do_POST(self):  # noqa: N802
        path = urlparse(self.path).path
        try:
            if path == "/transcribe":
                length = int(self.headers.get("Content-Length", 0))
                wav = self.rfile.read(length)
                log(f"stt> {length} bytes")
                text, backend = transcribe(wav)
                if text is None:
                    return self._send({"error": "no speech-to-text backend available"}, 503)
                log(f"stt< [{backend}] {text[:80]}")
                return self._send({"text": text, "backend": backend})

            data = self._json_body()
            routes = {
                "/ask": handle_ask,
                "/code": handle_code,
                "/vault/note": handle_vault_note,
                "/vault/daily": handle_vault_daily,
                "/vault/read": handle_vault_read,
            }
            fn = routes.get(path)
            if fn is None:
                return self._send({"error": "not found"}, 404)
            result = fn(data)
            return self._send(result, 400 if "error" in result else 200)
        except json.JSONDecodeError:
            self._send({"error": "bad json"}, 400)
        except Exception as e:  # noqa: BLE001 - never let one bad request kill the daemon
            log("error:", e)
            self._send({"error": str(e)}, 500)

    def log_message(self, fmt, *args):
        pass


def advertise(port: int):
    """Publish _cardputerd._tcp so the device can find us without an IP."""
    try:
        from zeroconf import ServiceInfo, Zeroconf  # type: ignore
    except ImportError:
        log("zeroconf not installed - set the host manually in Settings")
        return None
    import socket

    host = socket.gethostname().split(".")[0]
    addr = socket.inet_aton(socket.gethostbyname(socket.gethostname()))
    info = ServiceInfo(
        "_cardputerd._tcp.local.",
        f"{host}._cardputerd._tcp.local.",
        addresses=[addr],
        port=port,
        properties={"version": VERSION},
        server=f"{host}.local.",
    )
    zc = Zeroconf()
    zc.register_service(info)
    log(f"advertising _cardputerd._tcp as {host}.local:{port}")
    return zc


def main():
    global CFG
    ap = argparse.ArgumentParser(description="CardputerOS companion daemon")
    ap.add_argument("--config", default=str(HERE / "config.json"))
    ap.add_argument("--port", type=int)
    args = ap.parse_args()

    CFG = load_config(Path(args.config))
    if args.port:
        CFG["port"] = args.port

    port = CFG["port"]
    zc = advertise(port) if CFG["advertise"] else None

    log(f"cardputerd {VERSION} on 0.0.0.0:{port}")
    log(f"claude:  {CFG['claude_bin']}")
    log(f"vault:   {vault_root() or '(not configured)'}")
    log(f"stt:     {CFG['stt']}")

    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("shutting down")
    finally:
        if zc:
            zc.close()
        server.server_close()


if __name__ == "__main__":
    main()
