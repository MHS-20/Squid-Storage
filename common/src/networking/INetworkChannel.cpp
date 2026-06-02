#pragma once
#include "squidprotocol.hpp"

class INetworkChannel {
public:
  virtual ~INetworkChannel() = default;
  virtual bool sendMessage(const Message &msg) = 0;
  virtual Message receiveMessage() = 0;
  virtual bool isOpen() const = 0;
  virtual void close() = 0;
};
