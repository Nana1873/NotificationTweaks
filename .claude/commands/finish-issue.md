---
description: Verify, push, and open the PR for the current issue branch
---

Finish the current issue branch and open its pull request, following
AGENTS.md "GitHub Coordination".

1. Confirm the current branch is `claude/<N>-<slug>` and the working tree is
   clean (commit remaining work first with a focused message).
2. Run the project's local verification (see AGENTS.md "Local Workflow"). If
   it fails, stop and report the failure — do not push.
3. Push the branch and open the PR:
   `gh pr create --base main` with a title matching the commit style and a
   body that contains the filled checklist from
   `.github/PULL_REQUEST_TEMPLATE.md` and the line `Closes #<N>`.
4. Watch the CI result with `gh pr checks --watch` and report it. If
   `pr-hygiene` fails, fix the findings on this branch and push again.
5. Remind the user (or do it after merge): remove the worktree with
   `git worktree remove "..\..\worktrees\<repo-slug>-<slug>"` and run
   `git worktree prune`.
