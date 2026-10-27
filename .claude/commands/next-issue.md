---
description: Claim the next ready GitHub issue and set up a worktree for it
---

Claim the next task from the GitHub issue queue and prepare an isolated
worktree, following AGENTS.md "GitHub Coordination" exactly.

1. Run `gh issue list --label "status:ready" --state open`. If the user named
   an issue number in "$ARGUMENTS", use that one; otherwise pick the best fit
   and say why you picked it.
2. Read the chosen issue including comments (`gh issue view <N> --comments`).
   If it already has a claim comment or carries `status:in-progress`, pick a
   different issue.
3. Claim it: swap `status:ready` for `status:in-progress`, then comment
   `Claimed by claude, branch claude/<N>-<slug>` (pick a short kebab-case slug
   from the issue title).
4. Create the worktree from fresh main:
   `git fetch origin main` then
   `git worktree add "..\..\worktrees\<repo-slug>-<slug>" -b claude/<N>-<slug> origin/main`
5. Run the project's local preflight/checks if it has any (see AGENTS.md
   "Local Workflow").
6. Summarize the issue's acceptance criteria and verification steps, then
   continue working inside the worktree directory. Remember: runtime deploy is
   a mutex — coordinate before deploying to MO2 or launching Skyrim.
