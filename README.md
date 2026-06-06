# Squid Storage

[![CMake Build](https://github.com/MHS-20/Squid-Storage/actions/workflows/cmake-build.yml/badge.svg)](https://github.com/MHS-20/Squid-Storage/actions/workflows/cmake-build.yml)

<div align="center">
<img src="squid.png" alt="Storage Logo" width="250">
</div>

# SquidStorage

SquidStorage is a distributed file storage system written in C++ for Unix. It provides transparent file access through a FUSE filesystem, replicating data across a cluster of storage nodes with automatic failover, quorum-based consistency, and server-side push invalidation.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        FUSE Client                              │
│   /mnt/squid  ←──── SquidFS (FUSE layer) ←──── Client           │
└────────────────────────────┬────────────────────────────────────┘
                             │  SquidProtocol (TCP)
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Primary Server                              │
│  ┌──────────────┐  ┌─────────────────┐  ┌──────────────────┐    │
│  │ LockManager  │  │ReplicationMgr   │  │HeartbeatManager  │    │
│  └──────────────┘  └────────┬────────┘  └──────────────────┘    │
│                             │                                   │
│  ┌──────────────────────────┼──────────────────────────────┐    │
│  │        StandbyReplicaManager                            │    │
│  │   STATE_SNAP / STATE_DELTA / LEADER_HB fanout           │    │
│  └──────────────────────────┼──────────────────────────────┘    │
└────────────────────────┬────┼────────────────────────────────── ┘
                         │    │ Replication protocol (TCP)
              ┌──────────┘    └────────────────────┐
              ▼                                    ▼
┌─────────────────────┐             ┌──────────────────────────────┐
│     DataNode α      │             │    Standby Servers           │
│  (file storage)     │             │  server_standby1             │
└─────────────────────┘             │  server_standby2             │
┌─────────────────────┐             │  (ReplicaWatcher loop)       │
│     DataNode β      │             └──────────────────────────────┘
│  (file storage)     │
└─────────────────────┘
```

The system has four distinct roles:

- **Primary Server** — coordinates all reads and writes, manages locks, fans out replication to datanodes, and streams state deltas to standby servers.
- **Standby Servers** — shadow the primary by receiving state snapshots and incremental deltas. They monitor the primary with a heartbeat deadline and promote themselves to primary when the primary becomes unreachable.
- **DataNodes** — store raw file bytes on disk. The primary fans out every write to a configurable number of datanodes and enforces a write quorum before acknowledging the client.
- **FUSE Client** — mounts a virtual filesystem at a local mountpoint. All filesystem operations (open, read, write, create, unlink, readdir) translate into SquidProtocol RPCs to the primary.

---

## The SquidProtocol Binary Wire Format

All communication between every role uses a single custom binary protocol called SquidProtocol. Every message on the wire is a **frame** with a fixed-size header followed by a variable-length payload of typed fields.

### Frame Layout

```
 0       1       2       3       4       5       6       7
 ┌───────┬───────┬───────┬───────┬───────┬───────┬───────┐
 │ MAGIC │ MAGIC │ OPCODE│ FLAGS │NFIELDS│               │
 │  0x53 │  0x51 │ 1 byte│ 1 byte│ 1 byte│               │
 └───────┴───────┴───────┴───────┴───────┘               │
 ┌───────────────────────┐   ┌───────────────────────────┐
 │     SEQ (4 bytes)     │   │   PAYLOAD LEN (4 bytes)   │
 └───────────────────────┘   └───────────────────────────┘
 ╔═══════════════════════════════════════════════════════╗
 ║                PAYLOAD (variable)                     ║
 ║  [ FIELD_ID:1 | VALUE_LEN:4 | VALUE:N ] × NFIELDS     ║
 ╚═══════════════════════════════════════════════════════╝
```

**Header** (13 bytes total):

| Bytes | Field | Description |
|---|---|---|
| 0–1 | Magic | `0x5351` (`SQ`) — identifies a SquidProtocol frame |
| 2 | Opcode | Operation type (see table below) |
| 3 | Flags | `0x01` = RESPONSE; `0x00` = request/push |
| 4 | NFields | Number of typed fields in the payload |
| 5–8 | Seq | Monotonically increasing per-connection sequence number |
| 9–12 | PayloadLen | Total byte length of the payload that follows |

**Payload fields** — each field is a triple:

```
┌──────────┬──────────────────────┬───────────────────────┐
│ ID:1byte │ VALUE_LEN: 4 bytes   │ VALUE: VALUE_LEN bytes│
└──────────┴──────────────────────┴───────────────────────┘
```

### Opcodes

| Hex | Name | Direction | Description |
|---|---|---|---|
| `0x01` | `CONNECT_SERVER` | client→server | Initial connection handshake |
| `0x02` | `IDENTIFY` | server→peer | Server announces identity |
| `0x03` | `CLOSE` | any | Graceful connection teardown |
| `0x04` | `HEARTBEAT` | server→client | Keepalive ping |
| `0x05` | `SYNC_STATUS` | client→server | Request full file version map |
| `0x10` | `CREATE_FILE` | client→server | Create a new file |
| `0x12` | `READ_FILE` | client→server | Fetch file bytes |
| `0x13` | `UPDATE_FILE` | client→server | Overwrite file contents |
| `0x14` | `DELETE_FILE` | client→server | Remove a file |
| `0x20` | `ACQUIRE_LOCK` | client→server | Request exclusive write lock |
| `0x21` | `RELEASE_LOCK` | client→server | Surrender write lock |
| `0x30` | `PUSH_CREATE_FILE` | server→client | Unsolicited new-file notification |
| `0x31` | `PUSH_UPDATE_FILE` | server→client | Unsolicited update notification |
| `0x32` | `PUSH_DELETE_FILE` | server→client | Unsolicited delete notification |
| `0x40` | `STATE_SNAP` | primary→standby | Full state snapshot on first connect |
| `0x41` | `STATE_DELTA` | primary→standby | Incremental state update |
| `0x42` | `LEADER_HB` | primary→standby | Leader heartbeat carrying epoch |
| `0x43` | `NACK_STALE_EPOCH` | peer→stale server | Epoch-fencing rejection |
| `0xFF` | `RESPONSE` | any | ACK or NACK in reply to a request |

### Field IDs

| Hex | Name | Type | Usage |
|---|---|---|---|
| `0x01` | `FILE_PATH` | string | Relative file path |
| `0x02` | `FILE_VERSION` | uint32 | Monotonic version counter |
| `0x03` | `NODE_TYPE` | string | `CLIENT`, `DATANODE`, `STANDBY` |
| `0x04` | `PROCESS_NAME` | string | Unique node identity |
| `0x05` | `PORT` | uint32 | Port number |
| `0x06` | `ACK` | bool | `true`=ACK, `false`=NACK |
| `0x07` | `IS_LOCKED` | bool | Lock state |
| `0x08` | `TIMESTAMP` | uint64 | File modification time |
| `0x10` | `FILE_ENTRY` | string | Filename in a directory listing |
| `0x11` | `VER_ENTRY` | string | Version map entry |
| `0x20` | `EPOCH` | uint32 | Leadership epoch for fencing |
| `0x21` | `DELTA_OP` | uint8 | `0`=CREATE/UPDATE, `1`=DELETE |
| `0x22` | `DATANODE_NAME` | string | Datanode identity (repeatable) |
| `0x23` | `SNAP_ENTRY` | string | `"path version dn1,dn2"` packed line |

### Sequence Numbers and Reordering

Every frame carries a monotonically increasing per-connection sequence number stamped into bytes 5–8 of the header. The receiver maintains an `expectedRecvSeq` counter. Frames that arrive out of order are buffered in a reorder map keyed by sequence number; the next expected frame is delivered to the caller as soon as the gap closes. This provides ordered delivery over TCP without requiring a higher-level correlation layer.

### Request/Response Pattern

Client-initiated operations follow a strict synchronous pattern:

```
Client                          Server
  │                               │
  │── CREATE_FILE (seq=N) ───────►│
  │                               │  (fan-out to datanodes, await quorum)
  │◄─ RESPONSE ACK (seq=N+1) ──── │
  │   [FILE_VERSION=3]            │
```

The `RESPONSE` opcode with `FLAG_RESPONSE=0x01` in the flags byte is the universal reply. ACK or NACK is carried in the boolean `ACK` field. When a write is acknowledged the server always echoes back the new authoritative `FILE_VERSION` so the client can update its local version map.

### Push Frames

Server-initiated pushes use the `PUSH_*` opcodes (`0x30`–`0x32`) and carry no sequence correlation with any client request. Because push opcodes are numerically distinct from both request and response opcodes, the client's read loop can classify every incoming frame by opcode alone without ambiguity. A push frame is always immediately followed on the wire by the raw file bytes (for create/update pushes), consumed by a dedicated receive call.

---

## Replication and Consistency

### Write Path

```
Client                 Primary              DataNode α    DataNode β
  │                      │                      │              │
  │── UPDATE_FILE ──────►│                      │              │
  │                      │── UPDATE_FILE ──────►│              │
  │                      │── UPDATE_FILE ────────────────────► │
  │                      │◄─ ACK ───────────────│              │
  │                      │◄─ ACK ────────────────────────────  │
  │                      │  (quorum met)        │              │
  │◄─ ACK (version=N) ── │                      │              │
  │                      │── STATE_DELTA ──────► standbys      │
  │                      │── PUSH_UPDATE ──────► other clients │
```

A write is **committed** only after a quorum of datanodes acknowledge it. The quorum size is:

```
quorum = (replicationFactor / 2) + 1
```

With `replicationFactor=2` the quorum is 2 — both datanodes must ACK. With `replicationFactor=3` the quorum is 2 — any two of three datanodes suffice. If fewer than quorum datanodes ACK, the write is rolled back and the client receives a NACK. This provides **strong write consistency**: a committed version is guaranteed to be present on a majority of datanodes before the client is told it succeeded.

All write fan-out to datanodes happens **concurrently** using a thread pool. The primary launches all write requests in parallel and collects results, rather than waiting for each datanode sequentially. This keeps write latency proportional to the slowest-responding quorum member rather than the sum of all datanode round-trips.

### Read Path

On a normal read the primary queries the first live datanode that holds the file. If that datanode's reported version matches the server's known version, its bytes are returned immediately. If the version is stale (the datanode is behind), the primary queries all holders concurrently and returns the bytes from the one reporting the highest version. If no holder can satisfy the read, the client receives a NACK.

### Versioning

Every file carries a monotonically increasing integer version counter. The server is the sole authority for assigning version numbers — clients never invent them, they only echo back the last version they have seen. The server increments the version on every committed write and returns the new value in the ACK. This version flows into:

- The datanode's local version file (persisted to disk)
- The server's in-memory and persisted version map
- The FUSE client's in-memory version map
- The standby's replicated state

Version numbers are used for conflict detection during sync, for read-quorum staleness detection, and for epoch-fencing during leader failover.

---

## File Synchronisation

When a client (or FUSE client) connects or reconnects, it issues a `SYNC_STATUS` request to the server, which responds with the server's current file version map — a flat mapping of every known file path to its current version.

The client compares this remote map against its local in-memory version map and computes a set of sync operations:

| Condition | Action |
|---|---|
| File exists locally and remotely; local version > remote | `UPLOAD` — push local bytes to server |
| File exists locally and remotely; local version < remote | `DOWNLOAD` — fetch bytes from server |
| File exists locally; not known remotely | `CREATE_REMOTE` — create it on the server |
| File known remotely; not local | `DOWNLOAD` — fetch bytes from server |
| Versions match | No action |

This sync runs once at startup after the initial push burst from the server has been drained, ensuring that any files created or modified while the client was offline are reconciled before normal operation begins.

---

## Server-to-Client Push Invalidation

The primary maintains a registry of all connected clients. After every committed write it pushes the updated file to all other clients using the `PUSH_CREATE_FILE` or `PUSH_UPDATE_FILE` opcodes, which carry both the file metadata and the raw file bytes in a single burst. For deletes it sends `PUSH_DELETE_FILE`.

The FUSE layer responds to incoming pushes by updating its in-memory cache entry (unless that entry is dirty — a locally-modified but not yet flushed write buffer always wins over a push). If the kernel page cache holds stale bytes for the file, `fuse_invalidate_path()` is called to evict them so the next read forces a re-fetch.

---

## Locking

SquidStorage uses **advisory write locks** with server-side expiry. Before writing a file a client calls `ACQUIRE_LOCK`. The lock is per-file, exclusive, and stored in the server's lock map with a holder identity and an expiration timestamp.

Lock semantics:

- Only one client may hold the lock for a given file at a time.
- A second `ACQUIRE_LOCK` from a different client is NACKed immediately.
- Locks carry an expiration time. A background thread on the server periodically scans the lock map. When a lock expires the server forcibly releases it and sends a `RELEASE_LOCK` push to the formerly-holding client so it can clean up its local state.
- When a client calls `RELEASE_LOCK` or the connection closes, the lock is released immediately.

In the FUSE layer, `open()` with write flags triggers `ACQUIRE_LOCK` before populating the file cache. `release()` (called when the last writable file descriptor closes) flushes the dirty write buffer with `UPDATE_FILE` and then calls `RELEASE_LOCK`. Read-only opens acquire no lock.

---

## High Availability and Failover

### Leadership Epochs

Every server session carries a **leadership epoch** — a monotonically increasing uint32 stamped into all replication and heartbeat frames. Epochs start at 0 for the first primary and increment by 1 every time a new leader promotes. The epoch is persisted to disk before the new primary begins accepting connections, so it survives a crash and restart.

Epoch fencing prevents split-brain: when a datanode or client receives a frame carrying an epoch lower than the highest epoch it has already seen, it responds with `NACK_STALE_EPOCH`. A server receiving this response knows it is a stale leader and should step down.

### Primary-to-Standby Replication

When a standby connects to the primary it performs a handshake (IDENTIFY exchange), then receives a full `STATE_SNAP` frame containing every known file path, version, and the set of datanodes holding each file, along with the current epoch. From that point on the primary sends `STATE_DELTA` frames after every committed write and `LEADER_HB` frames at a configurable interval.

The standby's `receiveLoop` maintains a heartbeat deadline. Each arriving `LEADER_HB` or `STATE_SNAP` resets the deadline to `now + heartbeat_timeout_ms`. If the deadline passes without a frame, the standby declares the primary unreachable and begins the promotion probe.

### Promotion

A standby promotes itself to primary only when it has verified that **all higher-priority servers are unreachable**. Priority is determined by declaration order in the cluster config file — the first entry is highest priority. The promotion probe attempts a full TCP connection (not just a port check) to each higher-priority server, with a configurable number of attempts and delay between them. A TCP connect timeout is enforced at the socket level using non-blocking `connect()` + `select()`, so a dead host is detected within one timeout interval rather than the OS default (which can be minutes).

If the probe confirms all higher-priority servers are down, the standby:

1. Increments the observed epoch by 1.
2. Persists the new epoch to disk.
3. Sets its role to ACTIVE.
4. Calls the promotion callback, which starts the Server's accept loop on the same port the primary was using.

All standbys walk the server list in priority order. If `server_standby1` is also dead, `server_standby2` will detect that both `server_primary` and `server_standby1` are unreachable and promote itself.

### Demotion

If a node that believes itself to be ACTIVE receives a `LEADER_HB` from a peer carrying a higher epoch, it knows a legitimate leader has been elected. The demotion callback stops the server's accept loop and the node returns to STANDBY mode.

---

## File Rebalancing

When the primary's heartbeat to a datanode fails, the heartbeat manager:

1. Marks the datanode as dead.
2. Removes it from the replication map for every file it was holding.
3. For each under-replicated file (files that now have fewer holders than `replicationFactor`), triggers a rebalancing operation.

Rebalancing selects replacement datanodes using a round-robin cursor over the live datanode set, fetches the file bytes from a surviving holder, and pushes them to the new holder(s). The replication map is updated and persisted atomically after each successful rebalance.

Datanode selection for new files also uses the same round-robin cursor, distributing file placement evenly across the cluster without a central scheduler.

---

## Concurrency Model

### Server Thread Architecture

The server runs four concurrent threads alongside the main accept thread:

- **Accept thread** — calls `accept()` in a tight loop; each accepted connection is handed off to the request pool.
- **Request pool** — a fixed thread pool handles handshakes, client request dispatch, and datanode fan-out without blocking the accept thread.
- **Heartbeat thread** — periodically pings all datanodes, evicts dead ones, and triggers rebalancing.
- **Lock expiry thread** — periodically scans the lock map and releases expired locks.
- **Standby heartbeat thread** — periodically sends `LEADER_HB` frames to all connected standbys.

The server's shared mutable state (the datanode endpoint map, the client endpoint map) is protected by a `std::shared_mutex`. Concurrent reads (lookups) take a shared lock; writes (registering or removing a connection) take an exclusive lock.

### ConnectionSession and the Worker Thread Model

Every connection — client, datanode, or standby — is wrapped in a `ConnectionSession`. Each session owns a single dedicated worker thread that serialises all I/O for that connection. Two operations are available:

- `call(fn)` — dispatches `fn` to the worker thread and **blocks** the caller until `fn` completes. Used for request/response operations where the result is needed immediately. If called from the worker thread itself (e.g. from within a request handler), `fn` executes directly to avoid deadlock.
- `post(fn)` — enqueues `fn` for **asynchronous** execution on the worker thread and returns immediately. Used for fire-and-forget server-initiated pushes and heartbeats, where blocking the caller on network latency would be wasteful.

This design means the read loop and all outbound writes for a given connection are always sequenced on the same thread, eliminating the need for per-connection locking and preventing interleaving of request and push frames on the wire.

### FUSE Layer Concurrency

The FUSE filesystem runs in single-threaded mode (`fuse_loop` rather than `fuse_loop_mt`). All FUSE callbacks are serialised by libfuse. Shared mutable state inside `SquidFS` (the in-memory file cache) is protected by a `std::mutex` that is dropped around any blocking RPC call to prevent holding the lock while waiting for network I/O.

---

## Storage Layout

All nodes store their files under a configurable root directory, defaulting to `$HOME/SquidStorage` or overridable via the `SQUID_STORAGE_ROOT` environment variable. Metadata is stored in a hidden `.squid/` subdirectory:

```
$SQUID_STORAGE_ROOT/
├── .squid/
│   ├── fileVersions.txt      # path → version, one entry per line
│   └── replicationMap.txt    # path → dn1,dn2, one entry per line
├── myfile.txt
└── docs/
    └── readme.txt
```

Both metadata files are written atomically using a write-to-tmp then `rename()` pattern, ensuring that a crash during a write leaves the previous consistent state intact rather than a partial update.

---

## Cluster Configuration

All nodes — servers, standbys, datanodes, and the FUSE client — share a single INI-style configuration file:

```ini
[servers]
; Declaration order = priority order: first entry is the primary.
server_primary  = 172.20.0.10:12345
server_standby1 = 172.20.0.13:12345
server_standby2 = 172.20.0.14:12345

[replication]
heartbeat_interval_ms    = 2000   ; how often primary sends LEADER_HB
heartbeat_timeout_ms     = 8000   ; standby deadline before promotion probe
reconnect_attempts       = 3      ; reconnect retries after disconnect
reconnect_delay_ms       = 1000   ; delay between reconnect attempts
promotion_probe_attempts = 3      ; probes per higher-priority server
promotion_probe_delay_ms = 500    ; delay between probe attempts
```

The `heartbeat_timeout_ms` should be at least 4× `heartbeat_interval_ms` to absorb transient latency spikes without triggering false promotions. The promotion probe uses a non-blocking TCP connect with a 1-second per-attempt timeout, so the worst-case promotion detection time is:

```
heartbeat_timeout_ms + (promotion_probe_attempts × promotion_probe_delay_ms) + 1s
```

---

## Deployment (Docker Compose)

The reference deployment uses Docker Compose with a private bridge network (`172.20.0.0/16`):

| Container | IP | Role |
|---|---|---|
| `server_primary` | `172.20.0.10` | Primary server |
| `server_standby1` | `172.20.0.13` | Standby (priority 1) |
| `server_standby2` | `172.20.0.14` | Standby (priority 2) |
| `datanode1` | `172.20.0.11` | DataNode α |
| `datanode2` | `172.20.0.12` | DataNode β |
| `fuse_client` | `172.20.0.21` | FUSE client, mounts at `/mnt/squid` |

The FUSE client container requires `SYS_ADMIN` capability, `/dev/fuse` device access, and `apparmor:unconfined` on Ubuntu hosts. The `fuse3` package is installed at image build time.

### Chaos Testing

The deployment is compatible with [Pumba](https://github.com/alexei-led/pumba) for chaos engineering. Recommended scenarios:

- **Kill primary mid-write** — verifies quorum rollback and standby promotion.
- **Kill one datanode** — verifies reads still succeed from the surviving holder and rebalancing restores the replication factor.
- **Network partition** — use `tc netem` (requires `NET_ADMIN` capability) to introduce latency or packet loss and verify heartbeat timeout behaviour.

`restart: on-failure` on server and datanode services enables automatic container restart after a kill, exercising the full crash → restart → rejoin → resync cycle.

---

## Building

```bash
make -j16
```

Produces four binaries:

| Binary | Description |
|---|---|
| `SquidStorageServer` | Primary/standby server |
| `DataNode` | Storage node |
| `SquidFSMount` | FUSE filesystem client |
| `SquidStorageClient` | Headless test client (raw protocol) |

### Dependencies

- C++17 or later
- `libfuse3` (for `SquidFSMount`)
- POSIX sockets, `pthreads`
- CMake 3.15+
