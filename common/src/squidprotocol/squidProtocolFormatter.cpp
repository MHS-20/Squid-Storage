#include "squidProtocolFormatter.hpp"
#include <arpa/inet.h>
#include <sstream>

static void pushU16(std::vector<uint8_t> &buf, uint16_t v) {
  buf.push_back(static_cast<uint8_t>(v >> 8));
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void pushU32(std::vector<uint8_t> &buf, uint32_t v) {
  buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void pushU64(std::vector<uint8_t> &buf, uint64_t v) {
  for (int i = 7; i >= 0; --i)
    buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

static uint16_t readU16(const uint8_t *p) {
  return (uint16_t(p[0]) << 8) | p[1];
}
static uint32_t readU32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
static uint64_t readU64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v = (v << 8) | p[i];
  return v;
}

std::string opcodeToString(Opcode op) {
  switch (op) {
  case Opcode::CONNECT_SERVER:
    return "CONNECT_SERVER";
  case Opcode::IDENTIFY:
    return "IDENTIFY";
  case Opcode::CLOSE:
    return "CLOSE";
  case Opcode::HEARTBEAT:
    return "HEARTBEAT";
  case Opcode::SYNC_STATUS:
    return "SYNC_STATUS";
  case Opcode::CREATE_FILE:
    return "CREATE_FILE";
  case Opcode::READ_FILE:
    return "READ_FILE";
  case Opcode::UPDATE_FILE:
    return "UPDATE_FILE";
  case Opcode::DELETE_FILE:
    return "DELETE_FILE";
  case Opcode::ACQUIRE_LOCK:
    return "ACQUIRE_LOCK";
  case Opcode::RELEASE_LOCK:
    return "RELEASE_LOCK";
  case Opcode::PUSH_CREATE_FILE:
    return "PUSH_CREATE_FILE";
  case Opcode::PUSH_UPDATE_FILE:
    return "PUSH_UPDATE_FILE";
  case Opcode::PUSH_DELETE_FILE:
    return "PUSH_DELETE_FILE";
  case Opcode::STATE_SNAP:
    return "STATE_SNAP";
  case Opcode::STATE_DELTA:
    return "STATE_DELTA";
  case Opcode::LEADER_HB:
    return "LEADER_HB";
  case Opcode::NACK_STALE_EPOCH:
    return "NACK_STALE_EPOCH";
  case Opcode::RESPONSE:
    return "RESPONSE";
  default:
    return "UNKNOWN";
  }
}

BinaryField SquidProtocolFormatter::fieldString(FieldID id,
                                                const std::string &s) {
  BinaryField f;
  f.id = id;
  f.value.assign(s.begin(), s.end());
  return f;
}

BinaryField SquidProtocolFormatter::fieldUint32(FieldID id, uint32_t v) {
  BinaryField f;
  f.id = id;
  f.value.resize(4);
  f.value[0] = (v >> 24) & 0xFF;
  f.value[1] = (v >> 16) & 0xFF;
  f.value[2] = (v >> 8) & 0xFF;
  f.value[3] = v & 0xFF;
  return f;
}

BinaryField SquidProtocolFormatter::fieldUint64(FieldID id, uint64_t v) {
  BinaryField f;
  f.id = id;
  f.value.resize(8);
  for (int i = 7; i >= 0; --i) {
    f.value[i] = v & 0xFF;
    v >>= 8;
  }
  return f;
}

BinaryField SquidProtocolFormatter::fieldBool(FieldID id, bool v) {
  BinaryField f;
  f.id = id;
  f.value = {static_cast<uint8_t>(v ? 0x01 : 0x00)};
  return f;
}

// Header layout:
//   [magic:2][opcode:1][flags:1][nfields:1][seq:4][payloadLen:4]  = 13 bytes
//
// seq is written as 0 here; SquidProtocol::sendFrame stamps the real value
// before the frame hits the wire, keeping the formatter stateless.
std::vector<uint8_t>
SquidProtocolFormatter::buildFrame(Opcode opcode, uint8_t flags,
                                   const std::vector<BinaryField> &fields,
                                   uint32_t payloadLen) const {
  if (fields.size() > 255)
    throw std::runtime_error("Too many fields for a single frame");

  std::vector<uint8_t> frame;
  frame.reserve(FRAME_HEADER_SIZE + fields.size() * 4 + 64);

  pushU16(frame, SQUID_MAGIC);                          // [0..1]  magic
  frame.push_back(static_cast<uint8_t>(opcode));        // [2]     opcode
  frame.push_back(flags);                               // [3]     flags
  frame.push_back(static_cast<uint8_t>(fields.size())); // [4]     nfields
  pushU32(frame, 0);          // [5..8]  seq  (placeholder)
  pushU32(frame, payloadLen); // [9..12] payloadLen

  for (const auto &f : fields) {
    if (f.value.size() > 65535)
      throw std::runtime_error("Field value exceeds maximum length");
    frame.push_back(static_cast<uint8_t>(f.id));
    pushU16(frame, static_cast<uint16_t>(f.value.size()));
    frame.insert(frame.end(), f.value.begin(), f.value.end());
  }

  return frame;
}

// Patch bytes [5..8] of an already-built frame with the real seq number.
// Must be called before sendFrame writes the frame to the wire.
/*static*/ void SquidProtocolFormatter::stampSeq(std::vector<uint8_t> &frame,
                                                 uint32_t seq) {
  if (frame.size() < FRAME_HEADER_SIZE)
    return;
  frame[5] = static_cast<uint8_t>((seq >> 24) & 0xFF);
  frame[6] = static_cast<uint8_t>((seq >> 16) & 0xFF);
  frame[7] = static_cast<uint8_t>((seq >> 8) & 0xFF);
  frame[8] = static_cast<uint8_t>(seq & 0xFF);
}

std::vector<uint8_t> SquidProtocolFormatter::identifyFormat() const {
  return buildFrame(Opcode::IDENTIFY, 0, {});
}

std::vector<uint8_t> SquidProtocolFormatter::closeFormat() const {
  return buildFrame(Opcode::CLOSE, 0, {});
}

std::vector<uint8_t> SquidProtocolFormatter::connectServerFormat() const {
  return buildFrame(Opcode::CONNECT_SERVER, 0, {});
}

std::vector<uint8_t> SquidProtocolFormatter::heartbeatFormat() const {
  return buildFrame(Opcode::HEARTBEAT, 0, {});
}

std::vector<uint8_t> SquidProtocolFormatter::syncStatusFormat() const {
  return buildFrame(Opcode::SYNC_STATUS, 0, {});
}

std::vector<uint8_t>
SquidProtocolFormatter::createFileFormat(const std::string &filePath) const {
  return buildFrame(Opcode::CREATE_FILE, 0,
                    {fieldString(FieldID::FILE_PATH, filePath)});
}

std::vector<uint8_t>
SquidProtocolFormatter::createFileFormat(const std::string &filePath,
                                         int version) const {
  return buildFrame(
      Opcode::CREATE_FILE, 0,
      {fieldString(FieldID::FILE_PATH, filePath),
       fieldUint32(FieldID::FILE_VERSION, static_cast<uint32_t>(version))});
}

std::vector<uint8_t>
SquidProtocolFormatter::readFileFormat(const std::string &filePath) const {
  return buildFrame(Opcode::READ_FILE, 0,
                    {fieldString(FieldID::FILE_PATH, filePath)});
}

std::vector<uint8_t>
SquidProtocolFormatter::updateFileFormat(const std::string &filePath) const {
  return buildFrame(Opcode::UPDATE_FILE, 0,
                    {fieldString(FieldID::FILE_PATH, filePath)});
}

std::vector<uint8_t>
SquidProtocolFormatter::updateFileFormat(const std::string &filePath,
                                         int version) const {
  return buildFrame(
      Opcode::UPDATE_FILE, 0,
      {fieldString(FieldID::FILE_PATH, filePath),
       fieldUint32(FieldID::FILE_VERSION, static_cast<uint32_t>(version))});
}

std::vector<uint8_t>
SquidProtocolFormatter::deleteFileFormat(const std::string &filePath) const {
  return buildFrame(Opcode::DELETE_FILE, 0,
                    {fieldString(FieldID::FILE_PATH, filePath)});
}

std::vector<uint8_t>
SquidProtocolFormatter::acquireLockFormat(const std::string &filePath) const {
  return buildFrame(Opcode::ACQUIRE_LOCK, 0,
                    {fieldString(FieldID::FILE_PATH, filePath)});
}

std::vector<uint8_t>
SquidProtocolFormatter::releaseLockFormat(const std::string &filePath) const {
  return buildFrame(Opcode::RELEASE_LOCK, 0,
                    {fieldString(FieldID::FILE_PATH, filePath)});
}

std::vector<uint8_t>
SquidProtocolFormatter::pushCreateFileFormat(const std::string &filePath,
                                             int version) const {
  return buildFrame(
      Opcode::PUSH_CREATE_FILE, 0,
      {fieldString(FieldID::FILE_PATH, filePath),
       fieldUint32(FieldID::FILE_VERSION, static_cast<uint32_t>(version))});
}

std::vector<uint8_t>
SquidProtocolFormatter::pushUpdateFileFormat(const std::string &filePath,
                                             int version) const {
  return buildFrame(
      Opcode::PUSH_UPDATE_FILE, 0,
      {fieldString(FieldID::FILE_PATH, filePath),
       fieldUint32(FieldID::FILE_VERSION, static_cast<uint32_t>(version))});
}

std::vector<uint8_t> SquidProtocolFormatter::pushDeleteFileFormat(
    const std::string &filePath) const {
  return buildFrame(Opcode::PUSH_DELETE_FILE, 0,
                    {fieldString(FieldID::FILE_PATH, filePath)});
}

// ── Standby-replication frames
// ────────────────────────────────────────────────

std::vector<uint8_t> SquidProtocolFormatter::stateSnapFormat(
    const std::map<std::string, int> &versionMap,
    const std::map<std::string, std::set<std::string>> &repMap,
    uint32_t epoch) const {
  std::vector<BinaryField> fields;
  fields.push_back(fieldUint32(FieldID::EPOCH, epoch));

  // Each SNAP_ENTRY is a string: "filePath version dn1,dn2,..."
  for (const auto &[filePath, ver] : versionMap) {
    std::string entry = filePath + " " + std::to_string(ver);
    auto rit = repMap.find(filePath);
    if (rit != repMap.end() && !rit->second.empty()) {
      entry += " ";
      bool first = true;
      for (const auto &dn : rit->second) {
        if (!first)
          entry += ",";
        entry += dn;
        first = false;
      }
    }
    fields.push_back(fieldString(FieldID::SNAP_ENTRY, entry));
  }
  return buildFrame(Opcode::STATE_SNAP, 0, fields);
}

std::vector<uint8_t> SquidProtocolFormatter::stateDeltaFormat(
    uint8_t op, const std::string &filePath, int version,
    const std::vector<std::string> &datanodes, uint32_t epoch) const {
  std::vector<BinaryField> fields;
  fields.push_back(fieldUint32(FieldID::EPOCH, epoch));

  BinaryField opField;
  opField.id = FieldID::DELTA_OP;
  opField.value = {op};
  fields.push_back(opField);

  fields.push_back(fieldString(FieldID::FILE_PATH, filePath));
  fields.push_back(
      fieldUint32(FieldID::FILE_VERSION, static_cast<uint32_t>(version)));

  for (const auto &dn : datanodes)
    fields.push_back(fieldString(FieldID::DATANODE_NAME, dn));

  return buildFrame(Opcode::STATE_DELTA, 0, fields);
}

std::vector<uint8_t>
SquidProtocolFormatter::leaderHbFormat(uint32_t epoch) const {
  return buildFrame(Opcode::LEADER_HB, 0, {fieldUint32(FieldID::EPOCH, epoch)});
}

std::vector<uint8_t>
SquidProtocolFormatter::nackStaleEpochFormat(uint32_t myEpoch) const {
  return buildFrame(Opcode::NACK_STALE_EPOCH, 0,
                    {fieldUint32(FieldID::EPOCH, myEpoch)});
}

// ── parseSnapEntry
// ─────────────────────────────────────────────────────────────

bool SquidProtocolFormatter::parseSnapEntry(const std::string &entry,
                                            std::string &filePath, int &version,
                                            std::set<std::string> &datanodes) {
  // Format: "filePath version dn1,dn2,..."
  // The datanodes part is optional (absent for files with no live holders).
  std::istringstream ss(entry);
  if (!(ss >> filePath >> version))
    return false;

  std::string dnList;
  if (ss >> dnList && !dnList.empty()) {
    std::istringstream dns(dnList);
    std::string dn;
    while (std::getline(dns, dn, ','))
      if (!dn.empty())
        datanodes.insert(dn);
  }
  return true;
}

std::vector<uint8_t>
SquidProtocolFormatter::responseAck(bool isAck) const {
  return buildFrame(Opcode::RESPONSE, FLAG_RESPONSE,
                    {fieldBool(FieldID::ACK, isAck)});
}

std::vector<uint8_t>
SquidProtocolFormatter::responseAckWithVersion(bool isAck, int version) const {
  return buildFrame(
      Opcode::RESPONSE, FLAG_RESPONSE,
      {fieldBool(FieldID::ACK, isAck),
       fieldUint32(FieldID::FILE_VERSION, static_cast<uint32_t>(version))});
}

std::vector<uint8_t>
SquidProtocolFormatter::responseFormat(const std::string &ack) const {
  return responseAck(ack == "ACK");
}

std::vector<uint8_t> SquidProtocolFormatter::responsePort(int port) const {
  return buildFrame(Opcode::RESPONSE, FLAG_RESPONSE,
                    {fieldUint32(FieldID::PORT, static_cast<uint32_t>(port))});
}

std::vector<uint8_t>
SquidProtocolFormatter::responseIdentity(const std::string &nodeType,
                                         const std::string &processName) const {
  return buildFrame(Opcode::RESPONSE, FLAG_RESPONSE,
                    {fieldString(FieldID::NODE_TYPE, nodeType),
                     fieldString(FieldID::PROCESS_NAME, processName)});
}

std::vector<uint8_t> SquidProtocolFormatter::responseFileVersionMap(
    const std::map<std::string, int> &map) const {
  std::vector<BinaryField> fields;
  fields.reserve(map.size() * 2);
  for (const auto &kv : map) {
    fields.push_back(fieldString(FieldID::FILE_ENTRY, kv.first));
    fields.push_back(
        fieldUint32(FieldID::VER_ENTRY, static_cast<uint32_t>(kv.second)));
  }
  return buildFrame(Opcode::RESPONSE, FLAG_RESPONSE, fields);
}

std::vector<uint8_t> SquidProtocolFormatter::responseFormat(
    const std::map<std::string, long long> &fileTimeMap) const {
  std::map<std::string, int> m;
  for (const auto &kv : fileTimeMap)
    m[kv.first] = 0;
  return responseFileVersionMap(m);
}

std::vector<uint8_t> SquidProtocolFormatter::responseFormat(
    const std::map<std::string, fs::file_time_type> &filesLastWrite) const {
  std::map<std::string, int> m;
  for (const auto &kv : filesLastWrite)
    m[kv.first] = 0;
  return responseFileVersionMap(m);
}

// Header layout (13 bytes):
//   [magic:2][opcode:1][flags:1][nfields:1][seq:4][payloadLen:4]
Message
SquidProtocolFormatter::parseMessage(const std::vector<uint8_t> &frame) const {
  if (frame.size() < FRAME_HEADER_SIZE)
    throw std::runtime_error("Frame too short");

  const uint8_t *p = frame.data();

  uint16_t magic = readU16(p);
  p += 2;
  if (magic != SQUID_MAGIC)
    throw std::runtime_error("Bad magic");

  Message msg;
  msg.opcode = static_cast<Opcode>(*p++);
  msg.flags = *p++;
  uint8_t nf = *p++;
  msg.seq = readU32(p);
  p += 4;
  msg.payloadLen = readU32(p);
  p += 4;

  const uint8_t *end = frame.data() + frame.size();

  for (uint8_t i = 0; i < nf; ++i) {
    if (p + 3 > end)
      throw std::runtime_error("Frame truncated in field header");

    BinaryField f;
    f.id = static_cast<FieldID>(*p++);
    uint16_t vlen = readU16(p);
    p += 2;

    if (p + vlen > end)
      throw std::runtime_error("Frame truncated in field value");

    f.value.assign(p, p + vlen);
    p += vlen;
    msg.fields.push_back(std::move(f));
  }

  return msg;
}

Message SquidProtocolFormatter::makeNack() const {
  Message m;
  m.opcode = Opcode::RESPONSE;
  m.flags = FLAG_RESPONSE;
  m.fields.push_back(fieldBool(FieldID::ACK, false));
  return m;
}

Message SquidProtocolFormatter::makeAck() const {
  Message m;
  m.opcode = Opcode::RESPONSE;
  m.flags = FLAG_RESPONSE;
  m.fields.push_back(fieldBool(FieldID::ACK, true));
  return m;
}

const BinaryField *Message::findField(FieldID id) const noexcept {
  for (const auto &f : fields)
    if (f.id == id)
      return &f;
  return nullptr;
}

std::string Message::getString(FieldID id, const std::string &def) const {
  const BinaryField *f = findField(id);
  if (!f)
    return def;
  return std::string(f->value.begin(), f->value.end());
}

uint32_t Message::getUint32(FieldID id, uint32_t def) const {
  const BinaryField *f = findField(id);
  if (!f || f->value.size() < 4)
    return def;
  return (uint32_t(f->value[0]) << 24) | (uint32_t(f->value[1]) << 16) |
         (uint32_t(f->value[2]) << 8) | uint32_t(f->value[3]);
}

uint64_t Message::getUint64(FieldID id, uint64_t def) const {
  const BinaryField *f = findField(id);
  if (!f || f->value.size() < 8)
    return def;
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v = (v << 8) | f->value[i];
  return v;
}

bool Message::getBool(FieldID id, bool def) const {
  const BinaryField *f = findField(id);
  if (!f || f->value.empty())
    return def;
  return f->value[0] != 0x00;
}

std::map<std::string, int> Message::getFileVersionMap() const {
  std::vector<std::string> files;
  std::vector<uint32_t> versions;

  for (const auto &f : fields) {
    if (f.id == FieldID::FILE_ENTRY)
      files.push_back(std::string(f.value.begin(), f.value.end()));
    else if (f.id == FieldID::VER_ENTRY && f.value.size() >= 4)
      versions.push_back((uint32_t(f.value[0]) << 24) |
                         (uint32_t(f.value[1]) << 16) |
                         (uint32_t(f.value[2]) << 8) | uint32_t(f.value[3]));
  }

  std::map<std::string, int> result;
  size_t n = std::min(files.size(), versions.size());
  for (size_t i = 0; i < n; ++i)
    result[files[i]] = static_cast<int>(versions[i]);
  return result;
}

std::string Message::toString() const {
  std::ostringstream os;
  os << "Message{opcode=" << opcodeToString(opcode)
     << ",flags=" << static_cast<int>(flags) << ",seq=" << seq
     << ",payloadLen=" << payloadLen << ",fields=[";
  for (const auto &f : fields)
    os << static_cast<int>(f.id) << "(" << f.value.size() << "B),";
  os << "]}";
  return os.str();
}
