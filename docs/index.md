# Project Documentation Index

This directory is the single source of truth for active project documentation.

## Naming Rules

Use lowercase kebab-case for every document name.

Development plans must live under:

```text
docs/plan/
```

Development plan naming:

```text
plan-YYYY-MM-DD-topic.md
```

Examples:

- `docs/plan/plan-2026-06-03-gpu-driven-execution.md`
- `docs/plan/plan-2026-06-10-indirect-draw-mvp.md`
- `docs/plan/plan-2026-06-17-gpu-culling.md`

Learning logs must live under:

```text
docs/learning/
```

Learning log naming:

```text
YYYY-MM-DD-HHMM-topic.md
```

Other document categories:

- `guide-topic.md` for setup and operating guides.
- `test-topic.md` for repeatable test procedures.
- `report-YYYY-MM-DD-topic.md` for measured results and summaries.
- `note-topic.md` for lightweight technical notes.
- `archive-topic.md` for historical material that is not part of the active plan.

Avoid spaces, underscores, mixed casing, and vague names such as `Next_Phase` or `Final_Plan`.

## Active Documents

- [plan/plan-2026-06-03-gpu-driven-execution.md](plan/plan-2026-06-03-gpu-driven-execution.md): Current GPU-driven rendering execution plan.
- [guide-gpu-pass-demo-v1.md](guide-gpu-pass-demo-v1.md): Step-by-step setup for the first visible GPU pass demo.
- [test-compute-shader-validation.md](test-compute-shader-validation.md): Manual validation flow for the compute shader pass.
- [guide-mcp-configuration.md](guide-mcp-configuration.md): Unreal MCP setup guide.
- [learning/index.md](learning/index.md): Learning log index for task-by-task knowledge capture.

## Documentation Policy

Keep documents aligned with the current repository state. If implementation has already moved past a plan item, update the document instead of leaving stale statements in place.

Prefer concise project-specific instructions over broad tutorial content. Historical or non-project material should be archived or removed from the main documentation set.

After each meaningful implementation task, add a short user-facing explanation and, when useful, write a learning note under `docs/learning/`.
