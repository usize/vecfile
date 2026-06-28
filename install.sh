#!/bin/sh
set -e

REPO="usize/vecfile"
VERSION="v0.1.0"
BIN_DIR="$HOME/.claude/bin"
DB_PATH="$HOME/.claude/vecfile.memory.db"
SKILL_DIR="$HOME/.claude/skills/memory"

echo "vecfile installer"
echo "================="
echo

# 1. Download binary
echo "-> Installing binary to $BIN_DIR/vecfile"
mkdir -p "$BIN_DIR"
curl -sL "https://github.com/$REPO/releases/download/$VERSION/vecfile" \
  -o "$BIN_DIR/vecfile"
chmod +x "$BIN_DIR/vecfile"

# 2. Verify hash
echo "-> Verifying SHA256..."
EXPECTED="84b5a628243b8bea8bd0e8408d304fa390154c594ce6a1b3a1b73d1acc2926d7"
ACTUAL=$(shasum -a 256 "$BIN_DIR/vecfile" | cut -d' ' -f1)
if [ "$ACTUAL" != "$EXPECTED" ]; then
  echo "   HASH MISMATCH"
  echo "   expected: $EXPECTED"
  echo "   got:      $ACTUAL"
  echo "   Aborting."
  rm -f "$BIN_DIR/vecfile"
  exit 1
fi
echo "   OK ($ACTUAL)"

# 3. Initialize memory database
if [ ! -f "$DB_PATH" ]; then
  echo "-> Creating memory database at $DB_PATH"
  "$BIN_DIR/vecfile" ns create --db "$DB_PATH" --name default \
    --chunk-size 512 --chunk-overlap 64
else
  echo "-> Memory database already exists at $DB_PATH"
fi

# 4. Install skill
echo "-> Installing skill to $SKILL_DIR"
mkdir -p "$SKILL_DIR"
curl -sL "https://raw.githubusercontent.com/$REPO/$VERSION/skill/SKILL.md" \
  -o "$SKILL_DIR/SKILL.md"

echo
echo "Done. You can now use /memory in Claude Code."
echo
echo "  /memory remember \"something worth keeping\""
echo "  /memory recall \"what was that thing about...\""
echo
"$BIN_DIR/vecfile" --version
