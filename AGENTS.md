# Notification Tweaks Workspace

This folder is the Notification Tweaks repo and the source of truth for its
development. Do not treat `E:/DevProjects/MO2 Dev Setup/` as the repo when
working on this project.

## Boundaries

- **Workspace root**: `E:/DevProjects/MO2 Dev Setup/workspaces/Notification Tweaks/`
- **Shared lab root**: `E:/DevProjects/MO2 Dev Setup/`
- **Canonical MO2 runtime**: `E:/Modlisten/SkyrimAE/`
- **MO2 profile**: `E:/Modlisten/SkyrimAE/profiles/Default/`
- **MO2 dev mod target**: `E:/Modlisten/SkyrimAE/mods/houseCARL - Notification Tweaks Dev/`
- **Runtime logs**: `D:/Dokumente/My Games/Skyrim Special Edition/SKSE/` and `D:/Dokumente/My Games/Skyrim Special Edition/Logs/Script/`

Use this workspace for source, tests, docs, and build/deploy scripts. Use the
shared lab root for skills, build tools, and launch helpers; use SkyrimAE for
MO2, the active Game Root, and runtime deployment. Layout follows the lab
workspace rules: `src/` editable source, `vendor/original/` untouched release
files, `build/` generated output, `patches/` config experiments, `docs/` notes.

## Local Workflow

Start each agent session (Codex or Claude Code) in this workspace, then:

1. Read this file.
2. Check `git status --short --branch`.
3. Plan or implement the next slice.
4. Run the project's local checks before commit or push (add them to `tools/`
   as the project grows).

For runtime work, build and deploy from this workspace, then launch Skyrim
through the shared lab's MO2 helper:

```powershell
..\..\tools\agent\launch-skyrim-skse.ps1
```

Do not start `E:\Modlisten\SkyrimAE\Game Root\skse64_loader.exe` directly for
runtime tests - a direct launch bypasses MO2/USVFS.

For game-binary inspection (native crashes, SKSE hook targets, undocumented
engine behavior), follow the shared lab `AGENTS.md` "Shared Tools" Ghidra rules
and `tools/TOOLCHAIN.md`. The analyzed SkyrimSE 1.6.1170 project already exists.
Use the lab's `tools/agent/start-ghidra-mcp.ps1` for read-only inspection and
`tools/agent/stop-ghidra-mcp.ps1` to stop the session. It binds only to loopback;
respect existing listener/process/project locks. Stop the MCP session before
opening or rebuilding that same project. An inspection request does not call
for `python run.py`, a new import, or a Ghidra/LLVM download.

## Coordination

The repository is published at https://github.com/Nana1873/NotificationTweaks
(public, source only; release archives go to Nexus Mods). Work happens on
`main`; coordinate through commits and short notes in `docs/`, and push
`main` when a slice is done and verified. Use a lab-root worktree only for
parallel sessions. Runtime deploys and game launches follow the lab runtime
mutex.

## Build, Deploy, Package, Strings

- `tools\build.ps1` builds `build\release\NotificationTweaks.dll` (CMake,
  Ninja, vcpkg via `VCPKG_ROOT`, CommonLibSSE-NG submodule
  `extern/commonlibsse-ng`; Visual Studio is found through vswhere), then
  runs `NotificationTweaksTests.exe` (`tests/Tests.cpp`: merge rules,
  markup, hide keys, INI escaping); a failing check fails the build. This
  is the local check before commit.
- `tools\deploy.ps1 [-Register]` copies DLL, PDB and translations into the
  SkyrimAE mod `houseCARL - Notification Tweaks Dev`; `-Register` adds it to
  `modlist.txt` and refuses while MO2 runs. Quit the game first: it keeps
  the DLL and the build PDB open.
- `tools\package.ps1` writes `dist\NotificationTweaks-<version>.zip` for
  Nexus (DLL, translations, license, third-party notices; no PDB). It
  refuses when CMakeLists.txt and vcpkg.json disagree on the version, the
  generated string files are stale (`strings.py --check`) or the DLL is
  older than `src/`.
- `python -I tools\strings.py` regenerates `src/Translation.h`, the
  `LoadStrings()` body and both translation files from one table.

## Git Safety

Never commit third-party payloads, generated builds, deployed runtime
artifacts, or MO2 profile backups. These paths must stay untracked:

- `vendor/`
- `docs/source-docs/`
- `build/`
- `*.pex`, `*.esp`, `*.esm`, `*.esl`, `*.bsa`, `*.ba2`, `*.dll`, `*.pdb`
- MO2 `modlist.txt`, `plugins.txt`, `loadorder.txt`, or backup copies

Useful safety check (should print nothing):

```powershell
git ls-files | rg '^(vendor/|docs/source-docs/|build/)'
```

## Runtime Test Loop

1. Build and deploy dev artifacts from workspace tools.
2. Prefer the DevBench-first unattended runner
   `..\..\tools\agent\run-skyrim-test.ps1`; it launches SkyrimAE through
   MO2/USVFS, loads without saving, runs scenarios, and exits cleanly.
3. For renderer, shader, visual-regression, or performance work, use the
   Community Shaders Remote Control tools registered in DevBench. Default to
   read-only `communityshaders.inspect` and `communityshaders.feature`
   `list`/`get`; follow the shared lab `AGENTS.md` and `tools/TOOLCHAIN.md`
   mutation/rollback rules for all other Community Shaders operations.
4. For a visual/manual test use
   `..\..\tools\agent\launch-skyrim-skse.ps1`, load a save, run the test, then
   quit with `qqq`.
5. Check logs (`..\..\tools\agent\check-runtime-logs.ps1` for shared checks).
