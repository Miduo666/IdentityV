---
name: idv-plaintext-payload-audit
description: Audit a locally extracted plaintext Python payload for its object-field access and outbound data schema. Use for static, read-only source analysis; do not use to inject, hook, alter, or control a running target.
---

# Plaintext payload field audit

Use this skill to establish exactly what an extracted Python payload can read from an embedded game object graph. The deliverable is an evidence-led inventory: source field name, object context, access type, fallback status, and resulting output key.

This is a **static and read-only** workflow. Inspect the extracted text and its local companion artifacts only. Do not load a library into a target, invoke control packets, attach to a process, or validate fields by changing a target's state.

## Workflow

1. Confirm the input is the extracted text, not an inference from log output. Record its byte length and the number of top-level functions.
2. Treat a field as confirmed only when the payload directly calls `getattr`, accesses it with dot syntax, passes it to a known getter, or serializes its result. A string in a fallback tuple is a candidate, not proof that every candidate exists.
3. Keep three namespaces separate in the report:
   - engine fields and methods, such as `player.genius_id_lv_lst`;
   - payload-local JSON keys, such as `hook_progress_percent`;
   - UI labels and debug text.
4. For each collection path, document the root object, traversal, fields, and emitted JSON. Note `try/except: pass` blocks because they make missing fields fail silently.
5. Use the included extractor for a line-numbered inventory of literal attribute access. It is a static parser aid, not a runtime probe.

```powershell
& .\extract-literal-fields.ps1 -SourcePath <extracted-script.py>
```

For the field map and evidence already recovered from the archived 2026.0611.0155 payload, read [references/field-inventory.md](references/field-inventory.md). For the complete mechanically generated list of all 394 literal attribute names, read [references/literal-field-catalog.md](references/literal-field-catalog.md).

## Reporting rules

- State whether an item is a direct read, a fallback candidate, or an inferred semantic meaning.
- Do not translate field names into claims of current-version validity without a separate, authorized observation.
- Preserve the source line or function name for every non-obvious conclusion.
- Flag security-relevant plaintext exposure separately from functionality: embedded source, local unauthenticated control surface, log paths, and any outbound endpoints.
