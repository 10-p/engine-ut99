#!/usr/bin/env python3
"""Config/System/android/{Default,DefUser}.ini from Config/System/web/: the web inis with the Android deltas
below applied. The Gradle project packages the Android pair as APK assets, and GameActivity installs them
into the game folder's System/ before each start.

    tools/dev/sync-android-ini.py          # rewrite the Android pair
    tools/dev/sync-android-ini.py --check  # exit 1 when a rewrite would change them

A delta names the file, section and key, the value the web ini must hold and the Android value; a web value
that differs stops the script, so a change on the web side is seen rather than silently overwritten. Each
file keeps its source's line endings and Latin-1 bytes."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "Config" / "System" / "web"
ANDROID = ROOT / "Config" / "System" / "android"

# (file, section, key, web value, Android value)
DELTAS = [
    # XOpenGLDrv on a GLES 3 context (NSDLViewport asks for one): the GLES 3.0 shader profile the web uses.
    ("Default.ini", "XOpenGLDrv.XOpenGLRenderDevice", "OpenGLVersion", "ES_WEBGL", "ES_WEBGL"),
    # Vertical sync: the display's own rate, no tearing.
    ("Default.ini", "XOpenGLDrv.XOpenGLRenderDevice", "UseVSync", "Adaptive", "On"),
]


def apply(text, section, key, old, new, where):
    lines = text.split("\n")
    cur = None
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            cur = s[1:-1]
        elif cur == section and "=" in s and s.split("=", 1)[0].strip() == key:
            val = s.split("=", 1)[1].strip()
            if val != old:
                sys.exit(f"{where}: [{section}] {key}={val}, expected {old}")
            lines[i] = f"{key}={new}"
            return "\n".join(lines)
    sys.exit(f"{where}: [{section}] {key} not found")


def main():
    check = "--check" in sys.argv[1:]
    changed = False
    for name in ("Default.ini", "DefUser.ini"):
        src = WEB / name
        raw = src.read_bytes().decode("latin-1")
        crlf = "\r\n" in raw
        text = raw.replace("\r\n", "\n")
        for f, section, key, old, new in DELTAS:
            if f == name:
                text = apply(text, section, key, old, new, src)
        out = ANDROID / name
        if crlf:
            text = text.replace("\n", "\r\n")
        data = text.encode("latin-1")
        if out.exists() and out.read_bytes() == data:
            continue
        changed = True
        if check:
            print(f"{out} is out of date")
        else:
            out.write_bytes(data)
            print(f"wrote {out}")
    if check and changed:
        sys.exit(1)


if __name__ == "__main__":
    main()
