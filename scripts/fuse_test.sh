#!/usr/bin/env bash
# fuse_test.sh — exercise SquidFS through standard shell filesystem operations.
#
# Usage: fuse_test.sh <configPath> <processName> <mountpoint>
#
# Exit code: 0 = all checks passed, non-zero = first failure.

set -euo pipefail

CONFIG="$1"
PROCESS="$2"
MNT="$3"

PASS=0
FAIL=0

# ── Helpers ───────────────────────────────────────────────────────────────────

ok() {
  echo "[PASS] $*"
  ((PASS++))
}
fail() {
  echo "[FAIL] $*"
  ((FAIL++))
}

check_eq() {
  local label="$1" got="$2" want="$3"
  if [[ "$got" == "$want" ]]; then
    ok "$label"
  else
    fail "$label: got='$got' want='$want'"
  fi
}

# ── Setup ─────────────────────────────────────────────────────────────────────

echo "=== SquidFS integration test ==="
echo "config=$CONFIG  process=$PROCESS  mountpoint=$MNT"

# Install fuse3 if the image doesn't have it (Arch base image).
if ! command -v fusermount3 &>/dev/null; then
  echo "[setup] Installing fuse3..."
  pacman -Sy --noconfirm fuse3 2>/dev/null || true
fi

mkdir -p "$MNT"

# Give the rest of the cluster 2 s to be ready before connecting.
sleep 2

# Start the FUSE daemon in the background.
/app/bin/SquidFSMount "$MNT" "$CONFIG" "$PROCESS" &
FUSE_PID=$!
echo "[setup] SquidFSMount pid=$FUSE_PID"

# Wait for the mountpoint to become a real FUSE mount (up to 10 s).
for i in $(seq 1 20); do
  if mountpoint -q "$MNT"; then
    echo "[setup] Mounted after ${i}×0.5 s"
    break
  fi
  sleep 0.5
done

if ! mountpoint -q "$MNT"; then
  echo "[FATAL] $MNT is not mounted after 10 s — aborting"
  kill "$FUSE_PID" 2>/dev/null || true
  exit 1
fi

# ── Test cases ────────────────────────────────────────────────────────────────

# ── 1. Create a file at the root ─────────────────────────────────────────────
echo "hello squid" >"$MNT/hello.txt"
GOT=$(cat "$MNT/hello.txt")
check_eq "root file write+read" "$GOT" "hello squid"

# ── 2. Overwrite the file ─────────────────────────────────────────────────────
echo "updated content" >"$MNT/hello.txt"
GOT=$(cat "$MNT/hello.txt")
check_eq "root file overwrite" "$GOT" "updated content"

# ── 3. Append to the file ─────────────────────────────────────────────────────
echo "second line" >>"$MNT/hello.txt"
LINES=$(wc -l <"$MNT/hello.txt")
check_eq "root file append line count" "$LINES" "2"

# ── 4. Create a subdirectory and a file inside it ────────────────────────────
mkdir -p "$MNT/docs"
echo "document body" >"$MNT/docs/readme.txt"
GOT=$(cat "$MNT/docs/readme.txt")
check_eq "subdir file write+read" "$GOT" "document body"

# ── 5. List the root — both entries must appear ───────────────────────────────
ROOT_LISTING=$(ls "$MNT")
if echo "$ROOT_LISTING" | grep -q "hello.txt"; then
  ok "root listing contains hello.txt"
else
  fail "root listing missing hello.txt (got: $ROOT_LISTING)"
fi
if echo "$ROOT_LISTING" | grep -q "docs"; then
  ok "root listing contains docs/"
else
  fail "root listing missing docs/ (got: $ROOT_LISTING)"
fi

# ── 6. List the subdirectory ─────────────────────────────────────────────────
DOCS_LISTING=$(ls "$MNT/docs")
if echo "$DOCS_LISTING" | grep -q "readme.txt"; then
  ok "docs/ listing contains readme.txt"
else
  fail "docs/ listing missing readme.txt (got: $DOCS_LISTING)"
fi

# ── 7. Nested subdirectory ────────────────────────────────────────────────────
mkdir -p "$MNT/a/b/c"
echo "deep file" >"$MNT/a/b/c/deep.txt"
GOT=$(cat "$MNT/a/b/c/deep.txt")
check_eq "nested subdir file write+read" "$GOT" "deep file"

# ── 8. cp copies a file correctly ────────────────────────────────────────────
cp "$MNT/hello.txt" "$MNT/hello_copy.txt"
GOT=$(cat "$MNT/hello_copy.txt")
WANT=$(cat "$MNT/hello.txt")
check_eq "cp produces identical content" "$GOT" "$WANT"

# ── 9. mv renames a file ─────────────────────────────────────────────────────
# mv is rename(2) → unlink+create on FUSE when crossing device boundaries.
echo "to be moved" >"$MNT/move_src.txt"
mv "$MNT/move_src.txt" "$MNT/move_dst.txt"
if [[ ! -e "$MNT/move_src.txt" ]]; then
  ok "mv: source is gone"
else
  fail "mv: source still exists"
fi
GOT=$(cat "$MNT/move_dst.txt")
check_eq "mv: destination content" "$GOT" "to be moved"

# ── 10. Delete a file ─────────────────────────────────────────────────────────
rm "$MNT/hello_copy.txt"
if [[ ! -e "$MNT/hello_copy.txt" ]]; then
  ok "unlink removes file from mount"
else
  fail "unlink: file still visible after rm"
fi

# ── 11. Read-only open does not block a concurrent reader ────────────────────
# Two background readers should both complete without deadlock.
echo "concurrent" >"$MNT/concurrent.txt"
cat "$MNT/concurrent.txt" >/dev/null &
PID1=$!
cat "$MNT/concurrent.txt" >/dev/null &
PID2=$!
wait "$PID1" && wait "$PID2"
ok "two concurrent read-only opens completed"

# ── 12. stat shows correct file type ─────────────────────────────────────────
if [[ -f "$MNT/hello.txt" ]]; then
  ok "stat: hello.txt is a regular file"
else
  fail "stat: hello.txt is not a regular file"
fi
if [[ -d "$MNT/docs" ]]; then
  ok "stat: docs is a directory"
else
  fail "stat: docs is not a directory"
fi

# ── Teardown ──────────────────────────────────────────────────────────────────

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="

# Gracefully unmount before killing the daemon.
fusermount3 -u "$MNT" 2>/dev/null || umount "$MNT" 2>/dev/null || true
sleep 0.5
kill "$FUSE_PID" 2>/dev/null || true

[[ $FAIL -eq 0 ]]
