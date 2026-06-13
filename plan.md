# SquidStorage — Codebase Audit

Priority tiers: **P0** = data-loss / protocol-corruption / use-after-free,  
**P1** = correctness / silent-failure,  
**P2** = resilience / edge-case,  
**P3** = quality / portability.

---

## P0 — CRITICAL

---

### P0.1 `readSuspended_` dead code — protocol corruption

The server calls `suspendReads()` before offloading `handleClientRequest` to the thread
pool, intending to pause the session worker's read loop so that incoming bytes (e.g. file
data for `CREATE_FILE`/`UPDATE_FILE`) are not interleaved with other frames. However, the
`run()` loop **never checks `readSuspended_`**, so the flag is completely ignored.

**Impact:** Protocol frames can be read and dispatched concurrently with a pool-thread
handler execution, causing out-of-order or interleaved message processing.

**Files:**
- `common/src/networking/ConnectionSession.hpp:24` — declaration
- `common/src/networking/ConnectionSession.hpp:254-255` — setter / getter
- `common/src/networking/ConnectionSession.hpp:306-347` — `run()` loop never reads it
- `server/src/server.cpp:181,184` — callers that believe it works

**Fix:** Check `readSuspended_` in `run()` after `select()` returns; skip processing while
suspended.

---

### P0.2 Use-after-free in PushHandler lambda

`SquidFS` constructor captures `this` (raw `SquidFS*`) into a lambda stored in
`Client::pushHandler_`. In `squidfs_main.cpp`, `Client client` is declared before
`SquidFS fs`. On scope exit, `~SquidFS()` runs **first**, then `~Client()` (which joins the
worker thread). Between the two, the ConnectionSession worker thread can receive a PUSH
frame and invoke `pushHandler_()` on a **dangling `SquidFS*`**.

**Impact:** Use-after-free crash or silent corruption when pushes arrive during shutdown.

**Files:**
- `client/src/squidfs.cpp:18-47` — lambda captures `this`
- `client/src/squidfs_main.cpp:60-67` — declaration order (Client before SquidFS)

**Fix:** Clear the push handler (`client_.setPushHandler({})`) in `~SquidFS()`.

---

### P0.3 Dangling reference in `op_release()` after lock drop

`op_release()` captures `auto &entry = it->second` while holding `cacheMutex_`, then
drops and re-acquires the lock multiple times. The PushHandler (running on the worker
thread) can acquire `cacheMutex_` and erase `cache_[r]` between unlock and re-lock,
invalidating the `entry` reference. Subsequent accesses (`entry.openCount`,
`entry.dirty`) are use-after-free.

**Impact:** Undefined behavior on any overwrite / release when a concurrent push arrives.

**File:** `client/src/squidfs.cpp:465-501`

**Fix:** Re-`find()` the entry after re-acquiring the lock; bail out if erased.

---

### P0.4 `fetchIntoCache()` writes to invalidated reference

Same pattern as P0.3: `entry` reference obtained from `cache_[r]` under lock. Lock is
dropped for the blocking `readFile()` RPC. PushHandler can erase `cache_[r]` during
that window. Writing `entry.data` / `entry.version` / `entry.dirty` after re-lock is
use-after-free.

**Impact:** UB on any cache fill that races with a push.

**File:** `client/src/squidfs.cpp:190-206`

**Fix:** Re-`find()` after re-acquiring the lock; create a fresh entry if erased.

---

### P0.5 Future-index misalignment in propagation loops — silent data corruption

`propagateCreateFile`, `propagateUpdateFile`, and `HeartbeatManager::sendHeartbeats`
collect futures in a first pass (skipping dead sessions), then consume them in a second
pass using a separate counter. If `isAlive()` state changes between passes, indices
desync and responses get attributed to the **wrong datanode**. In `heartbeat_manager.cpp`,
the second loop indexes `futures[i]` by the `datanodes` vector index, but `futures` may
have fewer entries — **out-of-bounds access**.

**Impact:** Silent data corruption (wrong ACK counted for quorum), false heartbeat
failures, or UB from OOB access.

**Files:**
- `server/src/replication_manager.cpp:175-185` (create)
- `server/src/replication_manager.cpp:272-282` (update)
- `server/src/heartbeat_manager.cpp:40-50`

**Fix:** Use a single pass that submits and consumes each future immediately, or pair each
future with the datanode name.

---

### P0.6 Data race on `protocol_.alive_`

`SquidProtocol::alive_` is a plain `bool`. The worker thread writes it (receive errors,
send failures, `closeConn`). `ConnectionSession::isAlive()` reads it and can be called
from any thread. Concurrent non-atomic read/write is UB — the compiler may cache the
value in a register and never see updates from the writing thread.

**Impact:** Stale `isAlive()` results leading to connection mismanagement; theoretically
UB on every session access from a non-worker thread.

**Files:**
- `common/src/squidprotocol/squidprotocol.hpp:109` — declaration as `bool`
- `common/src/networking/ConnectionSession.hpp:62` — cross-thread reader

**Fix:** Make `alive_` `std::atomic<bool>` and ensure all writes go through the atomic.

---

### P0.7 No upper bound on wire `payloadLen` — OOM / crash

`payloadLen` is a 32-bit value decoded directly from the wire without any limit check.
`frame.resize(before + payloadLen)` with `payloadLen = 0xFFFFFFFF` attempts a ~4 GiB
allocation. The `FileTransfer` layer enforces a 1 GiB limit (`FILETRANSFER_MAX_SIZE`),
but the raw frame parser does not.

**Impact:** Remote-triggerable OOM crash.

**File:** `common/src/squidprotocol/squidprotocol.cpp:122-127`

**Fix:** Reject frames with `payloadLen > MAX_PAYLOAD_SIZE` (e.g. 16 MiB).

---

### P0.8 `ReplicaWatcher::onPromote_` blocks forever — no demotion, split-brain risk

The `onPromote_` callback creates a `Server` on the stack and calls `server.run()`,
which **never returns** (it joins the accept thread). The watcher thread is permanently
blocked inside `promote()` and never reaches the `watchLoop()` again. If a higher-epoch
leader appears later, the promoted standby cannot detect or demote. The entire demotion
path is non-functional.

**Impact:** Permanent split-brain after a mistaken promotion.

**Files:**
- `server/src/main.cpp:60-65` — `onPromote_` blocks on `server.run()`
- `server/src/replica_watcher.cpp:373` — never returns from `promote()`

**Fix:** Run the `Server` on a separate thread, or have the watcher own the `Server` and
manage its lifetime.

---

### P0.9 Data race on `standbyReplicaManager_` and `standby_` raw pointer

`standbyReplicaManager_` (a `unique_ptr`) is read by the `standbyHbThread_` loop (its
own thread) and written by `handleStandbyConnect` (a pool thread) — no synchronization.
Similarly, `ReplicationManager::standby_` (a raw pointer) is written by
`setStandbyReplicaManager()` (pool thread) and read by propagation methods (pool
threads) without any mutex. Concurrent read/write of non-atomic pointers is UB.

**Impact:** UB; standby heartbeats may read a partially-written pointer.

**Files:**
- `server/src/server.cpp:91-92` (read in `standbyHbThread_` loop)
- `server/src/server.cpp:206` (write in `handleStandbyConnect`)
- `server/src/replication_manager.hpp:86` (raw pointer declaration)
- `server/src/replication_manager.cpp:215,303,374` (reads)

**Fix:** Protect with `stateMutex_` or use `std::atomic<StandbyReplicaManager*>`.

---

### P0.10 `FileManager` not thread-safe (shared across session workers)

`FileManager` has no mutex. Multiple `ConnectionSession` workers (one per accepted
connection) share the same `FileManager` reference and call `setFileVersion()`,
`getFileVersionMap()`, `deleteFileAndVersion()` concurrently — all modify a
`std::map` without synchronization.

**Impact:** UB on any concurrent metadata access (every server handles concurrent
client/datanode sessions).

**File:** `common/src/filesystem/filemanager.hpp:88-89`

**Fix:** Add a `mutable std::mutex` and lock in every public method.

---

### P0.11 Data race on `roundRobinCursor_`

`pickDataNodes()` acquires a `shared_lock<shared_mutex>` on `stateMutex_`, which allows
multiple readers. Inside the lock, it reads AND writes `roundRobinCursor_` (a plain
`size_t`). Two concurrent callers both hold a shared lock and both mutate the cursor —
data race.

**Impact:** Stale/duplicate datanode selection on concurrent replication.

**File:** `server/src/replication_manager.cpp:503-517`

**Fix:** Make `roundRobinCursor_` `std::atomic<size_t>` or use `unique_lock`.

---

## P1 — HIGH (correctness bugs, likely failures)

---

### P1.1 Stale `versions_` map after health-monitor-initiated reconnect

The health monitor calls `client_.reconnect()` but never calls `syncStatus()`. The
in-memory `versions_` map is never refreshed, so `op_getattr` reports files that were
remotely deleted or returns `-ENOENT` for files created by another client. This persists
until the connection breaks again and a FUSE op triggers the natural reconnect path
(which does call `syncStatus`).

**Files:**
- `client/src/squidfs.cpp:96-113` (health monitor)
- `client/src/client.cpp:27-41` (`ensureConnected` skips sync when alive)

**Fix:** Call `syncStatus()` after a health-monitor reconnection.

---

### P1.2 `op_read()` may dereference `cache_.end()` after `fetchIntoCache`

After `fetchIntoCache()` (which drops and re-acquires the lock), `it = cache_.find(r)`
can return `end()` if the PushHandler erased the entry during the lock drop. Line 420
dereferences `it` without checking.

**File:** `client/src/squidfs.cpp:417-420`

**Fix:** Check `it` after `find()`; return `-EIO` if erased.

---

### P1.3 Double `fuse_unmount()` call

`run()` calls `fuse_unmount(fuse_)` after `fuse_loop()` returns. `~SquidFS()` calls
`fuse_unmount(fuse_)` again on the same pointer. Double unmount is undefined behaviour
per libfuse3 documentation.

**Files:**
- `client/src/squidfs.cpp:54-57` (destructor)
- `client/src/squidfs.cpp:92` (run())

**Fix:** Set `fuse_ = nullptr` after unmount in `run()`; check for null in destructor.

---

### P1.4 Static `instance_` nulled before FUSE teardown complete

`instance_` is set to `nullptr` in the destructor. Between that point and the FUSE device
being fully torn down, the kernel may still deliver callbacks (`c_getattr`, etc.) which
dereference `instance_` without a null-check — null pointer dereference.

**File:** `client/src/squidfs.cpp:60,533-566`

**Fix:** Null `instance_` **after** `fuse_destroy()`, not before.

---

### P1.5 Inconsistent `versions_` / `cache_` view on push

`PUSH_DELETE_FILE` erases `versions_` under `versionMutex_` first, then the cache under
`cacheMutex_` second. A racing FUSE operation can see the version gone but the cache
still present (or vice versa), leading to stale `getattr` results.

**File:** `client/src/client.cpp:88-143`

**Fix:** Erase cache first (under cacheMutex), then version (under versionMutex), to
prevent FUSE from seeing a stale cache entry after the version is gone.

---

### P1.6 Data race on datanode `lastSeenEpoch_`

`lastSeenEpoch_` is read/written from the worker thread (`makeRequestHandler`) and from
the calling thread (`performHandshake`) without synchronization.

**Files:**
- `datanode/src/datanode.cpp:38-39`
- `common/src/peer/peer.hpp:58` (declaration)

**Fix:** Make `lastSeenEpoch_` `std::atomic<uint32_t>`.

---

### P1.7 Orphaned file when `acquireLock` fails after `createFile`

`createFile` succeeds on the server, then `acquireLock` NACKs. The function returns
`-EACCES` without cleaning up the server-side file. The file is permanently orphaned.

**File:** `client/src/squidfs.cpp:377-389`

**Fix:** Call `deleteFile` on the server when `acquireLock` fails after a successful `createFile`.

---

### P1.8 Lock leak when cache entry evicted between `op_open` and `op_release`

If the PushHandler erases the cache entry before `op_release` runs, `cache_.find(r)`
returns `end()` and the function returns 0 without releasing the server lock.

**File:** `client/src/squidfs.cpp:460-463`

**Fix:** If the entry was erased and the handle was writable, call `releaseLock` before
returning.

---

### P1.9 `op_release()` retries `updateFile` with stale version on NACK

If `updateFile` NACKs, `entry.dirty` remains `true`. On the next release, the same
`entry.version` is used for the retry, but the server's version has already advanced —
causing a version conflict.

**File:** `client/src/squidfs.cpp:477-488`

**Fix:** After a NACK, fetch the current server version (via `syncStatus` or similar) before
retrying.

---

### P1.10 `propagateDeleteFile` returns void — server always responds `true`

The server sends `clientSession.response(true)` regardless of whether the propagation to
datanodes succeeded. Silent failure swallowing.

**File:** `server/src/server.cpp:268-273`

**Fix:** Make `propagateDeleteFile` return `bool` and only ACK if quorum succeeded.

---

### P1.11 `handleAccept` has no exception safety

If `datanodeSession->syncStatus()`, `registerDataNodeFiles()`, or any other call in
`handleAccept` throws, the session was already added to `dataNodeEndpointMap_` and its
worker thread is running. The exception propagates to the pool worker, terminating the
task. The session is orphaned in the map.

**File:** `server/src/server.cpp:110-201`

**Fix:** Wrap in try/catch; on failure, remove from the map and stop the session.

---

## P2 — MEDIUM (resilience, edge cases, latent bugs)

---

### P2.1 `ConnectionSession::call()` RPC timeout leaves queued task

On timeout, `alive_` is set to false and the channel is closed, but the `packaged_task`
is still enqueued. When the worker eventually executes it, the channel is closed, partial
writes may already have consumed a sequence number, and the protocol seq state is
corrupted for the dying connection. Harmless in practice (connection is being torn down),
but the seq corruption could interfere if reconnect reuses the same session object.

**File:** `common/src/networking/ConnectionSession.hpp:92-99`

---

### P2.2 `sendFrame` silently consumes sequence number on write failure

If `writeBytes` returns 0 or negative, `sendFrame` sets `alive_ = false` and returns.
The seq number (`nextSendSeq_`) has already been incremented, so it is consumed even
though the frame was never sent. Subsequent retries on the same connection would send
frames with the wrong seq, but since `alive_` is false the connection is dead.

**File:** `common/src/squidprotocol/squidprotocol.cpp:64-82`

---

### P2.3 `select()` UB with socket fd >= `FD_SETSIZE`

The `run()` loop uses `select()` which has a hard limit of `FD_SETSIZE` (typically 1024).
With fd >= 1024, `select()` writes past the `fd_set` bit array. Should use `poll()` or
`ppoll()`.

**File:** `common/src/networking/ConnectionSession.hpp:332-333`

---

### P2.4 Inconsistent ACK checking across operations

`deleteFile`, `acquireLock`, `releaseLock`, and `heartbeat` in `squidprotocol.cpp` return
the raw `Message` without checking `isAck()`. Callers receive what they assume is a
success but may be operating on a NACK silently.

**Files:**
- `common/src/squidprotocol/squidprotocol.cpp:467` (deleteFile)
- `common/src/squidprotocol/squidprotocol.cpp:474` (acquireLock)
- `common/src/squidprotocol/squidprotocol.cpp:479` (releaseLock)
- `common/src/squidprotocol/squidprotocol.cpp:484` (heartbeat)

---

### P2.5 Health monitor reconnect while FUSE ops in-flight may hang

If the health monitor reconnects while a FUSE operation is blocked inside `sess->call()`,
the old session's worker is stopped (via `disconnect()` → `stop()`). A queued
`packaged_task` may never execute, so `future.get()` in `call()` blocks forever. The
RPC timeout is 0 by default (no timeout).

**Files:**
- `client/src/squidfs.cpp:96-113` (health monitor)
- `common/src/peer/peer.cpp:118-139` (disconnect + reconnect)

---

### P2.6 `LockManager::acquireLock` unlocks and re-locks `stateMutex_` — TOCTOU

Both `acquireLock` and `releaseLock` release the `unique_lock` to call
`buildFileLockMap()` (which acquires `shared_lock`). Between unlock and re-lock, another
thread can modify the file lock map, making the re-check stale.

**File:** `server/src/lock_manager.cpp:49-56,70-78`

---

### P2.7 `push_createFile`/`push_updateFile` sets version before cache update

The version map is updated first, then the cache is updated second (via pushHandler). A
racing `op_getattr` can see the version (reporting the file exists) but find an empty
cache.

**File:** `client/src/client.cpp:88-103`

---

### P2.8 `EpochStore::save()` silently swallows write failures

If `ofstream` construction, writing, or `fsync` fails, the function prints to `cerr` and
returns without error. Callers do not check the return value. A failed epoch persist
could cause split-brain after a crash.

**File:** `common/src/filesystem/epoch_store.hpp:36-60`

---

### P2.9 `ClusterConfig::fromFile()` silently skips bad entries

If a server entry has a bad port (`std::stoi` throws), the catch block prints to `cerr`
and continues parsing without the server. A misconfigured cluster file results in a
server silently missing from the failover list.

**File:** `common/src/config/ClusterConfig.hpp:148-165`

---

### P2.10 `ReplicaWatcher::allHigherPriorityDown()` — UAF risk in async tasks

The lambda passed to `std::async` captures `this` by reference. If the watcher is
destroyed while tasks are in-flight, `this` is dangling. All futures are `.get()` before
returning in the normal path, but exceptions or concurrent shutdown could leave tasks
unjoined.

**File:** `server/src/replica_watcher.cpp:335-348`

---

### P2.11 `dataCopy = entry.data` — blocking large allocation under lock

The full file data vector is copied while holding `cacheMutex_`, blocking the FUSE
thread. For files > 1 MiB this is a latency issue.

**File:** `client/src/squidfs.cpp:473`

---

## P3 — LOW (quality, portability, style)

---

### P3.1 `std::atomic<std::thread::id>` in C++17 (technically UB)

`std::thread::id` is not guaranteed to be trivially copyable until C++20.
`std::atomic<T>` requires trivially copyable `T`. Works on all current toolchains but is
a standards violation.

**File:** `common/src/networking/ConnectionSession.hpp:372`

**Fix:** Guard with `if __cplusplus >= 202002L` or use a different synchronization
mechanism.

---

### P3.2 `select()` doesn't handle `EINTR`

If a signal interrupts `select()`, it returns -1 with `errno == EINTR`. The code
`continue`s, restarting the loop. On systems with frequent signals, the 100ms timeout
budget can be significantly extended.

**File:** `common/src/networking/ConnectionSession.hpp:332-336`

---

### P3.3 `SIGPIPE` ignored per-connection instead of once at startup

Every `SquidProtocol` constructor calls `signal(SIGPIPE, SIG_IGN)`. Should be done once
at process entry.

**File:** `common/src/squidprotocol/squidprotocol.cpp:19`

---

### P3.4 `retryDelaySeconds` parameter silently ignored

The `TCPConnectorChannel` constructor accepts `retryDelaySeconds` but discards it with
`(void)retryDelaySeconds`. Misleading API.

**File:** `common/src/networking/TCPConnectorChannel.cpp:19`

---

### P3.5 `errno` check fragile on immediate `connect()` success

The code checks `errno == EINPROGRESS` without first verifying `connect()` returned -1.
If `connect()` succeeds immediately (loopback), `errno` retains its previous value and
may spuriously enter the non-blocking branch.

**File:** `common/src/networking/TCPConnectorChannel.cpp:45`

**Fix:** `if (rc < 0 && errno == EINPROGRESS)`

---

### P3.6 `printMap` takes non-const reference but is read-only

**File:** `server/src/server.cpp:320-323`

---

### P3.7 `std::max(0, ...)` silently masks underflow bugs

`lockCount` and `openCount` decrements are clamped with `std::max(0, ...)`. A logic bug
causing a double-free would be silently hidden.

**File:** `client/src/squidfs.cpp:467-468`

---

### P3.8 Datanode infinite reconnect loop with no backoff

The `while(true)` loop in `DataNode::run()` has no maximum retry count and no
exponential backoff.

**File:** `datanode/src/datanode.cpp:55-69`

---

### P3.9 `using namespace std;` in headers

`filelock.hpp` and `filetransfer.hpp` have `using namespace std;` at file scope,
polluting the namespace for all includers.

**Files:**
- `common/src/filesystem/filelock.hpp:4`
- `common/src/filesystem/filetransfer.hpp:12`

---

## Implementation Order

1. **P0 items** — data-loss / protocol-corruption / use-after-free fixes
   - P0.1 (`readSuspended_` dead code)
   - P0.2 (PushHandler UAF)
   - P0.3, P0.4 (dangling refs in op_release / fetchIntoCache)
   - P0.5 (future-index misalignment)
   - P0.6 (alive_ data race)
   - P0.7 (payloadLen OOM)
   - P0.8 (promote blocks forever)
   - P0.9 (standbyReplicaManager_ race)
   - P0.10, P0.11 (FileManager / roundRobinCursor_ races)

2. **P1 items** — correctness bugs
   - P1.1, P1.5, P1.7, P1.8, P1.9 (FUSE correctness)
   - P1.2, P1.3, P1.4 (UB / null derefs)
   - P1.6 (datanode epoch race)
   - P1.10, P1.11 (server error handling)

3. **P2 items** — resilience / edge-case hardening

4. **P3 items** — quality / portability cleanup
