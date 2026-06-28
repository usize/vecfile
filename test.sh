#!/bin/sh
# vecfile integration tests
set -e

VF="./vecfile"
DB="/tmp/vecfile-test-$$.db"
PASS=0
FAIL=0

pass() { PASS=$((PASS + 1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

cleanup() { rm -f "$DB"; }
trap cleanup EXIT

echo "vecfile integration tests"
echo "========================="
$VF --version
echo

# ── Basics ───────────────────────────────────────────────────

echo "--- Basics ---"

$VF --version | grep -q "vecfile v" && pass "--version" || fail "--version"
$VF model | grep -q "bge-small" && pass "model" || fail "model"

# ── Namespace CRUD ───────────────────────────────────────────

echo "--- Namespace CRUD ---"

$VF ns create --db "$DB" --name test1 >/dev/null && pass "ns create" || fail "ns create"
$VF ns create --db "$DB" --name test2 --chunk-size 256 --chunk-overlap 32 >/dev/null && pass "ns create (custom chunk)" || fail "ns create (custom chunk)"
$VF ns list --db "$DB" | grep -q "test1" && pass "ns list" || fail "ns list"
$VF ns info --db "$DB" --name test2 | grep -q "chunk_size:     256" && pass "ns info" || fail "ns info"
$VF ns delete --db "$DB" --name test2 >/dev/null && pass "ns delete" || fail "ns delete"
! $VF ns info --db "$DB" --name test2 2>/dev/null && pass "ns delete verified" || fail "ns delete verified"

# ── Add: literal text ────────────────────────────────────────

echo "--- Add: literal text ---"

$VF add --db "$DB" --ns test1 "Semantic search uses vector embeddings." | grep -q "file_id=" && pass "add literal" || fail "add literal"

# ── Add: stdin ───────────────────────────────────────────────

echo "--- Add: stdin ---"

echo "Neural networks learn representations." | $VF add --db "$DB" --ns test1 --tag "neural" - | grep -q "file_id=" && pass "add stdin" || fail "add stdin"

# ── Add: file ────────────────────────────────────────────────

echo "--- Add: file ---"

TMPFILE="/tmp/vecfile-testfile-$$.txt"
echo "SQLite is the most deployed database engine." > "$TMPFILE"
$VF add --db "$DB" --ns test1 --file "$TMPFILE" | grep -q "file_id=" && pass "add --file" || fail "add --file"
rm -f "$TMPFILE"

# ── Add: dedup ───────────────────────────────────────────────

echo "--- Dedup ---"

ID1=$($VF add --db "$DB" --ns test1 "Semantic search uses vector embeddings." | grep -o 'file_id=[0-9]*')
ID2=$($VF add --db "$DB" --ns test1 "Semantic search uses vector embeddings." | grep -o 'file_id=[0-9]*')
[ "$ID1" = "$ID2" ] && pass "dedup returns same id" || fail "dedup returns same id"

# ── Add: empty content rejected ──────────────────────────────

echo "--- Edge: empty content ---"

! $VF add --db "$DB" --ns test1 --tag "empty" 2>/dev/null && pass "empty add rejected" || fail "empty add rejected"

# ── Add: --tag without content rejected ──────────────────────

echo "--- Edge: --tag without content ---"

! $VF add --db "$DB" --ns test1 --tag sometag 2>/dev/null && pass "--tag without content rejected" || fail "--tag without content rejected"

# ── Query modes ──────────────────────────────────────────────

echo "--- Query ---"

$VF query --db "$DB" --ns test1 "vector databases" | grep -q "score=" && pass "hybrid query" || fail "hybrid query"
$VF query --db "$DB" --ns test1 --semantic-only "machine learning" | grep -q "score=" && pass "semantic-only" || fail "semantic-only"
$VF query --db "$DB" --ns test1 --lexical-only "SQLite" | grep -q "score=" && pass "lexical-only" || fail "lexical-only"

# ── Query: --chunks with positional text ─────────────────────

echo "--- Edge: --chunks positional ---"

$VF query --db "$DB" --ns test1 --chunks "embeddings" | grep -q "chunk_id=" && pass "--chunks with query text" || fail "--chunks with query text"
$VF query --db "$DB" --ns test1 --semantic-only --chunks "neural" | grep -q "chunk_id=" && pass "--semantic-only --chunks" || fail "--semantic-only --chunks"

# ── Get: by id, tag, chunk ───────────────────────────────────

echo "--- Get ---"

$VF get --db "$DB" --id 1 | grep -q "Semantic search" && pass "get --id" || fail "get --id"
$VF get --db "$DB" --tag "neural" | grep -q "Neural networks" && pass "get --tag" || fail "get --tag"
$VF get --db "$DB" --chunk 1 | grep -q -i "[a-z]" && pass "get --chunk" || fail "get --chunk"
$VF get --db "$DB" --chunk 1 -C 0 | grep -q -i "[a-z]" && pass "get --chunk -C 0" || fail "get --chunk -C 0"

# ── Get: missing ─────────────────────────────────────────────

echo "--- Edge: get missing ---"

! $VF get --db "$DB" --id 99999 2>/dev/null && pass "get missing id" || fail "get missing id"
! $VF get --db "$DB" --tag "nonexistent" 2>/dev/null && pass "get missing tag" || fail "get missing tag"
! $VF get --db "$DB" --chunk 99999 2>/dev/null && pass "get missing chunk" || fail "get missing chunk"

# ── Delete ───────────────────────────────────────────────────

echo "--- Delete ---"

$VF delete --db "$DB" --ns test1 --path "neural" | grep -q "deleted" && pass "delete --path" || fail "delete --path"
! $VF get --db "$DB" --tag "neural" 2>/dev/null && pass "delete verified" || fail "delete verified"
$VF delete --db "$DB" --ns test1 --id 1 | grep -q "deleted" && pass "delete --id" || fail "delete --id"
$VF delete --db "$DB" --ns test1 --all | grep -q "deleted" && pass "delete --all" || fail "delete --all"
$VF ns info --db "$DB" --name test1 | grep -q "files:          0" && pass "delete --all verified" || fail "delete --all verified"

# ── Summary ──────────────────────────────────────────────────

echo
echo "========================="
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit "$FAIL"
