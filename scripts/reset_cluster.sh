#!/usr/bin/env bash
# reset_cluster.sh — wipe all persisted state and restart the Docker cluster.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== Stopping cluster ==="
docker compose -f "$REPO_DIR/docker-compose.yaml" down 2>/dev/null || true

echo "=== Wiping persisted state (.squid/) ==="
find "$REPO_DIR/mockfiles" -name '.squid' -type d -exec rm -rf {} + 2>/dev/null || true
echo "  done"

echo "=== Starting cluster ==="
docker compose -f "$REPO_DIR/docker-compose.yaml" up -d

echo "=== Cluster is fresh ==="
