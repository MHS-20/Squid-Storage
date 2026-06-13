# SquidStorage — Improvement Plan

Priority tiers: P0 = crash/data-loss risk, P1 = correctness, P2 = resilience, P3 = quality.

---

## Phase 1 — Correctness (P1)

### 1.1 Regression test for syncStatus push consumption

The fix in `SquidProtocol::syncStatus()` (looping until `RESPONSE`/`ACK`,
consuming intervening pushes) has no unit test.

**Fix:** Add an integration test via `PairedChannel`:
- Server enqueues `PUSH_CREATE_FILE` + file data frames before responding to
  `SYNC_STATUS`.
- Client calls `syncStatus()` and asserts it returns the `RESPONSE` (not a
  push), and that file data trailing the consumed pushes doesn't corrupt the
  next read (verify by doing a subsequent `syncStatus()` or `heartbeat()`).

**Test:** `IntegrationTest.SyncStatusConsumesPushes` in `test_integration.cpp`.

---

## Phase 2 — Resilience (P2)

### 2.1 Full-rewrite state persistence

`StateManager::saveVersionMap()` (`server/src/replication_manager.cpp:56`)
rewrites the entire file on every change. For N files, every write triggers
O(N) I/O.

**Fix:** Implement WAL-style incremental persistence:
- Append-only log for version/replication changes.
- Periodic compaction (rewrite full state after M entries).
- On startup, replay the log.

**Test:**
- Unit test: simulate M mutations, kill the process, restart, verify state is
  recovered correctly.
- Docker: create/update/delete files, kill the server, restart, verify state.

**Risk:** Invasive change to `server/src/state_manager.hpp/.cpp`. Must not break
atomic-write guarantees (write-to-tmp-then-rename already exists).

---

### 2.2 No timeout on RPC calls

`ConnectionSession::call()` uses `future.get()` which blocks indefinitely. If
the server hangs, the FUSE thread (and thus the entire mount) hangs.

**Fix:** Add a configurable timeout to `call()`:
```cpp
template <typename Fn, typename Rep, typename Period>
auto call(Fn &&fn, std::chrono::duration<Rep, Period> timeout)
    -> std::invoke_result_t<Fn, SquidProtocol &>
```
On timeout, set `alive_ = false`, close the channel, and throw
`std::runtime_error`. The caller (`Client::ensureConnected()`) can catch and
attempt reconnect.

**Test:** Create a `TestChannel` that never responds. Verify that `call()` with
a short timeout throws, then `ensureConnected()` reconnects.

---

### 2.3 Thread detach in ConnectionSession::stop()

`ConnectionSession::stop()` (`common/src/networking/ConnectionSession.hpp:49`)
detaches the worker thread when called from inside the worker itself (e.g., the
last `shared_ptr` is released inside a requestHandler callback). The detached
thread may still be running during destruction.

**Fix:** Restructure ownership so `stop()` is never called from the worker
thread. Options:
- Use `weak_ptr<ConnectionSession>` in request handlers instead of capturing
  `shared_from_this()`.
- Or, signal the worker to stop and let it self-destruct after the loop exits
  (the thread function returns, cleaning up the local `shared_ptr`).

**Test:** Hard to unit-test reliably (race condition). Stress-test with rapid
connect/disconnect cycles.

---

### 2.4 FUSE client idle connection health monitoring

Reconnect only happens when a FUSE operation triggers an RPC. If the mount sits
idle, a dead connection is not detected until someone accesses a file.

**Fix:** In `SquidFS::run()`, launch a background monitor thread that
periodically (every 5s) calls `client_.heartbeat()`. If the heartbeat fails,
`ensureConnected()` triggers reconnect.

**Test:** `scripts/fuse_test.sh` with a sleep + kill + access sequence.

---

## Phase 3 — Quality & Consistency (P3)

### 3.1 FileTransfer::BUFFER_SIZE (1024) is small

`common/src/filesystem/filetransfer.hpp:13` defines `#define BUFFER_SIZE 1024`.
File transfers use 1 KB chunks with individual `read()` syscalls. For large
files this is inefficient.

**Fix:** Increase `BUFFER_SIZE` to 1 MB (or use a dynamically-sized buffer
based on the file size). Replace the `#define` with a `constexpr` or `static
const` to avoid macro namespace pollution.

**Test:** Existing protocol tests (`FileTransferThroughPairedChannel`) should
still pass. Performance benchmark is manual.

---

### 3.2 Client::handlePush does blocking I/O on worker thread

`Client::handlePush()` (`client/src/client.cpp:80`) calls
`session.call([&](SquidProtocol &proto) { proto.receiveFileData(data); })`
for `PUSH_CREATE_FILE`/`PUSH_UPDATE_FILE` frames. Since the handler is already
running on the worker thread, `call()` takes the direct path and
`receiveFileData()` does blocking `socket::read()` calls. While this blocks,
the worker cannot process other tasks (heartbeats, other pushes, RPC responses).

**Fix:** Since `handlePush` is already on the worker thread, call
`receiveFileData()` directly (not through `session.call()`). Or use
non-blocking reads with the existing `select()` loop.

**Test:** Existing push tests pass.

---

## Testing Summary

| Area | Tool | What to verify |
|---|---|---|
| Unit tests | `build/tests/SquidStorageTests` | All 91+ tests pass (GTest) |
| Integration tests | `test_integration.cpp` | PairedChannel-based protocol tests |
| Docker compose | `make reset-cluster` | Full cluster startup, file propagation |
| Failover | `docker compose stop server_primary` | Client reconnects to standby, files accessible |
| FUSE integration | `scripts/fuse_test.sh` | 12 FUSE operations (write, read, mkdir, etc.) |
| Chaos | `docker compose --profile chaos up` | Random SIGKILL of primaries every 30s |

### Docker compose test procedure for failover

```bash
# Terminal 1: cluster logs
docker compose up -d && docker compose logs -f

# Terminal 2: FUSE operations
docker compose exec fuse_client sh -c "
  ls /mnt/squid/
  cat /mnt/squid/hello.txt
  echo test > /tmp/x && cp /tmp/x /mnt/squid/newfile.txt
"

# Kill primary
docker compose stop server_primary

# Verify failover
docker compose exec fuse_client sh -c "
  ls /mnt/squid/          # should still list files
  cat /mnt/squid/newfile.txt  # should work after reconnect
"
```

---

## Implementation Order

1. **Phase 1:** 1.1
2. **Phase 2:** 2.2 → 2.4 → 2.3 → 2.1
3. **Phase 3:** 3.1 → 3.2
