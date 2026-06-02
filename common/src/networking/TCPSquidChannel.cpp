#pragma once
#include "network_channel.hpp"

class TCPSquidChannel : public INetworkChannel {
private:
  int socketFd_;
  SquidProtocol protocol_;

public:
  TCPSquidChannel(int socketFd) : socketFd_(socketFd), protocol_(socketFd) {}
  ~TCPSquidChannel() { close(); }

  bool sendMessage(const Message &msg) override {
    if (!isOpen())
      return false;
    return protocol_.sendAndSerialize(msg);
  }

  Message receiveMessage() override {
    if (!isOpen())
      return Message(Opcode::ERROR);
    return protocol_.receiveAndParse();
  }

  bool isOpen() const override { return socketFd_ >= 0; }

  void close() override {
    if (socketFd_ >= 0) {
      ::close(socketFd_);
      socketFd_ = -1;
    }
  }
};
