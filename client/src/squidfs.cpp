#define FUSE_USE_VERSION 31
#include "squidfs.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/stat.h>

SquidFS *SquidFS::instance_ = nullptr;

SquidFS::SquidFS(const std::string &mountpoint, Client &client)
    : mountpoint_(mountpoint), client_(client) {

  if (instance_ != nullptr)
    throw std::runtime_error("SquidFS: only one instance allowed");
  instance_ = this;

  client_.setPushHandler(
      [this](const Message &msg, const std::vector<uint8_t> &data) {
        const std::string path = msg.getString(FieldID::FILE_PATH);
        const int version =
            static_cast<int>(msg.getUint32(FieldID::FILE_VERSION, 0));

        switch (msg.opcode) {
        case Opcode::PUSH_CREATE_FILE:
        case Opcode::PUSH_UPDATE_FILE: {
          std::lock_guard<std::mutex> lk(cacheMutex_);
          auto &entry = cache_[path];
          // Never clobber a dirty write buffer — release() will win.
          if (!entry.dirty) {
            entry.data = data;
            entry.version = version;
          }
          break;
        }
        case Opcode::PUSH_DELETE_FILE: {
          std::lock_guard<std::mutex> lk(cacheMutex_);
          cache_.erase(path);
          break;
        }
        default:
          break;
        }

        if (!path.empty())
          invalidatePath(path);
      });
}

SquidFS::~SquidFS() {
  if (fuse_) {
    fuse_unmount(fuse_);
    fuse_destroy(fuse_);
  }
  if (fargs_.argc)
    fuse_opt_free_args(&fargs_);
  instance_ = nullptr;
}

SquidFS *SquidFS::instance() { return instance_; }

int SquidFS::run() {
  char progname[] = "squidfs";
  char *argv_arr[] = {progname};
  fargs_ = FUSE_ARGS_INIT(1, argv_arr);

  fuse_ = fuse_new(&fargs_, &ops_, sizeof(ops_), this);
  if (!fuse_) {
    std::cerr << "[SquidFS]: fuse_new() failed\n";
    return -1;
  }
  if (fuse_mount(fuse_, mountpoint_.c_str()) != 0) {
    std::cerr << "[SquidFS]: fuse_mount() failed for " << mountpoint_ << "\n";
    fuse_destroy(fuse_);
    fuse_ = nullptr;
    return -1;
  }
  std::cout << "[SquidFS]: mounted at " << mountpoint_ << "\n";
  int rc = fuse_loop(fuse_);
  fuse_unmount(fuse_);
  return rc;
}

void SquidFS::invalidatePath(const std::string &path) {
  if (!fuse_)
    return;
  std::string fusePath = "/" + path;
  int rc = fuse_invalidate_path(fuse_, fusePath.c_str());
  if (rc != 0 && rc != -ENOENT)
    std::cerr << "[SquidFS]: fuse_invalidate_path(" << fusePath << ") returned "
              << rc << "\n";
}

std::string SquidFS::rel(const char *path) {
  if (path && path[0] == '/')
    return std::string(path + 1);
  return path ? std::string(path) : "";
}

std::map<std::string, int> SquidFS::localVersionMap() const {
  return FileManager().getFileVersionMap(FileManager::storageRoot().string());
}

std::set<std::string> SquidFS::listChildren(const std::string &dirRel) const {
  std::set<std::string> children;
  const std::string prefix = dirRel.empty() ? "" : dirRel + "/";

  for (const auto &[filePath, _] : localVersionMap()) {
    if (!prefix.empty() && filePath.substr(0, prefix.size()) != prefix)
      continue; // not under this directory

    // Strip the directory prefix to get the path relative to `dir`.
    std::string rest = filePath.substr(prefix.size());
    auto slash = rest.find('/');
    if (slash == std::string::npos) {
      // Direct file child.
      children.insert(rest);
    } else {
      // The first component is a subdirectory; add it with trailing '/'.
      children.insert(rest.substr(0, slash + 1));
    }
  }
  return children;
}

bool SquidFS::isKnownDir(const std::string &dirRel) const {
  // A virtual directory exists if any known file path starts with "dirRel/".
  const std::string prefix = dirRel + "/";
  for (const auto &[filePath, _] : localVersionMap())
    if (filePath.size() > prefix.size() &&
        filePath.substr(0, prefix.size()) == prefix)
      return true;
  return false;
}

// fetchIntoCache — must be called with cacheMutex_ held via `lock`.
// Drops the lock around the blocking RPC and re-acquires it before returning.
int SquidFS::fetchIntoCache(std::unique_lock<std::mutex> &lock,
                            const std::string &relPath, CacheEntry &entry) {
  lock.unlock();
  std::vector<uint8_t> data;
  Message ack = client_.readFile(relPath, data);
  lock.lock();

  if (!ack.isAck()) {
    std::cerr << "[SquidFS]: readFile(" << relPath
              << ") NACK: " << ack.toString() << "\n";
    return -EIO;
  }
  entry.data = std::move(data);
  entry.version = static_cast<int>(ack.getUint32(FieldID::FILE_VERSION, 0));
  entry.dirty = false;
  return 0;
}

int SquidFS::op_getattr(const char *path, struct stat *st,
                        struct fuse_file_info * /*fi*/) {
  memset(st, 0, sizeof(*st));

  // Root is always a directory.
  if (strcmp(path, "/") == 0) {
    st->st_mode = S_IFDIR | 0755;
    st->st_nlink = 2;
    return 0;
  }

  const std::string r = rel(path);

  // Check in-memory cache first (covers open files and push-warmed entries).
  {
    std::lock_guard<std::mutex> lk(cacheMutex_);
    auto it = cache_.find(r);
    if (it != cache_.end()) {
      st->st_mode = S_IFREG | 0644;
      st->st_nlink = 1;
      st->st_size = static_cast<off_t>(it->second.data.size());
      return 0;
    }
  }

  // Check if it's a virtual intermediate directory.
  if (isKnownDir(r)) {
    st->st_mode = S_IFDIR | 0755;
    st->st_nlink = 2;
    return 0;
  }

  // Check the on-disk version map for a known file.
  int localVersion = client_.fileManager_.getFileVersion(r);
  if (localVersion < 0)
    return -ENOENT;

  st->st_mode = S_IFREG | 0644;
  st->st_nlink = 1;
  std::error_code ec;
  auto diskSize = fs::file_size(FileManager::resolvePath(r), ec);
  st->st_size = ec ? 0 : static_cast<off_t>(diskSize);
  return 0;
}

int SquidFS::op_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t /*offset*/, struct fuse_file_info * /*fi*/,
                        enum fuse_readdir_flags /*flags*/) {
  const std::string dirRel = rel(path); // "" for root, "a" for "/a", etc.

  if (!dirRel.empty() && !isKnownDir(dirRel))
    return -ENOENT;

  filler(buf, ".", nullptr, 0, FUSE_FILL_DIR_PLUS);
  filler(buf, "..", nullptr, 0, FUSE_FILL_DIR_PLUS);

  for (const std::string &child : listChildren(dirRel)) {
    // Subdirectories arrive with a trailing '/'; strip it for the dirent.
    bool isDir = !child.empty() && child.back() == '/';
    std::string name = isDir ? child.substr(0, child.size() - 1) : child;
    filler(buf, name.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
  }
  return 0;
}

// mkdir is virtual: directories in SquidFS are implied by file paths.
int SquidFS::op_mkdir(const char * /*path*/, mode_t /*mode*/) { return 0; }

// rmdir succeeds only when the directory has no children in the version map.
int SquidFS::op_rmdir(const char *path) {
  const std::string dirRel = rel(path);
  if (!isKnownDir(dirRel))
    return -ENOENT;
  if (!listChildren(dirRel).empty())
    return -ENOTEMPTY;
  return 0;
}

int SquidFS::op_open(const char *path, struct fuse_file_info *fi) {
  const std::string r = rel(path);
  if (r.empty())
    return -EISDIR;

  const bool writable = (fi->flags & O_ACCMODE) != O_RDONLY;

  std::unique_lock<std::mutex> lock(cacheMutex_);
  auto &entry = cache_[r];

  if (writable && entry.lockCount == 0) {
    // First writable open: acquire the server lock before fetching.
    lock.unlock();
    Message ack = client_.acquireLock(r);
    lock.lock();
    if (!ack.isAck()) {
      // Clean up the (possibly just-inserted) empty cache entry only if
      // nobody else is using it.
      if (entry.openCount == 0)
        cache_.erase(r);
      std::cerr << "[SquidFS]: acquireLock(" << r
                << ") NACK: " << ack.toString() << "\n";
      return -EACCES;
    }
  }

  // Fetch bytes from the server if the cache entry is empty.
  // This covers: first open of any kind, and re-open after eviction.
  if (entry.openCount == 0) {
    if (int rc = fetchIntoCache(lock, r, entry); rc != 0) {
      if (writable) {
        lock.unlock();
        client_.releaseLock(r);
        lock.lock();
      }
      if (entry.openCount == 0)
        cache_.erase(r);
      return rc;
    }
  }

  if (writable)
    entry.lockCount++;
  entry.openCount++;

  // Store per-fd state so release() knows whether to call releaseLock().
  fi->fh = reinterpret_cast<uint64_t>(new FileHandle{r, writable});
  return 0;
}

int SquidFS::op_create(const char *path, mode_t /*mode*/,
                       struct fuse_file_info *fi) {
  const std::string r = rel(path);
  if (r.empty())
    return -EINVAL;

  // If the file already exists in our cache, delegate to open.
  {
    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (cache_.count(r))
      return op_open(path, fi);
  }

  // Create on the server (always writable).
  Message cAck = client_.createFile(r, {}, 0);
  if (!cAck.isAck()) {
    std::cerr << "[SquidFS]: createFile(" << r << ") NACK: " << cAck.toString()
              << "\n";
    return -EIO;
  }

  Message lAck = client_.acquireLock(r);
  if (!lAck.isAck()) {
    std::cerr << "[SquidFS]: acquireLock after create(" << r
              << ") NACK: " << lAck.toString() << "\n";
    return -EACCES;
  }

  {
    std::lock_guard<std::mutex> lk(cacheMutex_);
    auto &entry = cache_[r];
    entry.data = {};
    entry.version = static_cast<int>(cAck.getUint32(FieldID::FILE_VERSION, 0));
    entry.dirty = false;
    entry.lockCount = 1;
    entry.openCount = 1;
  }

  fi->fh = reinterpret_cast<uint64_t>(new FileHandle{r, true});
  return 0;
}

int SquidFS::op_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info * /*fi*/) {
  const std::string r = rel(path);

  std::unique_lock<std::mutex> lock(cacheMutex_);
  auto it = cache_.find(r);

  if (it == cache_.end() || it->second.data.empty()) {
    // Cache miss: pushed eviction or first access on a read-only open.
    auto &entry = cache_[r];
    if (int rc = fetchIntoCache(lock, r, entry); rc != 0)
      return rc;
    it = cache_.find(r);
  }

  const auto &data = it->second.data;
  if (offset >= static_cast<off_t>(data.size()))
    return 0;

  size_t avail = data.size() - static_cast<size_t>(offset);
  size_t toCopy = std::min(size, avail);
  memcpy(buf, data.data() + offset, toCopy);
  return static_cast<int>(toCopy);
}

int SquidFS::op_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info * /*fi*/) {
  const std::string r = rel(path);

  std::lock_guard<std::mutex> lk(cacheMutex_);
  auto it = cache_.find(r);
  if (it == cache_.end())
    return -EBADF;

  auto &data = it->second.data;
  size_t newEnd = static_cast<size_t>(offset) + size;
  if (newEnd > data.size())
    data.resize(newEnd, 0);

  memcpy(data.data() + offset, buf, size);
  it->second.dirty = true;
  return static_cast<int>(size);
}

int SquidFS::op_release(const char *path, struct fuse_file_info *fi) {
  // Recover and own the FileHandle.
  auto *fh = reinterpret_cast<FileHandle *>(fi->fh);
  fi->fh = 0;
  if (!fh)
    return 0;

  const std::string r = fh->path;
  const bool writable = fh->writable;
  delete fh;

  std::unique_lock<std::mutex> lock(cacheMutex_);
  auto it = cache_.find(r);
  if (it == cache_.end())
    return 0;

  auto &entry = it->second;
  if (writable)
    entry.lockCount = std::max(0, entry.lockCount - 1);
  entry.openCount = std::max(0, entry.openCount - 1);

  // Only flush and release when the last writable handle closes.
  if (writable && entry.lockCount == 0) {
    if (entry.dirty) {
      std::vector<uint8_t> dataCopy = entry.data;
      int ver = entry.version;
      lock.unlock();

      Message ack = client_.updateFile(r, dataCopy, ver);
      if (!ack.isAck())
        std::cerr << "[SquidFS]: updateFile on release(" << r
                  << ") NACK: " << ack.toString() << "\n";

      lock.lock();
      if (ack.isAck()) {
        uint32_t v = ack.getUint32(FieldID::FILE_VERSION, 0);
        if (v)
          cache_[r].version = static_cast<int>(v);
        cache_[r].dirty = false;
      }
      lock.unlock();
    } else {
      lock.unlock();
    }
    client_.releaseLock(r);
    lock.lock();
  }

  // Evict the cache entry only when all handles (read and write) are gone.
  if (entry.openCount == 0)
    cache_.erase(r);

  return 0;
}

int SquidFS::op_unlink(const char *path) {
  const std::string r = rel(path);

  Message ack = client_.deleteFile(r);
  if (!ack.isAck()) {
    std::cerr << "[SquidFS]: deleteFile(" << r << ") NACK: " << ack.toString()
              << "\n";
    return -EIO;
  }

  std::lock_guard<std::mutex> lk(cacheMutex_);
  cache_.erase(r);
  return 0;
}

int SquidFS::op_truncate(const char *path, off_t size,
                         struct fuse_file_info * /*fi*/) {
  const std::string r = rel(path);

  std::lock_guard<std::mutex> lk(cacheMutex_);
  auto it = cache_.find(r);
  if (it == cache_.end())
    return -ENOENT;

  it->second.data.resize(static_cast<size_t>(size), 0);
  it->second.dirty = true;
  return 0;
}

int SquidFS::c_getattr(const char *p, struct stat *st,
                       struct fuse_file_info *fi) {
  return instance_->op_getattr(p, st, fi);
}
int SquidFS::c_readdir(const char *p, void *buf, fuse_fill_dir_t f, off_t off,
                       struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags) {
  return instance_->op_readdir(p, buf, f, off, fi, flags);
}
int SquidFS::c_mkdir(const char *p, mode_t m) {
  return instance_->op_mkdir(p, m);
}
int SquidFS::c_rmdir(const char *p) { return instance_->op_rmdir(p); }
int SquidFS::c_open(const char *p, struct fuse_file_info *fi) {
  return instance_->op_open(p, fi);
}
int SquidFS::c_create(const char *p, mode_t m, struct fuse_file_info *fi) {
  return instance_->op_create(p, m, fi);
}
int SquidFS::c_read(const char *p, char *buf, size_t sz, off_t off,
                    struct fuse_file_info *fi) {
  return instance_->op_read(p, buf, sz, off, fi);
}
int SquidFS::c_write(const char *p, const char *buf, size_t sz, off_t off,
                     struct fuse_file_info *fi) {
  return instance_->op_write(p, buf, sz, off, fi);
}
int SquidFS::c_release(const char *p, struct fuse_file_info *fi) {
  return instance_->op_release(p, fi);
}
int SquidFS::c_unlink(const char *p) { return instance_->op_unlink(p); }
int SquidFS::c_truncate(const char *p, off_t sz, struct fuse_file_info *fi) {
  return instance_->op_truncate(p, sz, fi);
}

const fuse_operations SquidFS::ops_ = [] {
  fuse_operations o{};
  o.getattr = c_getattr;
  o.readdir = c_readdir;
  o.mkdir = c_mkdir;
  o.rmdir = c_rmdir;
  o.open = c_open;
  o.create = c_create;
  o.read = c_read;
  o.write = c_write;
  o.release = c_release;
  o.unlink = c_unlink;
  o.truncate = c_truncate;
  return o;
}();
