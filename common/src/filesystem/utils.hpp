#pragma once
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

inline fs::path getDefaultStoragePath() {
  const char *homeEnv = std::getenv("HOME");
  fs::path baseDir = homeEnv ? fs::path(homeEnv) : fs::path("/tmp");
  fs::path squidStorage = baseDir / "SquidStorage";
  fs::create_directories(squidStorage);
  return squidStorage;
}
