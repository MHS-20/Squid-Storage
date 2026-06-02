#include "client.hpp"
#include <chrono>
#include <iostream>
#include <thread>

static const char *TEST_FILE = "testfile.txt";

static void checkResponse(const std::string &op, const Message &msg) {
  if (msg.isAck())
    std::cout << "[main]: " << op << " -> ACK" << std::endl;
  else
    std::cerr << "[main]: " << op << " -> NACK  " << msg.toString()
              << std::endl;
}

int main(int argc, char **argv) {
  const char *serverIp = MOCK_CLIENT_SERVER_IP;
  int serverPort = MOCK_CLIENT_SERVER_PORT;
  int secondaryPort = MOCK_CLIENT_SECONDARY_PORT;
  std::string processName = "MOCK_CLIENT_1";

  if (argc > 1)
    serverIp = argv[1];
  if (argc > 2)
    serverPort = std::atoi(argv[2]);
  if (argc > 3)
    secondaryPort = std::atoi(argv[3]);
  if (argc > 4)
    processName = argv[4];

  MockClient client(serverIp, serverPort, secondaryPort, processName);

  client.connect();
  client.runPushListener();

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  checkResponse("createFile", client.createFile(TEST_FILE, 1));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  checkResponse("acquireLock", client.acquireLock(TEST_FILE));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  checkResponse("updateFile", client.updateFile(TEST_FILE, 2));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  checkResponse("releaseLock", client.releaseLock(TEST_FILE));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  checkResponse("readFile", client.readFile(TEST_FILE));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  checkResponse("syncStatus", client.syncStatus());
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  checkResponse("heartbeat", client.heartbeat());
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  //checkResponse("deleteFile", client.deleteFile(TEST_FILE));
  // std::this_thread::sleep_for(std::chrono::milliseconds(200));

  client.disconnect();
  return 0;
}
