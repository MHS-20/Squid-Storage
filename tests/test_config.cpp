#include <gtest/gtest.h>

#include <cstdio>

#include "ClusterConfig.hpp"

class ClusterConfigTest : public ::testing::Test {
protected:
    std::string configPath_;

    void writeConfig(const std::string &content) {
        configPath_ = "/tmp/squid_test_config_XXXXXX";
        std::vector<char> buf(configPath_.begin(), configPath_.end());
        buf.push_back('\0');
        int fd = mkstemp(buf.data());
        ASSERT_GE(fd, 0) << "mkstemp failed";
        configPath_ = buf.data();
        ASSERT_EQ(ftruncate(fd, 0), 0);
        ASSERT_EQ(write(fd, content.data(), content.size()),
                  static_cast<ssize_t>(content.size()));
        close(fd);
    }

    void TearDown() override {
        if (!configPath_.empty())
            std::remove(configPath_.c_str());
    }
};

TEST_F(ClusterConfigTest, ParseBasic) {
    writeConfig(
        "[servers]\n"
        "server1 = 192.168.1.1:9000\n"
        "server2 = 192.168.1.2:9001\n"
        "\n"
        "[replication]\n"
        "heartbeat_interval_ms = 1000\n"
        "heartbeat_timeout_ms  = 5000\n"
    );

    auto cfg = ClusterConfig::fromFile(configPath_);
    ASSERT_EQ(cfg.servers.size(), 2u);
    EXPECT_EQ(cfg.servers[0].name, "server1");
    EXPECT_EQ(cfg.servers[0].ip, "192.168.1.1");
    EXPECT_EQ(cfg.servers[0].port, 9000);
    EXPECT_EQ(cfg.servers[1].name, "server2");
    EXPECT_EQ(cfg.servers[1].ip, "192.168.1.2");
    EXPECT_EQ(cfg.servers[1].port, 9001);

    EXPECT_EQ(cfg.heartbeat_interval_ms, 1000);
    EXPECT_EQ(cfg.heartbeat_timeout_ms, 5000);
}

TEST_F(ClusterConfigTest, DefaultsApplied) {
    writeConfig("[servers]\nserver1 = 10.0.0.1:1234\n");
    auto cfg = ClusterConfig::fromFile(configPath_);
    // Only heartbeat_interval_ms and heartbeat_timeout_ms are set in the
    // example — the rest should be the class defaults
    EXPECT_EQ(cfg.replication_factor, 2);
    EXPECT_EQ(cfg.reconnect_attempts, 3);
    EXPECT_EQ(cfg.reconnect_delay_ms, 500);
    EXPECT_EQ(cfg.promotion_probe_attempts, 3);
    EXPECT_EQ(cfg.promotion_probe_delay_ms, 200);
}

TEST_F(ClusterConfigTest, ReplicationFactorParsed) {
    writeConfig(
        "[servers]\n"
        "s = 1.2.3.4:5\n"
        "[replication]\n"
        "replication_factor = 5\n"
    );
    auto cfg = ClusterConfig::fromFile(configPath_);
    EXPECT_EQ(cfg.replication_factor, 5);
}

TEST_F(ClusterConfigTest, CommentLines) {
    writeConfig(
        "# this is a comment\n"
        "; also a comment\n"
        "[servers]\n"
        "s = 1.2.3.4:5\n"
    );
    auto cfg = ClusterConfig::fromFile(configPath_);
    ASSERT_EQ(cfg.servers.size(), 1u);
    EXPECT_EQ(cfg.servers[0].name, "s");
}

TEST_F(ClusterConfigTest, PriorityOf) {
    writeConfig(
        "[servers]\n"
        "a = 1.1.1.1:1\n"
        "b = 2.2.2.2:2\n"
        "c = 3.3.3.3:3\n"
    );
    auto cfg = ClusterConfig::fromFile(configPath_);
    EXPECT_EQ(cfg.priorityOf("a"), 0);
    EXPECT_EQ(cfg.priorityOf("b"), 1);
    EXPECT_EQ(cfg.priorityOf("c"), 2);
    EXPECT_EQ(cfg.priorityOf("unknown"), -1);
}

TEST_F(ClusterConfigTest, HigherPriorityThan) {
    writeConfig(
        "[servers]\n"
        "a = 1.1.1.1:1\n"
        "b = 2.2.2.2:2\n"
        "c = 3.3.3.3:3\n"
    );
    auto cfg = ClusterConfig::fromFile(configPath_);
    auto higher = cfg.higherPriorityThan("c");
    ASSERT_EQ(higher.size(), 2u);
    EXPECT_EQ(higher[0].name, "a");
    EXPECT_EQ(higher[1].name, "b");

    EXPECT_TRUE(cfg.higherPriorityThan("a").empty());
}

TEST_F(ClusterConfigTest, Find) {
    writeConfig("[servers]\ns = 1.2.3.4:5\n");
    auto cfg = ClusterConfig::fromFile(configPath_);
    auto *e = cfg.find("s");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->ip, "1.2.3.4");

    EXPECT_EQ(cfg.find("nonexistent"), nullptr);
}

TEST_F(ClusterConfigTest, IsPrimary) {
    writeConfig("[servers]\na = 1:1\nb = 2:2\n");
    auto cfg = ClusterConfig::fromFile(configPath_);
    EXPECT_TRUE(cfg.isPrimary("a"));
    EXPECT_FALSE(cfg.isPrimary("b"));
    EXPECT_FALSE(cfg.isPrimary("c"));
}

TEST_F(ClusterConfigTest, Valid) {
    writeConfig("[servers]\na = 1:1\n");
    auto cfg = ClusterConfig::fromFile(configPath_);
    EXPECT_TRUE(cfg.valid());

    ClusterConfig empty;
    EXPECT_FALSE(empty.valid());
}

TEST_F(ClusterConfigTest, StandaloneFactory) {
    auto cfg = ClusterConfig::standalone("10.0.0.1", 9999, "my_server");
    ASSERT_EQ(cfg.servers.size(), 1u);
    EXPECT_EQ(cfg.servers[0].name, "my_server");
    EXPECT_EQ(cfg.servers[0].ip, "10.0.0.1");
    EXPECT_EQ(cfg.servers[0].port, 9999);
    EXPECT_TRUE(cfg.isPrimary("my_server"));
}

TEST_F(ClusterConfigTest, ParseErrorBadPort) {
    writeConfig("[servers]\ns = 1.2.3.4:notanumber\n");
    // Bad port line should be skipped with a warning
    auto cfg = ClusterConfig::fromFile(configPath_);
    EXPECT_TRUE(cfg.servers.empty());
}

TEST_F(ClusterConfigTest, EmptyFile) {
    writeConfig("");
    auto cfg = ClusterConfig::fromFile(configPath_);
    EXPECT_FALSE(cfg.valid());
}

TEST_F(ClusterConfigTest, BadPath) {
    EXPECT_THROW(ClusterConfig::fromFile("/nonexistent/path.conf"), std::runtime_error);
}

TEST_F(ClusterConfigTest, InlineComment) {
    writeConfig(
        "[servers]\n"
        "s = 1.2.3.4:5 ; inline comment\n"
    );
    auto cfg = ClusterConfig::fromFile(configPath_);
    ASSERT_EQ(cfg.servers.size(), 1u);
    EXPECT_EQ(cfg.servers[0].port, 5);
}
