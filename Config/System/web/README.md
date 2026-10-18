# Config/System/web

The web build's boot config. The Emscripten link preloads `Default.ini` and `DefUser.ini` into `/System`
(`Source/UnrealTournament/CMakeLists.txt`), and the engine derives `UnrealTournament.ini` and `User.ini` from them on
a player's first boot. The frontend leaves the asset install's copies of all four files out of what it serves
(`manifestExclude` in its `targets/ut99/target.json`), so these are the only ones the web reads. Edit this folder
when the web config changes, never an asset tree.

`Default.ini` keeps the retail subsystem names (`WinDrv.WindowsClient`, `Galaxy.GalaxyAudioSubsystem`,
`IpDrv.TcpNetDriver`); the launcher replaces each with the one the build has (`BUILD.md`, Run time). On top of the
retail file it carries `[WebRTCDrv.WebRTCNetDriver]`, `[XOpenGLDrv.XOpenGLRenderDevice]`,
`[NOpenGLESDrv.NOpenGLESRenderDevice]`, `[NOpenALDrv.NOpenALAudioSubsystem]`, `[NullDrv.NullRenderDevice]`,
`[NSDLDrv.NSDLClient]` and `[ufront.AutoFOV]`. `DefUser.ini` adds the game-pad bindings (Unreal 1's layout).

`../*.ini` is ut99dc's Dreamcast config; no build preloads or installs it.
