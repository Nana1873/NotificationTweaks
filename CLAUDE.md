# Claude Code Notes (Notification Tweaks)

Read `AGENTS.md` first: it is the source of truth for workspace boundaries,
git safety, the local workflow and coordination. Everything there applies to
Claude Code sessions. This file only adds Claude-specific deltas; do not
duplicate AGENTS.md content here.

## Claude-specific deltas

- Shell is PowerShell.
- For parallel sessions use the worktree convention in AGENTS.md. Worktrees
  go to `E:\DevProjects\MO2 Dev Setup\worktrees\`, never into `workspaces\`.
- Hard rule (echo of AGENTS.md "Git Safety"): never commit `.pex`, `.esp`,
  `vendor/`, or any third-party payload.
