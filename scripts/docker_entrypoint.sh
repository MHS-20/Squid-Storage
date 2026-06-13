#!/bin/sh
# docker_entrypoint.sh — clean persisted state on container start, then exec.
#
# Intended as the Docker ENTRYPOINT for all SquidStorage containers.
# If SQUID_STORAGE_ROOT is set, removes .squid/ metadata so every
# `docker compose up` starts from a clean state.

if [ -n "${SQUID_STORAGE_ROOT:-}" ]; then
    rm -rf "${SQUID_STORAGE_ROOT}/.squid"
fi

# Remove stale readiness flag so healthcheck doesn't pass prematurely.
rm -f /tmp/squid_ready

exec "$@"
