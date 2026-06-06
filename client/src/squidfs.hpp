#pragma once

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "client.hpp"

// Per-file in-memory state, shared across all fds that have the same path open.
struct CacheEntry {
  std::vector<uint8_t> data;
  bool dirty = false;
  int lockCount = 0; // fds that hold the server lock
  int openCount = 0; // total fds (including read-only)
  int version = 0;
};

// Per-fd state stored in fuse_file_info::fh.
// Heap-allocated in op_open/op_create, deleted in op_release.
struct FileHandle {
  std::string path; // relative path this handle refers to
  bool writable;    // true  → lock was acquired, must release on close
                    // false → read-only open, no lock held
};

class SquidFS {
public:
  SquidFS(const std::string &mountpoint, Client &client);
  ~SquidFS();
  int run();
  void invalidatePath(const std::string &path);
  static SquidFS *instance();

private:
  int op_getattr(const char *path, struct stat *st, struct fuse_file_info *fi);
  int op_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi,
                 enum fuse_readdir_flags flags);
  int op_mkdir(const char *path, mode_t mode);
  int op_rmdir(const char *path);
  int op_open(const char *path, struct fuse_file_info *fi);
  int op_create(const char *path, mode_t mode, struct fuse_file_info *fi);
  int op_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi);
  int op_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi);
  int op_release(const char *path, struct fuse_file_info *fi);
  int op_unlink(const char *path);
  int op_truncate(const char *path, off_t size, struct fuse_file_info *fi);

  // Fetch file bytes from the server into entry. cacheMutex_ must be held
  // by the caller, and is dropped + re-acquired around the blocking RPC.
  // Returns 0 on success or a negative errno.
  int fetchIntoCache(std::unique_lock<std::mutex> &lock,
                     const std::string &relPath, CacheEntry &entry);

  // Build (or return cached) snapshot of the server version map.
  // The map is re-read from disk; no network call.
  std::map<std::string, int> localVersionMap() const;

  // Return the set of immediate children of `dir` (empty string = root).
  // Children that are files are returned as plain names; subdirectory
  // components are returned with a trailing '/'.
  // Derived entirely from localVersionMap() — no network call.
  std::set<std::string> listChildren(const std::string &dir) const;

  // Returns true if `dir` (relative, no trailing slash) is a prefix of at
  // least one known file path. Used by getattr to synthesise directory stats.
  bool isKnownDir(const std::string &dir) const;

  // Strip the single leading '/' that FUSE puts on every path argument.
  static std::string rel(const char *path);

  // ── State ────────────────────────────────────────────────────────────────
  std::string mountpoint_;
  Client &client_;

  mutable std::mutex cacheMutex_;
  std::map<std::string, CacheEntry> cache_; // keyed by relative path

  struct fuse *fuse_ = nullptr;
  struct fuse_args fargs_{};

  static SquidFS *instance_;

  // ── FUSE C-callback trampolines ──────────────────────────────────────────
  static int c_getattr(const char *, struct stat *, struct fuse_file_info *);
  static int c_readdir(const char *, void *, fuse_fill_dir_t, off_t,
                       struct fuse_file_info *, enum fuse_readdir_flags);
  static int c_mkdir(const char *, mode_t);
  static int c_rmdir(const char *);
  static int c_open(const char *, struct fuse_file_info *);
  static int c_create(const char *, mode_t, struct fuse_file_info *);
  static int c_read(const char *, char *, size_t, off_t,
                    struct fuse_file_info *);
  static int c_write(const char *, const char *, size_t, off_t,
                     struct fuse_file_info *);
  static int c_release(const char *, struct fuse_file_info *);
  static int c_unlink(const char *);
  static int c_truncate(const char *, off_t, struct fuse_file_info *);

  static const fuse_operations ops_;
};
