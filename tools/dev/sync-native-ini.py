#!/usr/bin/env python3
"""Config/System/native/{Default,DefUser}.ini from Config/System/web/: the web inis with the native deltas below
applied. The native release archives ship the pair as System/Default.ini and System/DefUser.ini, and the engine
derives UnrealTournament.ini and User.ini from them when those are absent.

    tools/dev/sync-native-ini.py          # rewrite the native pair
    tools/dev/sync-native-ini.py --check  # exit 1 when a rewrite would change them

Each delta names the file and the section, and the web lines it expects: a web value that differs, a key that is
missing or one the web gained in a section the native file trims stops the script, so a change on the web side is
seen rather than silently overwritten or dropped. Each file keeps its source's line endings and Latin-1 bytes.

The operations, applied in order:
    ("set", file, section, key, web value, native value)   the native value may be ("web", section, key): the
                                                           web file's value of that key
    ("drop", file, section, "Key=Value", ...)              remove these lines
    ("trim", file, section, [keys kept], ["Key=Value" dropped, ...])
                                                           remove the listed lines; every line of the section is
                                                           in one of the two lists
    ("add", file, section, after key, "Key=Value")         insert after that key (None: after the last key)
    ("drop-section", file, section)                        remove the section with the comment block above it
    ("add-section", file, before section, [lines])         insert a section before that one (None: at the end)
    ("comment", file, section, web text or None, [lines])  replace the comment block above the section's header,
                                                           which must contain that text (None: the web has none)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "Config" / "System" / "web"
NATIVE = ROOT / "Config" / "System" / "native"

WEBRTC = "WebRTCDrv.WebRTCNetDriver"
XOPENGL = "XOpenGLDrv.XOpenGLRenderDevice"
UTGLR = "OpenGLDrv.OpenGLRenderDevice"
NOPENGLES = "NOpenGLESDrv.NOpenGLESRenderDevice"

DELTAS = [
    # A game install's CD-ROM path and a bot count the web carries over from an installed game's ini: natively the
    # retail behaviour (no CdPath, the game type's own bot count).
    ("drop", "Default.ini", "Engine.Engine", "CdPath=D:"),
    ("drop", "Default.ini", "Botpack.DeathMatchPlus", "InitialBots=0"),
    # The native server and client: the web driver's client rate and tick rates on the TCP driver, whose retail
    # values the web keeps unused. The web's own driver section goes.
    ("comment", "Default.ini", "IpDrv.TcpNetDriver", None, [
        "; A dedicated server ticks at NetServerMaxTickRate (LanServerMaxTickRate with -lanplay), clamped 10..120;",
        "; MaxClientRate caps the rate a client's ConfiguredInternetSpeed asks for.",
    ]),
    ("set", "Default.ini", "IpDrv.TcpNetDriver", "MaxClientRate", "20000", ("web", WEBRTC, "MaxClientRate")),
    ("set", "Default.ini", "IpDrv.TcpNetDriver", "NetServerMaxTickRate", "20", ("web", WEBRTC, "NetServerMaxTickRate")),
    ("set", "Default.ini", "IpDrv.TcpNetDriver", "LanServerMaxTickRate", "35", ("web", WEBRTC, "LanServerMaxTickRate")),
    ("drop-section", "Default.ini", WEBRTC),
    # The desktop window: fullscreen at the display's own resolution (the compiled default, written out).
    ("add", "Default.ini", "NSDLDrv.NSDLClient", "StartupFullscreen", "BorderlessFullscreen=True"),
    # UTGLR: antialiasing off (NumAASamples is the count when it is turned on), anisotropy, screenshots and detail;
    # every other key takes the driver's compiled default.
    ("comment", "Default.ini", UTGLR, None, [
        "; UTGLR: antialiasing off (NumAASamples is the count when UseAA is turned on), anisotropy, screenshots and",
        "; detail; every other key takes the driver's compiled default.",
    ]),
    ("set", "Default.ini", UTGLR, "UseAA", "True", "False"),
    ("trim", "Default.ini", UTGLR,
     ["UseAA", "NumAASamples", "MaxAnisotropy", "GammaCorrectScreenshots", "Translucency", "VolumetricLighting",
      "VolumetricLightingMeshes", "ShinySurfaces", "Coronas", "HighDetailActors", "DetailTextures"],
     ["LODBias=0.0", "GammaOffset=0.0", "GammaOffsetRed=0.0", "GammaOffsetGreen=0.0", "GammaOffsetBlue=0.0",
      "OneXBlending=False", "MinLogTextureSize=0", "MaxLogTextureSize=12", "UseZTrick=False",
      "UseBGRATextures=True", "UseMultiTexture=True", "UsePalette=False", "ShareLists=False", "UsePrecache=False",
      "UseTrilinear=True", "UseAlphaPalette=False", "UseS3TC=True", "Use16BitTextures=False", "NoFiltering=False",
      "MaxTMUnits=0", "RefreshRate=0", "DetailMax=1", "DetailClipping=False", "ColorizeDetailTextures=False",
      "SinglePassFog=True", "SinglePassDetail=False", "BufferTileQuads=False", "UseSSE=True", "UseSSE2=True",
      "UseTexIdPool=True", "UseTexPool=True", "CacheStaticMaps=True", "DynamicTexIdRecycleLevel=100",
      "TexDXT1ToDXT3=False", "UseMultiDrawArrays=False", "UseFragmentProgram=False", "SwapInterval=-1",
      "FrameRateLimit=0", "SceneNodeHack=True", "SmoothMaskedTextures=True", "MaskedTextureHack=True",
      "NoAATiles=True", "ZRangeHack=True"]),
    # XOpenGL: antialiasing off; the GL version, vertical sync and the GL 4 capability keys (which the web turns off
    # for WebGL 2) take the driver's compiled defaults.
    ("comment", "Default.ini", XOPENGL, "On the web OpenGLVersion is set to ES_WEBGL", [
        "; ============================================================================",
        "; XOpenGL: antialiasing off (NumAASamples is the count when UseAA is turned",
        "; on). OpenGLVersion, UseVSync and the GL 4 capability keys (bindless",
        "; textures, persistent buffers, hardware clipping and lighting...) take the",
        "; driver's compiled defaults. GammaCorrectScreenshots=False: a shot is the",
        "; frame as the present pass shows it, without a display gamma ramp.",
        "; ============================================================================",
    ]),
    ("set", "Default.ini", XOPENGL, "UseAA", "True", "False"),
    ("drop", "Default.ini", XOPENGL, "OpenGLVersion=ES_WEBGL", "UseVSync=Adaptive", "UseBufferInvalidation=False",
     "UseHWLighting=False", "UseHWClipping=False", "UseEnhancedLightmaps=False", "UseMeshBuffering=False",
     "UsePersistentBuffers=False", "UseBindlessTextures=False", "UseShaderDrawParameters=False",
     "UseLightmapAtlas=False"),
    # NOpenGLES: antialiasing off; the vertex arrays and texture format take the driver's compiled defaults.
    ("set", "Default.ini", NOPENGLES, "NumAASamples", "4", "0"),
    ("drop", "Default.ini", NOPENGLES, "UseVAO=False", "UseBGRA=True"),
]

HEADER = re.compile(r"\[(.+)\]\s*$")


def fail(where, msg):
    sys.exit(f"{where}: {msg}")


def section_range(lines, section, where):
    """(header index, end): the section's lines are header+1 .. end-1, end being the next header or the end."""
    h = None
    for i, line in enumerate(lines):
        m = HEADER.match(line)
        if m and h is not None:
            return h, i
        if m and m.group(1) == section:
            h = i
    if h is None:
        fail(where, f"[{section}] not found")
    return h, len(lines)


def last_key(lines, h, n):
    """The index of the section's last line that is neither blank nor a comment (the header when it has none)."""
    e = n - 1
    while e > h and (not lines[e].strip() or lines[e].startswith(";")):
        e -= 1
    return e


def key_of(line):
    return line.split("=", 1)[0].strip() if "=" in line and not line.startswith(";") else None


def find_key(lines, h, n, key, where, section):
    hits = [i for i in range(h + 1, n) if key_of(lines[i]) == key]
    if len(hits) != 1:
        fail(where, f"[{section}] {key}: {'not found' if not hits else 'more than once'}")
    return hits[0]


def comment_start(lines, h):
    c = h
    while c > 0 and lines[c - 1].startswith(";"):
        c -= 1
    return c


def web_value(web_lines, section, key, where):
    h, n = section_range(web_lines, section, where)
    return web_lines[find_key(web_lines, h, n, key, where, section)].split("=", 1)[1]


def apply(lines, web_lines, delta, where):
    op, _, section = delta[0], delta[1], delta[2]
    if op == "add-section":
        before, block = delta[2], delta[3]
        if before is None:
            while lines and not lines[-1].strip():
                lines.pop()
            lines[len(lines):] = [""] + block + [""]
        else:
            h, _ = section_range(lines, before, where)
            c = comment_start(lines, h)
            lines[c:c] = block + [""]
        return
    h, n = section_range(lines, section, where)
    if op == "set":
        key, old, new = delta[3], delta[4], delta[5]
        if isinstance(new, tuple):
            new = web_value(web_lines, new[1], new[2], where)
        i = find_key(lines, h, n, key, where, section)
        val = lines[i].split("=", 1)[1]
        if val != old:
            fail(where, f"[{section}] {key}={val}, expected {old}")
        lines[i] = f"{key}={new}"
    elif op in ("drop", "trim"):
        if op == "drop":
            dropped = delta[3:]
        else:
            kept, dropped = delta[3], delta[4]
            for i in range(h + 1, n):
                k = key_of(lines[i])
                if k is not None and k not in kept and lines[i] not in dropped:
                    fail(where, f"[{section}] {lines[i]}: neither kept nor dropped by this script")
        for line in dropped:
            hits = [i for i in range(h + 1, n) if lines[i] == line]
            if len(hits) != 1:
                fail(where, f"[{section}] {line}: {'not found' if not hits else 'more than once'}")
            del lines[hits[0]]
            n -= 1
    elif op == "add":
        after, line = delta[3], delta[4]
        if any(key_of(lines[i]) == key_of(line) for i in range(h + 1, n)):
            fail(where, f"[{section}] {key_of(line)} is already there")
        i = last_key(lines, h, n) if after is None else find_key(lines, h, n, after, where, section)
        lines.insert(i + 1, line)
    elif op == "drop-section":
        e = last_key(lines, h, n)
        while e + 1 < len(lines) and not lines[e + 1].strip():
            e += 1
        del lines[comment_start(lines, h):e + 1]
    elif op == "comment":
        marker, block = delta[3], delta[4]
        c = comment_start(lines, h)
        if marker is None and c != h:
            fail(where, f"[{section}] has a comment block, expected none")
        if marker is not None and marker not in "\n".join(lines[c:h]):
            fail(where, f"[{section}]'s comment block does not contain {marker!r}")
        lines[c:h] = block
    else:
        fail(where, f"unknown operation {op}")


def main():
    check = "--check" in sys.argv[1:]
    changed = False
    for name in ("Default.ini", "DefUser.ini"):
        src = WEB / name
        raw = src.read_bytes().decode("latin-1")
        crlf = "\r\n" in raw
        web_lines = raw.replace("\r\n", "\n").split("\n")
        lines = list(web_lines)
        for delta in DELTAS:
            if delta[1] == name:
                apply(lines, web_lines, delta, src)
        text = "\n".join(lines)
        if crlf:
            text = text.replace("\n", "\r\n")
        data = text.encode("latin-1")
        out = NATIVE / name
        if out.exists() and out.read_bytes() == data:
            continue
        changed = True
        if check:
            print(f"{out} is out of date (run tools/dev/sync-native-ini.py)")
        else:
            NATIVE.mkdir(exist_ok=True)
            out.write_bytes(data)
            print(f"wrote {out}")
    if check and changed:
        sys.exit(1)


if __name__ == "__main__":
    main()
