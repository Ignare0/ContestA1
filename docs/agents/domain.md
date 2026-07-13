# Domain Docs

How engineering skills should consume this repo's domain documentation when exploring the codebase.

## A1 simulator scope

All contest implementation and final submission work is scoped to `A1-simulator/`.

Before planning or changing this work, read the relevant project documentation and Markdown files. Treat `A1-simulator/submission/third_party/` as vendored code, not as project planning guidance.

## Before exploring, read these

- **`CONTEXT.md`** at the repo root, or
- **`CONTEXT-MAP.md`** at the repo root if it exists — it points at one `CONTEXT.md` per context. Read each one relevant to the topic.
- **`docs/adr/`** — read ADRs that touch the area you're about to work in. In multi-context repos, also check `src/<context>/docs/adr/` for context-scoped decisions.

If any of these files don't exist, proceed silently. The domain-modeling skill creates them when terms or decisions are actually resolved.

## File structure

Single-context repo:

```text
/
├── CONTEXT.md
├── docs/adr/
└── src/
```

Multi-context repo:

```text
/
├── CONTEXT-MAP.md
├── docs/adr/
└── src/
    ├── ordering/
    │   ├── CONTEXT.md
    │   └── docs/adr/
    └── billing/
        ├── CONTEXT.md
        └── docs/adr/
```

## Use the glossary's vocabulary

When output names a domain concept, use the term defined in `CONTEXT.md`. If a necessary concept is absent, reconsider whether the wording drifts from existing vocabulary; otherwise record the gap for domain-modeling.

## Flag ADR conflicts

If an output contradicts an existing ADR, surface the conflict explicitly rather than silently overriding it.
