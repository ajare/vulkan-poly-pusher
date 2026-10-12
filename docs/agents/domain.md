# Domain docs

## Layout

Single-context: `CONTEXT.md` at the repo root and ADRs in `docs/adr/`.

## Before exploring

Read `CONTEXT.md` and any ADRs relevant to the area being explored.

If these files do not exist, proceed silently. Do not flag their absence
or suggest creating them upfront. The domain-modeling skill creates
them lazily when terms or decisions are resolved.

## Use the glossary's vocabulary

Use terms defined in `CONTEXT.md` when naming domain concepts in issues,
proposals, hypotheses, and tests. Avoid synonyms the glossary excludes.

If a concept is missing, reconsider whether it belongs or note the gap
for domain-modeling.

## Flag ADR conflicts

Explicitly surface proposals that contradict an existing ADR rather
than silently overriding it.
