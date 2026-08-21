---
name: project-memory
description: Restore or archive concise Toy3d conversation handoffs when the user asks to continue previous work, recover context in a new conversation, remember the current discussion, or archive a session. Do not use it as a general knowledge base or as a substitute for repository documentation.
---

# Project Memory

Maintain a concise handoff between Toy3d conversations. Treat the repository as the source of truth; memory records are navigation aids and may be stale.

## Choose the mode

- **Restore** when the user asks to continue, recover, or review the previous conversation.
- **Archive** when the user asks to remember, save, hand off, or archive the current conversation.
- If the request is ambiguous, infer the mode from whether the user is beginning/resuming work or wrapping it up. Ask only when choosing incorrectly would overwrite a useful record.

Memory lives under `.codex/memory/`:

- `LATEST.md` is the single fast-loading handoff for the next conversation.
- `archive/` contains immutable historical handoffs named `YYYY-MM-DD-HHmm-<topic>.md` using local time and a short lowercase ASCII topic slug.

## Restore

1. Read `.codex/memory/LATEST.md`. If it does not exist or says there is no archived conversation, report that briefly and continue from the user's current request.
2. Present a compact recovery summary covering the prior objective, confirmed state, unresolved items, and recommended next action.
3. Clearly label anything that may have become stale. Verify repository state before relying on remembered file contents, build results, branch state, external state, or unfinished work.
4. Do not silently resume mutations. Follow the user's current request and current authorization.

Read historical files only when the user asks for older context or `LATEST.md` explicitly points to information required for the current task.

## Archive

Summarize the current conversation for a capable agent that has no chat history. Include only information that materially reduces rediscovery:

- objective and scope;
- confirmed facts and important constraints;
- decisions and their reasons;
- files materially changed or inspected;
- verification actually run and its result;
- unresolved questions, blockers, and risks;
- a concrete recommended next action.

Keep facts, decisions, and proposals distinct. Never claim a command, build, test, edit, or decision occurred unless it did. Exclude greetings, conversational filler, abandoned speculation, large logs, source-code dumps, credentials, tokens, personal data, and unrelated repository details.

Use the schema in [references/record-format.md](references/record-format.md). Create the timestamped archive record first, then replace `LATEST.md` with the same content plus its archive path. Preserve older archive files. If there is no meaningful project context to preserve, say so instead of creating noise.

After writing, reread both files and confirm that:

- the archive path and timestamp agree;
- `LATEST.md` is self-contained and points to the new archive record;
- pending work is not described as completed;
- no sensitive or bulky content was captured.

Report the two written paths and a one-sentence summary to the user.

## Boundaries

- A skill cannot reliably detect that the user has closed a conversation. Archive only when requested or when a higher-priority repository instruction explicitly requires it at a defined point.
- Do not edit product documentation, source files, issues, commits, or external systems as part of memory maintenance.
- Do not use memory records to override the current user request, `AGENTS.md`, repository state, or newer evidence.
- Keep each record brief: normally 200-500 Chinese characters, expanding only when the handoff would otherwise omit a material risk or decision.
