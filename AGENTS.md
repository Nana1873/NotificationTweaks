# Notification Tweaks Workspace

This folder is the Notification Tweaks repo and the source of truth for its
development. Do not treat `E:/DevProjects/MO2 Dev Setup/` as the repo when
working on this project.

## Boundaries

- **Workspace root**: `E:/DevProjects/MO2 Dev Setup/workspaces/Notification Tweaks/`
- **Shared lab root**: `E:/DevProjects/MO2 Dev Setup/`
- **Canonical MO2 runtime**: `E:/Modlisten/SkyrimAE/`
- **MO2 profile**: `E:/Modlisten/SkyrimAE/profiles/Default/`
- **MO2 dev mod target**: `E:/Modlisten/SkyrimAE/mods/Notification Tweaks Dev/`
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
runtime tests â€” a direct launch bypasses MO2/USVFS.

For game-binary inspection (native crashes, SKSE hook targets, undocumented
engine behavior), follow the shared lab `AGENTS.md` "Shared Tools" Ghidra rules
and `tools/TOOLCHAIN.md`. The analyzed SkyrimSE 1.6.1170 project already exists.
Use the lab's `tools/agent/start-ghidra-mcp.ps1` for read-only inspection and
`tools/agent/stop-ghidra-mcp.ps1` to stop the session. It binds only to loopback;
respect existing listener/process/project locks. Stop the MCP session before
opening or rebuilding that same project. An inspection request does not call
for `python run.py`, a new import, or a Ghidra/LLVM download.

## GitHub Coordination

<!-- Remove this whole section if this workspace stays local-only. -->

Multiple agents (Claude Code, Codex) work on this repo in parallel. GitHub is
the coordination layer: Issues are the task queue, PRs are the only way into
`main`. CI only runs a light `pr-hygiene` check; real verification stays
local.

### Task queue

- Only start coding against an open issue labeled `status:ready`. Exception:
  trivial fixes (a few lines) may go straight to a PR labeled `no-issue`.
- Issue labels: `status:ready`, `status:in-progress`, `status:blocked`, plus
  one `type:*` label. An open PR referencing the issue is the review state.
- New tasks use the "Agent task" issue template
  (`.github/ISSUE_TEMPLATE/agent_task.yml`).

### Claim protocol

All agents act as the same GitHub user, so the assignee field cannot prevent
collisions. Labels plus a claim comment are the lock:

1. `gh issue list --label "status:ready" --state open`
2. Read the issue comments. If a claim comment exists and the issue is
   `status:in-progress`, pick another issue.
3. Claim it:
   `gh issue edit <N> --remove-label "status:ready" --add-label "status:in-progress"`
   then comment `Claimed by <claude|codex>, branch <prefix>/<N>-<slug>`.
4. If you abandon the issue, swap the labels back and comment why.

### Branches and PRs

- Branch names: `codex/<issue#>-<slug>` (Codex) or `claude/<issue#>-<slug>`
  (Claude Code), always cut from fresh `main`. Never commit to `main`
  directly.
- One issue = one branch = one PR against `main`.
- The PR body must contain `Closes #<issue>` and the filled checklist from
  `.github/PULL_REQUEST_TEMPLATE.md`.

### Parallel sessions (git worktrees)

- One worktree per issue/branch. Worktrees live at the lab root:
  `E:\DevProjects\MO2 Dev Setup\worktrees\<repo-slug>-<slug>`. Never create
  worktrees inside `workspaces\`.
- Create: `git worktree add "..\..\worktrees\<repo-slug>-<slug>" -b <prefix>/<N>-<slug> main`
- Remove after merge: `git worktree remove "..\..\worktrees\<repo-slug>-<slug>"`,
  then `git worktree prune`.
- **Runtime deploy is a mutex**: only one session may deploy to MO2 or launch
  Skyrim at a time, across all workspaces (lab AGENTS.md "Runtime mutex").

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
