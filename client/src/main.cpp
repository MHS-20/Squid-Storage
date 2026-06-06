#include "ClusterConfig.hpp"
#include "client.hpp"
#include <chrono>
#include <iostream>
#include <thread>

static const char *TEST_FILE = "testfile.txt";

static void checkResponse(const std::string &op, const Message &msg) {
  if (msg.isAck()) {
    uint32_t v = msg.getUint32(FieldID::FILE_VERSION, UINT32_MAX);
    if (v != UINT32_MAX)
      std::cout << "[main]: " << op << " -> ACK (server version=" << v << ")\n";
    else
      std::cout << "[main]: " << op << " -> ACK\n";
  } else {
    std::cerr << "[main]: " << op << " -> NACK  " << msg.toString() << "\n";
  }
}

int main(int argc, char **argv) {
  std::string serverIp = DEFAULT_SERVER_IP;
  int serverPort = DEFAULT_SERVER_PORT;
  std::string processName = "CLIENT_1";

  const std::string arg1 = argv[1];
  const bool isConfigFile =
      arg1.size() > 5 && arg1.substr(arg1.size() - 5) == ".conf";
  if (argc > 1 && isConfigFile) {
    const std::string configPath = argv[1];
    try {
      ClusterConfig cfg = ClusterConfig::fromFile(configPath);
      if (!cfg.servers.empty()) {
        serverIp = cfg.servers[0].ip;
        serverPort = cfg.servers[0].port;
      }
      if (argc > 2)
        processName = argv[2];
    } catch (const std::exception &ex) {
      std::cerr << "[main]: failed to load config '" << configPath
                << "': " << ex.what() << "\n";
      return 1;
    }
  } else {
    if (argc > 1)
      serverIp = argv[1];
    if (argc > 2)
      serverPort = std::atoi(argv[2]);
    if (argc > 3)
      processName = argv[3];
  }

  Client client(serverIp, serverPort, processName);

  client.setPushHandler(
      [](const Message &msg, const std::vector<uint8_t> &data) {
        std::cout << "[main]: push received: " << msg.toString() << " ("
                  << data.size() << " bytes)\n";
      });

  client.connectToServer();

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  checkResponse("syncStatus (startup)", client.syncStatus());
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::vector<uint8_t> fileData = {'h', 'e', 'l', 'l', 'o'};

  checkResponse("createFile", client.createFile(TEST_FILE, fileData));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  checkResponse("acquireLock", client.acquireLock(TEST_FILE));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  std::vector<uint8_t> updateData = {'w', 'o', 'r', 'l', 'd'};
  checkResponse("updateFile", client.updateFile(TEST_FILE, updateData));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  checkResponse("releaseLock", client.releaseLock(TEST_FILE));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  std::vector<uint8_t> readBuf;
  checkResponse("readFile", client.readFile(TEST_FILE, readBuf));
  std::cout << "[main]: read content: "
            << std::string(readBuf.begin(), readBuf.end()) << "\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  checkResponse("syncStatus (mid-session)", client.syncStatus());
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  checkResponse("heartbeat", client.heartbeat());
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  client.disconnect();
  return 0;
}
