#include "client.hpp"
#include <chrono>
#include <iostream>
#include <thread>

static const char *TEST_FILE = "testfile.txt";

static void checkResponse(const std::string &op, const Message &msg)
{
    if (msg.isAck())
        std::cout << "[main]: " << op << " -> ACK" << std::endl;
    else
        std::cerr << "[main]: " << op << " -> NACK  " << msg.toString() << std::endl;
}

int main(int argc, char **argv)
{
    const char *serverIp = DEFAULT_SERVER_IP;
    int serverPort = DEFAULT_SERVER_PORT;
    std::string processName = "CLIENT_1";

    if (argc > 1) serverIp = argv[1];
    if (argc > 2) serverPort = std::atoi(argv[2]);
    if (argc > 3) processName = argv[3];

    Client client(serverIp, serverPort, processName);

    client.setPushHandler([](const Message &msg) {
        std::cout << "[main]: push received: " << msg.toString() << std::endl;
    });

    client.connect();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<uint8_t> fileData = {'h', 'e', 'l', 'l', 'o'};

    checkResponse("createFile", client.createFile(TEST_FILE, fileData, 1));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    checkResponse("acquireLock", client.acquireLock(TEST_FILE));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<uint8_t> updateData = {'w', 'o', 'r', 'l', 'd'};
    checkResponse("updateFile", client.updateFile(TEST_FILE, updateData, 2));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    checkResponse("releaseLock", client.releaseLock(TEST_FILE));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<uint8_t> readBuf;
    checkResponse("readFile", client.readFile(TEST_FILE, readBuf));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    checkResponse("syncStatus", client.syncStatus());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    checkResponse("heartbeat", client.heartbeat());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    client.disconnect();
    return 0;
}
