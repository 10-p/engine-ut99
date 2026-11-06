# Config/System/web

The web build's boot config. The Emscripten link preloads `Default.ini` and `DefUser.ini` into `/System`
(`Source/UnrealTournament/CMakeLists.txt`), and the engine derives `UnrealTournament.ini` and `User.ini` from them on
a player's first boot. The frontend leaves the asset install's copies of all four files out of what it serves
(`manifestExclude` in its `targets/ut99-436-compat/target.json`), so these are the only ones the web reads. Edit this folder
when the web config changes, never an asset tree.

`Default.ini` keeps the retail subsystem names (`WinDrv.WindowsClient`, `Galaxy.GalaxyAudioSubsystem`,
`IpDrv.TcpNetDriver`); the launcher replaces each with the one the build has (`BUILD.md`, Run time). It names
XOpenGL as the render device (`GameRenderDevice`, `WindowedRenderDevice`, `RenderDevice`). On top of the
retail file it carries `[WebRTCDrv.WebRTCNetDriver]`, `[ProxyNetDrv.ProxyNetDriver]`,
`[XOpenGLDrv.XOpenGLRenderDevice]`, `[NOpenGLESDrv.NOpenGLESRenderDevice]`, `[NOpenALDrv.NOpenALAudioSubsystem]`,
`[NullDrv.NullRenderDevice]`, `[NSDLDrv.NSDLClient]` and `[ufront.AutoFOV]`, the net drivers' `DownloadManagers`, and
the community master servers in `[Engine.GameEngine]` and `[UBrowserAll]`, and sets `[UMenu.UMenuRootWindow]
GUIScale=2` (the menus' Font Size at 2x: the retail 1x suits a 1280x1024 screen, not a 1080p one) and XOpenGL's
`GammaCorrectScreenshots=False` (the canvas has no display gamma ramp, so a shot is the frame as the present pass
shows it). `DefUser.ini` adds the game-pad bindings (Unreal 1's layout) and `[Engine.PlayerPawn]
bMaxMouseSmoothing=False`: the view stops on the frame the mouse stops (the script's default keeps turning for up to
`MouseSmoothThreshold` after it).

`../native/` is the native pair, which the native release archives ship in `System/`, and `../android/` the
Android pair: `tools/dev/sync-native-ini.py` and `tools/dev/sync-android-ini.py` write them from this folder with the
deltas each script lists (`--check` verifies them), so a change here reaches them by running the scripts, never by
editing them. `../*.ini` is ut99dc's Dreamcast config (with the same master-server lists and `DownloadManagers`); no
build preloads or installs it.

## Config version

Every default ini a build ships (this pair, `../native/` and `../android/`) opens with a section of its own:

```ini
[ConfigVersion]
Version=1
```

The engine makes a player's `UnrealTournament.ini` and `User.ini` as byte copies of `Default.ini` and `DefUser.ini` on
the first run and never refreshes them, so the `Version` they keep says which defaults they were copied from. A live ini
without the section was made before the number. Right after the config cache starts, before the `-ini:` overrides,
`appInit` adds `Version=0` to each live ini (`UnrealTournament.ini` and `User.ini`, or the files `INI=` and `USERINI=`
name) when it has no `[ConfigVersion] Version` key and writes that file alone, as any save writes it (comment lines left
out), logging `Config: <file> has no [ConfigVersion]: Version=0 added`; the section then comes last in the file, and a
`Version` that is there is never changed. That marker is the only thing the engine writes into a player's ini for the
config version: nothing reads the number to change anything, and a player's ini is never migrated. No class owns the
section, so no `SaveConfig` or `ResetConfig` touches it and the config cache writes it back as it is. All six files
carry the same number; the generators keep the section first. `../*.ini` (the Dreamcast config) has none.
`feat/v436-compat` shares this number: it merges `main`, and a bump made there alone is merged back by taking the higher
number plus one.

The rule: a commit that adds a default, changes a default value or removes a key in one of these inis, or adds or
changes a compiled or script default of a setting the engine saves to the ini, bumps `Version` by one in all of them
and adds the new number's row below, saying what a player on the version before lacks. Comment-only and
whitespace-only changes do not bump. The frontend's `ini-config-version` pre-commit gate checks the inis; its
`docs/CONVENTIONS.md` § Ini config version has the rule in full.

| Version | What a player on the version before lacks |
|---------|-------------------------------------------|
| 1 | The defaults when the number was introduced: a live ini without the section may lack any of them. |
