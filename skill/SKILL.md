---
description: Search and manage long-term memory using vecfile
arguments: [action]
argument-hint: [remember|recall|context|forget]
allowed-tools: Bash Read
---

# vecfile memory

You have access to a persistent hybrid search memory system via vecfile.
The binary is at `~/.claude/bin/vecfile` and the database is at `~/.claude/vecfile.memory.db`.

All commands use this base:
```
~/.claude/bin/vecfile --db ~/.claude/vecfile.memory.db
```

## Actions

### remember — store something

When `$action` is "remember" or when you learn something worth retaining,
store it. Use `$ARGUMENTS` (minus the action word) as the content, or
compose your own summary of what was learned.

```bash
~/.claude/bin/vecfile add --db ~/.claude/vecfile.memory.db --ns default \
  --tag "short-label" "content to remember"
```

Use `--tag` to label memories for later retrieval by name. Good tags:
user preferences, project conventions, debugging findings, tool usage patterns.

For file content:
```bash
~/.claude/bin/vecfile add --db ~/.claude/vecfile.memory.db --ns default \
  --file /path/to/file.md
```

### recall — search memory

When `$action` is "recall" or when you need context before answering,
search for relevant memories. Use `$ARGUMENTS` as the query.

```bash
# Search across ALL namespaces (default — searches everything)
~/.claude/bin/vecfile query --db ~/.claude/vecfile.memory.db --all \
  --chunks --limit 5 "query text"

# Search a specific namespace
~/.claude/bin/vecfile query --db ~/.claude/vecfile.memory.db --ns default \
  --chunks --limit 5 "query text"

# Then expand context around the best chunk
~/.claude/bin/vecfile get --db ~/.claude/vecfile.memory.db \
  --chunk CHUNK_ID -C 2
```

Always use `--chunks` so you get chunk IDs for context expansion.
Use `--all` by default to search across every namespace.

### context — load a specific memory

When `$action` is "context", retrieve a specific memory by tag or chunk ID.

```bash
# By tag
~/.claude/bin/vecfile get --db ~/.claude/vecfile.memory.db --tag "tag-name"

# By chunk with surrounding context
~/.claude/bin/vecfile get --db ~/.claude/vecfile.memory.db --chunk N -C 2
```

### forget — remove a memory

When `$action` is "forget", remove content by ID or tag.

```bash
~/.claude/bin/vecfile delete --db ~/.claude/vecfile.memory.db --ns default \
  --path "tag-name"
```

## Guidelines

- **Remember proactively.** When you learn user preferences, project patterns,
  or debugging solutions, store them without being asked.
- **Recall before complex tasks.** Before starting work that might benefit from
  prior context, check memory first.
- **Keep memories atomic.** One concept per memory. A short paragraph is ideal.
- **Tag meaningfully.** Tags like "user-prefers-spaces" or "project-uses-pytest"
  are retrievable by name later.
- **Use --all for recall.** Always search across all namespaces unless the user
  asks for a specific one. Memories and knowledge live in different namespaces.
