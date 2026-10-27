# Claude Code Notes (Notification Tweaks)

Read `AGENTS.md` first â€” it is the source of truth for workspace boundaries,
git safety, the local workflow, and (if present) the GitHub coordination
rules. Everything there applies to Claude Code sessions. This file only adds
Claude-specific deltas; do not duplicate AGENTS.md content here.

## Claude-specific deltas

- Branch prefix is `claude/` (`claude/<issue#>-<slug>`), not `codex/`.
- Use the `gh` CLI for all GitHub operations (issues, labels, PRs, checks).
- Shell is PowerShell.
- For parallel sessions use the worktree convention in AGENTS.md. Worktrees
  go to `E:\DevProjects\MO2 Dev Setup\worktrees\`, never into `workspaces\`.
- Slash commands: `/next-issue` claims the next ready issue and sets up the
  worktree; `/finish-issue` verifies, pushes, and opens the PR.
- Hard rule (echo of AGENTS.md "Git Safety"): never commit `.pex`, `.esp`,
  `vendor/`, or any third-party payload.
