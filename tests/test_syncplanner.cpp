#include <gtest/gtest.h>
#include "syncplanner.hpp"

TEST(SyncPlanner, LocalOnly_UploadToRemote) {
    std::map<std::string, int> local  = {{"f1", 2}};
    std::map<std::string, int> remote = {{"f1", 1}};

    auto ops = planSync(local, remote);
    ASSERT_EQ(ops.size(), 1u);
    EXPECT_EQ(ops[0].action, SyncAction::UPLOAD);
    EXPECT_EQ(ops[0].filePath, "f1");
    EXPECT_EQ(ops[0].version, 2);
}

TEST(SyncPlanner, RemoteOnly_DownloadFromRemote) {
    std::map<std::string, int> local  = {{"f1", 1}};
    std::map<std::string, int> remote = {{"f1", 2}};

    auto ops = planSync(local, remote);
    ASSERT_EQ(ops.size(), 1u);
    EXPECT_EQ(ops[0].action, SyncAction::DOWNLOAD);
    EXPECT_EQ(ops[0].filePath, "f1");
    EXPECT_EQ(ops[0].version, 2);
}

TEST(SyncPlanner, SameVersion_NoOp) {
    std::map<std::string, int> local  = {{"f1", 1}};
    std::map<std::string, int> remote = {{"f1", 1}};

    auto ops = planSync(local, remote);
    EXPECT_TRUE(ops.empty());
}

TEST(SyncPlanner, FileOnlyOnLocal_CreateRemote) {
    std::map<std::string, int> local  = {{"f1", 1}};
    std::map<std::string, int> remote = {};

    auto ops = planSync(local, remote);
    ASSERT_EQ(ops.size(), 1u);
    EXPECT_EQ(ops[0].action, SyncAction::CREATE_REMOTE);
    EXPECT_EQ(ops[0].filePath, "f1");
    EXPECT_EQ(ops[0].version, 1);
}

TEST(SyncPlanner, FileOnlyOnRemote_Download) {
    std::map<std::string, int> local  = {};
    std::map<std::string, int> remote = {{"f1", 1}};

    auto ops = planSync(local, remote);
    ASSERT_EQ(ops.size(), 1u);
    EXPECT_EQ(ops[0].action, SyncAction::DOWNLOAD);
    EXPECT_EQ(ops[0].filePath, "f1");
    EXPECT_EQ(ops[0].version, 1);
}

TEST(SyncPlanner, Mixed) {
    std::map<std::string, int> local  = {{"f1", 2}, {"f2", 1}, {"f3", 1}};
    std::map<std::string, int> remote = {{"f1", 1}, {"f2", 2}, {"f4", 5}};

    auto ops = planSync(local, remote);

    // f1: local newer -> UPLOAD
    // f2: remote newer -> DOWNLOAD
    // f3: local only   -> CREATE_REMOTE
    // f4: remote only  -> DOWNLOAD (from remote)
    ASSERT_EQ(ops.size(), 4u);

    // Order: local files iterated first, then remote remnants
    EXPECT_EQ(ops[0].action, SyncAction::UPLOAD);
    EXPECT_EQ(ops[0].filePath, "f1");

    EXPECT_EQ(ops[1].action, SyncAction::DOWNLOAD);
    EXPECT_EQ(ops[1].filePath, "f2");

    EXPECT_EQ(ops[2].action, SyncAction::CREATE_REMOTE);
    EXPECT_EQ(ops[2].filePath, "f3");

    EXPECT_EQ(ops[3].action, SyncAction::DOWNLOAD);
    EXPECT_EQ(ops[3].filePath, "f4");
}
