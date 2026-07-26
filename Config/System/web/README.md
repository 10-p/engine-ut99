# Config/System/web — web platform config (build-consumed)

`Default.ini` + `DefUser.ini` here are the **web** boot config: the Emscripten link `--preload-file`s them
(`Source/UnrealTournament/CMakeLists.txt`), and UT99 v400's `appInit` derives `UnrealTournament.ini` /
`User.ini` from them at boot. They carry the web-only settings — the `[WebRTCDrv.WebRTCNetDriver]` section
and the GL/ES render device — that a native game install's inis do not have.

`../*.ini` (the parent folder) is the **platform-agnostic / Dreamcast-heritage** reference set; it is NOT
what the web ships.

Previously the web build preloaded these from the out-of-tree asset dir, so the web config lived only in an
untracked game install and was silently reverted whenever that image was rebuilt. The ufront manifest now
also excludes `System/UnrealTournament.ini` + `System/User.ini`, so a stale install copy cannot shadow what
`appInit` derives here. **Edit this folder when the web config changes — never the asset tree.**
