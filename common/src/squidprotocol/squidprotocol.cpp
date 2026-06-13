#include "squidprotocol.hpp"
#include "TCPSquidChannel.hpp"
#include <cerrno>
#include <iostream>

SquidProtocol::SquidProtocol() {}

SquidProtocol::SquidProtocol(FileManager &fileManager, int socket_fd,
                             std::string nodeType, std::string processName)
    : SquidProtocol(fileManager, std::make_shared<TCPSquidChannel>(socket_fd),
                    std::move(nodeType), std::move(processName)) {}

SquidProtocol::SquidProtocol(FileManager &fileManager,
                             std::shared_ptr<INetworkChannel> channel,
                             std::string nodeType, std::string processName)
    : alive_(true), processName_(std::move(processName)),
      nodeType_(std::move(nodeType)), channel_(std::move(channel)),
      fileManager_(&fileManager), formatter_(nodeType_) {
  signal(SIGPIPE, SIG_IGN);
}

SquidProtocol::~SquidProtocol() {}

void SquidProtocol::setChannel(std::shared_ptr<INetworkChannel> channel) {
  channel_ = std::move(channel);
}

void SquidProtocol::setSocket(int fd) {
  channel_ = std::make_shared<TCPSquidChannel>(fd);
}

std::string SquidProtocol::toString() const {
  return "Protocol{" + nodeType_ + ":" + processName_ + "}";
}

bool SquidProtocol::recvExact(uint8_t *buf, size_t n) {
  size_t total = 0;
  while (total < n) {
    if (!channel_)
      return false;
    ssize_t r = channel_->readBytes(buf + total, n - total);
    if (!handleRecvError(r))
      return false;
    total += static_cast<size_t>(r);
  }
  return true;
}

bool SquidProtocol::handleRecvError(ssize_t bytes) {
  if (bytes == 0) {
    std::cout << nodeType_ + ": Connection closed by peer" << std::endl;
    alive_ = false;
    return false;
  }
  if (bytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      std::cout << nodeType_ + ": Socket timeout" << std::endl;
    alive_ = false;
    return false;
  }
  return true;
}

void SquidProtocol::sendFrame(const std::vector<uint8_t> &frame) {
  // Stamp the current outbound sequence number before writing to the wire,
  // then advance the counter. The formatter leaves bytes [5..8] as zero
  // placeholders so we can patch them here without copying the frame.
  std::vector<uint8_t> stamped = frame;
  SquidProtocolFormatter::stampSeq(stamped, nextSendSeq_++);

  size_t total = 0;
  while (total < stamped.size()) {
    if (!channel_) {
      alive_ = false;
      return;
    }

    ssize_t sent =
        channel_->writeBytes(stamped.data() + total, stamped.size() - total);
    if (sent <= 0) {
      alive_ = false;
      return;
    }
    total += static_cast<size_t>(sent);
  }
}

std::vector<uint8_t> SquidProtocol::receiveFrame() {
  // Header layout (13 bytes):
  //   [magic:2][opcode:1][flags:1][nfields:1][seq:4][payloadLen:4]
  uint8_t header[FRAME_HEADER_SIZE];
  if (!recvExact(header, FRAME_HEADER_SIZE))
    return {};

  uint16_t magic = (uint16_t(header[0]) << 8) | header[1];
  if (magic != SQUID_MAGIC) {
    std::cerr << nodeType_ + ": Bad frame magic" << std::endl;
    alive_ = false;
    return {};
  }

  uint8_t numFields = header[4];
  // bytes [5..8] = seq  (read by receiveAndParse via parseMessage)
  uint32_t payloadLen = (uint32_t(header[9]) << 24) |
                        (uint32_t(header[10]) << 16) |
                        (uint32_t(header[11]) << 8) | uint32_t(header[12]);

  std::vector<uint8_t> frame(header, header + FRAME_HEADER_SIZE);

  for (uint8_t i = 0; i < numFields; ++i) {
    uint8_t fhdr[3];
    if (!recvExact(fhdr, 3))
      return {};
    uint16_t vlen = (uint16_t(fhdr[1]) << 8) | fhdr[2];
    frame.push_back(fhdr[0]);
    frame.push_back(fhdr[1]);
    frame.push_back(fhdr[2]);
    if (vlen > 0) {
      size_t before = frame.size();
      frame.resize(before + vlen);
      if (!recvExact(frame.data() + before, vlen))
        return {};
    }
  }

  if (payloadLen > 0) {
    size_t before = frame.size();
    frame.resize(before + payloadLen);
    if (!recvExact(frame.data() + before, payloadLen))
      return {};
  }

  return frame;
}

bool SquidProtocol::popNextBuffered(std::vector<uint8_t> &frame) {
  auto it = reorderBuffer_.find(expectedRecvSeq_);
  if (it == reorderBuffer_.end())
    return false;
  frame = std::move(it->second);
  reorderBuffer_.erase(it);
  return true;
}

// receiveAndParse delivers messages strictly in sequence-number order.
//
// If a frame arrives with seq == expectedRecvSeq_ it is returned immediately.
// If it arrives with seq > expectedRecvSeq_ (gap) it is stored in the reorder
// buffer and we read the next frame from the wire, repeating until the
// expected seq is available either from the wire or from the buffer.
// Frames with seq < expectedRecvSeq_ are stale duplicates and are discarded.
//
// This is fully transparent to all callers: server, client, and datanode code
// never see out-of-order messages and need not be changed.
Message SquidProtocol::receiveAndParse() {
  while (true) {
    // First check if the next expected frame is already buffered.
    std::vector<uint8_t> raw;
    if (!popNextBuffered(raw)) {
      raw = receiveFrame();
      if (raw.empty())
        return formatter_.makeNack();
    }

    Message msg;
    try {
      msg = formatter_.parseMessage(raw);
    } catch (const std::exception &e) {
      std::cerr << nodeType_ + ": parse error: " << e.what() << std::endl;
      return formatter_.makeNack();
    }

    const uint32_t seq = msg.seq;

    if (seq == expectedRecvSeq_) {
      // In-order delivery — the common fast path.
      lastDeliveredSeq_ = seq;
      ++expectedRecvSeq_;
      return msg;
    }

    if (seq > expectedRecvSeq_) {
      // Out-of-order: buffer it and keep reading.
      reorderBuffer_.emplace(seq, std::move(raw));
      continue;
    }

    // seq < expectedRecvSeq_: stale duplicate, discard and try again.
    std::cerr << nodeType_ + ": discarding duplicate frame seq=" << seq
              << std::endl;
  }
}

bool SquidProtocol::waitForAck(Message &ackMsg, const std::string &operation) {
  ackMsg = receiveAndParse();
  if (!ackMsg.isAck()) {
    std::cerr << nodeType_ + ": " + operation + " was not acknowledged"
              << std::endl;
    return false;
  }
  return true;
}

Message SquidProtocol::waitForTransferResult(const std::string &operation) {
  Message result = receiveAndParse();
  if (!result.isAck())
    std::cerr << nodeType_ + ": " + operation + " failed: " + result.toString()
              << std::endl;
  return result;
}

bool SquidProtocol::sendFileAfterAck(const std::string &filePath,
                                     const Message &ackMsg) {
  if (!ackMsg.isAck()) {
    std::cerr << nodeType_ + ": Error while transferring file: " + filePath
              << std::endl;
    return false;
  }

  if (!channel_)
    return false;

  return fileTransfer_.sendFile(*channel_, processName_, filePath);
}

bool SquidProtocol::sendFileAfterAck(const std::vector<uint8_t> &fileData,
                                     const Message &ackMsg) {
  if (!ackMsg.isAck()) {
    std::cerr << nodeType_ + ": Error while transferring file" << std::endl;
    return false;
  }

  if (!channel_)
    return false;

  return fileTransfer_.sendFile(*channel_, processName_, fileData);
}

bool SquidProtocol::receiveFileAfterAck(std::vector<uint8_t> &fileData,
                                        const Message &ackMsg) {
  if (!ackMsg.isAck()) {
    std::cerr << nodeType_ + ": Error while transferring file" << std::endl;
    return false;
  }

  if (!channel_)
    return false;

  return fileTransfer_.receiveFile(*channel_, processName_, fileData);
}

Message SquidProtocol::identify() {
  sendFrame(formatter_.identifyFormat());
  return receiveAndParse();
}

Message SquidProtocol::connectServer() {
  std::cout << nodeType_ + ": sending connect server request" << std::endl;
  sendFrame(formatter_.connectServerFormat());
  Message r = receiveAndParse();
  std::cout << nodeType_ + ": received connect server response" << std::endl;
  return r;
}

// closeConn sends the CLOSE frame and marks the connection dead locally.
// It does NOT wait for a response: the server ACKs and closes immediately,
// and waiting would race with the server tearing down its side first.
Message SquidProtocol::closeConn() {
  sendFrame(formatter_.closeFormat());
  alive_ = false;
  if (channel_)
    channel_->close();
  return formatter_.makeAck();
}

Message SquidProtocol::syncStatus() {
  std::cout << nodeType_ + ": sending sync status request" << std::endl;
  sendFrame(formatter_.syncStatusFormat());
  Message response = receiveAndParse();
  std::cout << nodeType_ + ": received sync status response" << std::endl;
  return response;
}

Message SquidProtocol::createFile(const std::string &filePath) {
  std::cout << nodeType_ + ": sending create file request: " + filePath
            << std::endl;
  sendFrame(formatter_.createFileFormat(filePath));
  Message ack;
  if (!waitForAck(ack, "create file"))
    return ack;

  std::cout << nodeType_ + ": received create file ack" << std::endl;
  if (!sendFileAfterAck(filePath, ack))
    return formatter_.makeNack();

  return waitForTransferResult("create file");
}

Message SquidProtocol::createFile(const std::string &filePath, int version) {
  std::cout << nodeType_ + ": sending create file request: " + filePath
            << std::endl;
  sendFrame(formatter_.createFileFormat(filePath, version));
  Message ack;
  if (!waitForAck(ack, "create file"))
    return ack;

  std::cout << nodeType_ + ": received create file ack" << std::endl;
  if (!sendFileAfterAck(filePath, ack))
    return formatter_.makeNack();

  return waitForTransferResult("create file");
}

Message SquidProtocol::createFile(const std::string &filePath, int version,
                                  const std::vector<uint8_t> &fileData) {
  std::cout << nodeType_ + ": sending create file request: " + filePath
            << std::endl;
  sendFrame(formatter_.createFileFormat(filePath, version));
  Message ack;
  if (!waitForAck(ack, "create file"))
    return ack;

  std::cout << nodeType_ + ": received create file ack" << std::endl;
  if (!sendFileAfterAck(fileData, ack))
    return formatter_.makeNack();

  return waitForTransferResult("create file");
}

Message SquidProtocol::readFile(const std::string &filePath,
                                std::vector<uint8_t> &fileData) {
  std::cout << nodeType_ + ": sending read file request" << std::endl;
  sendFrame(formatter_.readFileFormat(filePath));
  Message ack;
  if (!waitForAck(ack, "read file"))
    return ack;

  if (!receiveFileAfterAck(fileData, ack))
    return formatter_.makeNack();

  return waitForTransferResult("read file");
}

Message SquidProtocol::updateFile(const std::string &filePath) {
  sendFrame(formatter_.updateFileFormat(filePath));
  Message ack;
  if (!waitForAck(ack, "update file"))
    return ack;

  if (!sendFileAfterAck(filePath, ack))
    return formatter_.makeNack();

  return waitForTransferResult("update file");
}

Message SquidProtocol::updateFile(const std::string &filePath, int version) {
  sendFrame(formatter_.updateFileFormat(filePath, version));
  Message ack;
  if (!waitForAck(ack, "update file"))
    return ack;

  if (!sendFileAfterAck(filePath, ack))
    return formatter_.makeNack();

  return waitForTransferResult("update file");
}

Message SquidProtocol::updateFile(const std::string &filePath, int version,
                                  const std::vector<uint8_t> &fileData) {
  sendFrame(formatter_.updateFileFormat(filePath, version));
  Message ack;
  if (!waitForAck(ack, "update file"))
    return ack;

  if (!sendFileAfterAck(fileData, ack))
    return formatter_.makeNack();

  return waitForTransferResult("update file");
}

bool SquidProtocol::receiveFileData(std::vector<uint8_t> &fileData) {
  if (!channel_)
    return false;

  return fileTransfer_.receiveFile(*channel_, processName_, fileData);
}

bool SquidProtocol::sendFileData(const std::vector<uint8_t> &fileData) {
  if (!channel_)
    return false;

  return fileTransfer_.sendFile(*channel_, processName_, fileData);
}

// Push helpers use dedicated PUSH_* opcodes so the receiver can distinguish
// them from request/response frames without a correlation ID.
void SquidProtocol::pushCreateFile(const std::string &filePath, int version,
                                   const std::vector<uint8_t> &fileData) {
  sendFrame(formatter_.pushCreateFileFormat(filePath, version));
  sendFileData(fileData);
}

void SquidProtocol::pushUpdateFile(const std::string &filePath, int version,
                                   const std::vector<uint8_t> &fileData) {
  sendFrame(formatter_.pushUpdateFileFormat(filePath, version));
  sendFileData(fileData);
}

void SquidProtocol::pushDeleteFile(const std::string &filePath) {
  sendFrame(formatter_.pushDeleteFileFormat(filePath));
}

// ── Standby-replication outbound
// ──────────────────────────────────────────────

void SquidProtocol::sendStateSnap(
    const std::map<std::string, int> &versionMap,
    const std::map<std::string, std::set<std::string>> &repMap,
    uint32_t epoch) {
  sendFrame(formatter_.stateSnapFormat(versionMap, repMap, epoch));
}

void SquidProtocol::sendStateDelta(uint8_t op, const std::string &filePath,
                                   int version,
                                   const std::vector<std::string> &datanodes,
                                   uint32_t epoch) {
  sendFrame(
      formatter_.stateDeltaFormat(op, filePath, version, datanodes, epoch));
}

void SquidProtocol::sendLeaderHb(uint32_t epoch) {
  sendFrame(formatter_.leaderHbFormat(epoch));
}

void SquidProtocol::sendNackStaleEpoch(uint32_t myEpoch) {
  sendFrame(formatter_.nackStaleEpochFormat(myEpoch));
}

Message SquidProtocol::deleteFile(const std::string &filePath) {
  sendFrame(formatter_.deleteFileFormat(filePath));
  return receiveAndParse();
}

Message SquidProtocol::acquireLock(const std::string &filePath) {
  std::cout << nodeType_ + ": sending acquire lock request for " << filePath
            << std::endl;
  sendFrame(formatter_.acquireLockFormat(filePath));
  return receiveAndParse();
}

Message SquidProtocol::releaseLock(const std::string &filePath) {
  sendFrame(formatter_.releaseLockFormat(filePath));
  return receiveAndParse();
}

Message SquidProtocol::heartbeat() {
  sendFrame(formatter_.heartbeatFormat());
  return receiveAndParse();
}

void SquidProtocol::response(bool isAck) {
  std::cout << nodeType_ + ": Sending response: " << isAck << std::endl;
  sendFrame(formatter_.responseAck(isAck));
}

void SquidProtocol::response(bool isAck, int version) {
  std::cout << nodeType_ + ": Sending response: " << isAck
            << " version=" << version << std::endl;
  sendFrame(formatter_.responseAckWithVersion(isAck, version));
}

void SquidProtocol::response(int port) {
  sendFrame(formatter_.responsePort(port));
}

void SquidProtocol::response(const std::string &ack) {
  std::cout << nodeType_ + ": Sending response: " << ack << std::endl;
  sendFrame(formatter_.responseFormat(ack));
}

void SquidProtocol::response(const std::string &nodeType,
                             const std::string &processName) {
  sendFrame(formatter_.responseIdentity(nodeType, processName));
}

void SquidProtocol::response(const std::map<std::string, int> &fileVersionMap) {
  sendFrame(formatter_.responseFileVersionMap(fileVersionMap));
}

void SquidProtocol::response(
    const std::map<std::string, long long> &fileTimeMap) {
  sendFrame(formatter_.responseFormat(fileTimeMap));
}

void SquidProtocol::response(
    const std::map<std::string, fs::file_time_type> &filesLastWrite) {
  sendFrame(formatter_.responseFormat(filesLastWrite));
}

// requestDispatcher handles all opcodes that arrive as requests (i.e. the peer
// is initiating an operation and expects a response from us).
// PUSH_* opcodes must never reach this dispatcher — they are handled separately
// by the client's handlePush() and have no response protocol.
void SquidProtocol::requestDispatcher(const Message &message) {
  std::string path = message.getString(FieldID::FILE_PATH);
  int version = static_cast<int>(message.getUint32(FieldID::FILE_VERSION, 0));

  switch (message.opcode) {
  case Opcode::CREATE_FILE:
    std::cout << nodeType_ + ": received create file request\n";
    response(true);
    std::cout << nodeType_ + ": Receiving file" << std::endl;
    if (channel_ && fileTransfer_.receiveFile(*channel_, processName_, path)) {
      (*fileManager_).setFileVersion(path, version);
      response(true);
    } else {
      response(false);
    }
    break;

  case Opcode::READ_FILE:
    std::cout << nodeType_ + ": received read file request\n";
    response(true);
    if (channel_ && fileTransfer_.sendFile(*channel_, processName_, path))
      response(true, (*fileManager_).getFileVersion(path));
    else
      response(false);
    break;

  case Opcode::UPDATE_FILE:
    std::cout << nodeType_ + ": received update file request\n";
    response(true);
    if (channel_ && fileTransfer_.receiveFile(*channel_, processName_, path)) {
      (*fileManager_).setFileVersion(path, version);
      response(true, version);
    } else {
      response(false);
    }
    break;

  case Opcode::DELETE_FILE:
    std::cout << nodeType_ + ": received delete file request\n";
    (*fileManager_).deleteFileAndVersion(path);
    response(true);
    break;

  case Opcode::ACQUIRE_LOCK:
    // Datanodes do not manage locks; the server handles this.
    // If a datanode ever receives ACQUIRE_LOCK it simply ACKs to avoid
    // leaving the sender blocked.
    std::cerr << nodeType_ + ": unexpected ACQUIRE_LOCK on requestDispatcher\n";
    response(false);
    break;

  case Opcode::RELEASE_LOCK:
    response(true);
    break;

  case Opcode::HEARTBEAT:
    response(true);
    break;

  case Opcode::SYNC_STATUS:
    std::cout << nodeType_ + ": received sync status request\n";
    this->response(
        (*fileManager_).getFileVersionMap(FileManager::storageRoot().string()));
    break;

  case Opcode::IDENTIFY:
    this->response(nodeType_, processName_);
    break;

  case Opcode::CONNECT_SERVER:
    // A datanode or peer sending CONNECT_SERVER is handled at the session
    // layer (handshake). If it arrives here something is wrong; NACK.
    std::cerr << nodeType_ +
                     ": unexpected CONNECT_SERVER on requestDispatcher\n";
    response(false);
    break;

  case Opcode::CLOSE:
    response(true);
    alive_ = false;
    if (channel_)
      channel_->close();
    std::cout << nodeType_ + ": Connection closed" << std::endl;
    break;

  case Opcode::RESPONSE:
    // A RESPONSE arriving where a request was expected means the stream is
    // out of sync. Mark dead so the session tears down cleanly.
    std::cerr
        << nodeType_ +
               ": unexpected RESPONSE frame — connection out of sync, closing"
        << std::endl;
    alive_ = false;
    break;

  // PUSH_* opcodes: must not be dispatched as requests.
  case Opcode::PUSH_CREATE_FILE:
  case Opcode::PUSH_UPDATE_FILE:
  case Opcode::PUSH_DELETE_FILE:
    std::cerr << nodeType_ + ": PUSH opcode reached requestDispatcher — "
                             "misrouted, ignoring\n";
    break;

  default:
    std::cerr << nodeType_ + ": Unknown request: " + message.toString()
              << std::endl;
    break;
  }
}

// responseDispatcher is used when the local node is purely reactive (e.g. a
// datanode whose read loop only receives requests from the server).
// It no longer falls back to requestDispatcher for non-RESPONSE opcodes —
// if anything other than RESPONSE or a known request arrives, it is logged.
void SquidProtocol::responseDispatcher(const Message &message) {
  // PUSH_* opcodes should never reach a datanode; log and ignore.
  if (message.opcode == Opcode::PUSH_CREATE_FILE ||
      message.opcode == Opcode::PUSH_UPDATE_FILE ||
      message.opcode == Opcode::PUSH_DELETE_FILE) {
    std::cerr << nodeType_ +
                     ": PUSH opcode reached responseDispatcher — unexpected\n";
    return;
  }

  // For all other opcodes (requests from the server), delegate to the
  // full request handler which will send the appropriate response.
  requestDispatcher(message);
}
