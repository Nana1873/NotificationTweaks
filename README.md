# Notification Tweaks

Skyrim SE mod workspace inside the MO2 dev lab.

## Layout

- `src/` â€” editable mod source
- `vendor/original/` â€” untouched release files (gitignored)
- `build/` â€” generated MO2-ready output (gitignored)
- `patches/` â€” SPID/KID/SkyPatcher/BOS config experiments
- `docs/` â€” research and migration notes
- `tests/` â€” test artifacts
- `tools/` â€” build/deploy/verify scripts

## First Commands

```powershell
git status --short --branch
```

See `AGENTS.md` for the agent workflow and safety rules.
