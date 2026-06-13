#!/usr/bin/env bash
# reset_all.sh — wipe all mockfile state, keeping only seed files.
#
# Removes everything from mockfiles/ except the seed files that each
# component ships with (e.g. alpha_state.txt for datanode_alpha).
# The server directories are completely emptied so the primary starts
# fresh with no pre-existing files.
#
# Seed files (per-component initial data that survives reset):
#   datanode_alpha/alpha_state.txt
#   datanode_beta/beta_state.txt
#
# Usage: scripts/reset_all.sh

set -euo pipefail
cd "$(dirname "$0")/.."

echo "=== Stopping cluster ==="
docker compose down 2>/dev/null || true

# Use a one-shot Alpine container (runs as root) to delete root-owned
# files that the host user cannot remove (created by Docker containers).
CLEAN="docker run --rm -v $(pwd)/mockfiles:/data alpine"

echo "=== Wiping persisted state (.squid/) ==="
$CLEAN sh -c 'find /data -type d -name ".squid" -exec rm -rf {} + 2>/dev/null; exit 0'

echo "=== Removing test artifacts (keeping seed files) ==="

# Servers — full wipe (no seed files).
$CLEAN sh -c '
    for d in server_primary server_standby1 server_standby2 client fuse_client; do
        rm -rf /data/$d/* 2>/dev/null || true
    done
'

# Datanodes — keep seed files, remove everything else.
$CLEAN sh -c '
    for dn in datanode_alpha datanode_beta; do
        find /data/$dn -mindepth 1 -maxdepth 1 \
            ! -name alpha_state.txt \
            ! -name beta_state.txt \
            -exec rm -rf {} + 2>/dev/null || true
    done
'

echo "=== Starting cluster ==="
docker compose up -d
echo "=== Cluster is fresh ==="
