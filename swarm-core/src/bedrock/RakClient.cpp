#include <basalt/swarm/bedrock/RakClient.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <random>
#include <utility>

namespace basalt::swarm::bedrock {
namespace {

constexpr std::size_t kReceiveBuffer = 2048;

/// What a datagram spends before RakNet gets a byte, plus its own header, plus
/// the worst case of a frame header. What is left is what a payload may be.
constexpr std::size_t kDatagramOverhead = 28 + 4 + 23;

/// The biggest a RakNet datagram gets, which is what a client asks for first.
constexpr std::size_t kLargestMtu = 1400;

void writeMediumLE(std::vector<std::uint8_t> &out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xffU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
}

[[nodiscard]] std::uint32_t readMediumLE(std::span<const std::uint8_t> bytes,
                                         std::size_t at) {
  return static_cast<std::uint32_t>(bytes[at]) |
         (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[at + 2]) << 16U);
}

void writeBig(std::vector<std::uint8_t> &out, std::uint64_t value, int bytes) {
  for (int index = bytes - 1; index >= 0; --index) {
    out.push_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xffU));
  }
}

/// An address the way RakNet writes one: a version, the octets with every bit
/// flipped, the port.
void writeAddress(std::vector<std::uint8_t> &out, std::uint32_t address,
                  std::uint16_t port) {
  out.push_back(4);
  for (int octet = 3; octet >= 0; --octet) {
    out.push_back(
        static_cast<std::uint8_t>(~((address >> (octet * 8)) & 0xffU)));
  }
  writeBig(out, port, 2);
}

[[nodiscard]] std::uint64_t randomGuid() {
  std::random_device source;
  return (static_cast<std::uint64_t>(source()) << 32U) | source();
}

} // namespace

std::optional<Advertisement> Advertisement::parse(const std::string_view line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const std::size_t end = line.find(';', start);
    if (end == std::string_view::npos) {
      fields.emplace_back(line.substr(start));
      break;
    }
    fields.emplace_back(line.substr(start, end - start));
    start = end + 1;
  }
  // Edition, message, protocol, version, online, max, and then what a client
  // can live without. Fewer than six fields and it is not a server list entry.
  if (fields.size() < 6 || fields[0] != "MCPE") {
    return std::nullopt;
  }

  Advertisement advertisement;
  advertisement.raw = std::string{line};
  advertisement.edition = fields[0];
  advertisement.motd = fields[1];
  advertisement.protocol = std::atoi(fields[2].c_str());
  advertisement.version = fields[3];
  advertisement.online = std::atoi(fields[4].c_str());
  advertisement.max = std::atoi(fields[5].c_str());
  if (fields.size() > 10) {
    advertisement.port =
        static_cast<std::uint16_t>(std::atoi(fields[10].c_str()));
  }
  return advertisement;
}

std::optional<std::vector<std::uint8_t>>
RakClient::receiveRaw(const std::int32_t millis) {
  std::vector<std::uint8_t> buffer(kReceiveBuffer);
  const auto received = socket_.receive(buffer, millis);
  if (!received.has_value() || !received->has_value()) {
    return std::nullopt;
  }
  buffer.resize(**received);
  return buffer;
}

std::optional<Advertisement> RakClient::ping(const std::int32_t millis) {
  auto socket = UdpClient::to(host_, port_);
  if (!socket.has_value()) {
    return std::nullopt;
  }
  socket_ = std::move(*socket);
  guid_ = randomGuid();

  std::vector<std::uint8_t> request;
  request.push_back(0x01);
  writeBig(request, 0, 8);
  request.insert(request.end(), kMagic.begin(), kMagic.end());
  writeBig(request, guid_, 8);
  if (!socket_.send(request).has_value()) {
    return std::nullopt;
  }

  const auto answer = receiveRaw(millis);
  if (!answer.has_value() || answer->size() < 35 || (*answer)[0] != 0x1c) {
    return std::nullopt;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), answer->begin() + 17)) {
    return std::nullopt;
  }
  const std::size_t length =
      (static_cast<std::size_t>((*answer)[33]) << 8U) | (*answer)[34];
  if (35 + length > answer->size()) {
    return std::nullopt;
  }
  return Advertisement::parse(std::string_view{
      reinterpret_cast<const char *>(answer->data() + 35), length});
}

bool RakClient::connect(std::string &problem) {
  auto socket = UdpClient::to(host_, port_);
  if (!socket.has_value()) {
    problem = socket.error().message;
    return false;
  }
  socket_ = std::move(*socket);
  if (guid_ == 0) {
    guid_ = randomGuid();
  }

  // The first round asks with a datagram as big as the client wants to try: the
  // padding is the question. A server answers with what it will take, and
  // asking for the smallest would make every later packet split for no reason.
  std::vector<std::uint8_t> first;
  first.push_back(0x05);
  first.insert(first.end(), kMagic.begin(), kMagic.end());
  first.push_back(kRakNetProtocol);
  first.resize(kLargestMtu - 28, 0);
  if (!socket_.send(first).has_value()) {
    problem = "could not send the first open connection request";
    return false;
  }

  const auto reply = receiveRaw(3000);
  if (!reply.has_value() || reply->size() < 28) {
    problem = "no answer to the open connection request";
    return false;
  }
  if ((*reply)[0] == 0x19) {
    problem = "the server speaks a different RakNet version";
    return false;
  }
  if ((*reply)[0] != 0x06) {
    problem = "the server answered the open connection request with " +
              std::to_string((*reply)[0]);
    return false;
  }
  mtu_ = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>((*reply)[reply->size() - 2]) << 8U) |
      (*reply)[reply->size() - 1]);

  std::vector<std::uint8_t> second;
  second.push_back(0x07);
  second.insert(second.end(), kMagic.begin(), kMagic.end());
  writeAddress(second, 0x7f000001U, port_);
  writeBig(second, mtu_, 2);
  writeBig(second, guid_, 8);
  if (!socket_.send(second).has_value()) {
    problem = "could not send the second open connection request";
    return false;
  }

  const auto accepted = receiveRaw(3000);
  if (!accepted.has_value() || accepted->empty() || (*accepted)[0] != 0x08) {
    problem = "the server would not open the connection";
    return false;
  }

  std::vector<std::uint8_t> request;
  request.push_back(0x09);
  writeBig(request, guid_, 8);
  writeBig(request, 0, 8);
  request.push_back(0);
  sendMessage(request);

  // The server answers with the addresses it serves on, and then expects to be
  // told we are in.
  for (int attempt = 0; attempt < 20; ++attempt) {
    const auto datagram = receiveRaw(1000);
    if (!datagram.has_value()) {
      continue;
    }
    std::vector<std::vector<std::uint8_t>> messages;
    handleFrames(*datagram, messages);
    for (const std::vector<std::uint8_t> &message : messages) {
      if (message.empty() || message[0] != 0x10) {
        continue;
      }
      std::vector<std::uint8_t> incoming;
      incoming.push_back(0x13);
      writeAddress(incoming, 0x7f000001U, port_);
      for (int index = 0; index < 20; ++index) {
        writeAddress(incoming, 0, 0);
      }
      writeBig(incoming, 0, 8);
      writeBig(incoming, 0, 8);
      sendMessage(incoming);
      connected_ = true;
      return true;
    }
  }
  problem = "the server never accepted the connection request";
  return false;
}

void RakClient::sendDatagram(const std::span<const std::uint8_t> frame) {
  std::vector<std::uint8_t> datagram;
  datagram.reserve(frame.size() + 4);
  datagram.push_back(0x80);
  writeMediumLE(datagram, nextSequence_++);
  datagram.insert(datagram.end(), frame.begin(), frame.end());
  static_cast<void>(socket_.send(datagram));
}

void RakClient::sendMessage(const std::span<const std::uint8_t> body,
                            const std::uint8_t reliability) {
  std::vector<std::uint8_t> frame;
  frame.push_back(static_cast<std::uint8_t>(reliability << 5U));
  writeBig(frame, body.size() * 8, 2);
  if (reliability == 3) {
    writeMediumLE(frame, nextReliable_++);
    writeMediumLE(frame, nextOrder_++);
    frame.push_back(0);
  }
  frame.insert(frame.end(), body.begin(), body.end());
  sendDatagram(frame);
}

void RakClient::sendGame(const std::span<const std::uint8_t> payload) {
  std::vector<std::uint8_t> body;
  body.reserve(payload.size() + 1);
  body.push_back(0xfe);
  body.insert(body.end(), payload.begin(), payload.end());

  const std::size_t budget = mtu_ - kDatagramOverhead;
  if (body.size() <= budget) {
    sendMessage(body);
    return;
  }

  const auto parts =
      static_cast<std::uint32_t>((body.size() + budget - 1) / budget);
  const std::uint16_t splitId = nextSplitId_++;
  const std::uint32_t order = nextOrder_++;
  for (std::uint32_t part = 0; part < parts; ++part) {
    const std::size_t offset = static_cast<std::size_t>(part) * budget;
    const std::size_t length = std::min(budget, body.size() - offset);

    std::vector<std::uint8_t> frame;
    frame.push_back(static_cast<std::uint8_t>((3U << 5U) | 0x10U));
    writeBig(frame, length * 8, 2);
    writeMediumLE(frame, nextReliable_++);
    writeMediumLE(frame, order);
    frame.push_back(0);
    writeBig(frame, parts, 4);
    writeBig(frame, splitId, 2);
    writeBig(frame, part, 4);
    frame.insert(frame.end(),
                 body.begin() + static_cast<std::ptrdiff_t>(offset),
                 body.begin() + static_cast<std::ptrdiff_t>(offset + length));
    sendDatagram(frame);
  }
}

void RakClient::acknowledge(const std::uint32_t sequence) {
  std::vector<std::uint8_t> ack;
  ack.push_back(0x80 | 0x40);
  writeBig(ack, 1, 2);
  ack.push_back(1);
  writeMediumLE(ack, sequence);
  static_cast<void>(socket_.send(ack));
}

void RakClient::handleFrames(const std::span<const std::uint8_t> datagram,
                             std::vector<std::vector<std::uint8_t>> &out) {
  if (datagram.empty() || (datagram[0] & 0x80U) == 0) {
    return;
  }
  // An acknowledgement of ours, or a complaint that one went missing. A bot
  // answers neither: it sends everything reliably and the server asks again if
  // it has to.
  if ((datagram[0] & 0x40U) != 0 || (datagram[0] & 0x20U) != 0) {
    return;
  }
  if (datagram.size() < 4) {
    return;
  }
  acknowledge(readMediumLE(datagram, 1));

  std::size_t at = 4;
  while (at < datagram.size()) {
    const std::uint8_t flags = datagram[at++];
    const auto reliability = static_cast<std::uint8_t>(flags >> 5U);
    const bool split = (flags & 0x10U) != 0;
    if (at + 2 > datagram.size()) {
      return;
    }
    const std::size_t bits =
        (static_cast<std::size_t>(datagram[at]) << 8U) | datagram[at + 1];
    at += 2;
    const std::size_t length = (bits + 7) / 8;

    const bool reliable = reliability == 2 || reliability == 3 ||
                          reliability == 4 || reliability == 7;
    const bool sequenced =
        reliability == 1 || reliability == 4 || reliability == 6;
    const bool ordered = reliability == 3;
    std::uint32_t reliableIndex = 0;
    if (reliable) {
      if (at + 3 > datagram.size()) {
        return;
      }
      reliableIndex = readMediumLE(datagram, at);
      at += 3;
    }
    if (sequenced) {
      at += 3;
    }
    std::uint32_t orderIndex = 0;
    std::uint8_t orderChannel = 0;
    if (ordered || sequenced) {
      if (at + 4 > datagram.size()) {
        return;
      }
      orderIndex = readMediumLE(datagram, at);
      orderChannel = datagram[at + 3];
      at += 4;
    }

    std::uint32_t parts = 0;
    std::uint16_t splitId = 0;
    std::uint32_t index = 0;
    if (split) {
      if (at + 10 > datagram.size()) {
        return;
      }
      parts = (static_cast<std::uint32_t>(datagram[at]) << 24U) |
              (static_cast<std::uint32_t>(datagram[at + 1]) << 16U) |
              (static_cast<std::uint32_t>(datagram[at + 2]) << 8U) |
              datagram[at + 3];
      splitId = static_cast<std::uint16_t>(
          (static_cast<std::uint16_t>(datagram[at + 4]) << 8U) |
          datagram[at + 5]);
      index = (static_cast<std::uint32_t>(datagram[at + 6]) << 24U) |
              (static_cast<std::uint32_t>(datagram[at + 7]) << 16U) |
              (static_cast<std::uint32_t>(datagram[at + 8]) << 8U) |
              datagram[at + 9];
      at += 10;
    }

    if (at + length > datagram.size()) {
      return;
    }
    std::vector<std::uint8_t> body{
        datagram.begin() + static_cast<std::ptrdiff_t>(at),
        datagram.begin() + static_cast<std::ptrdiff_t>(at + length)};
    at += length;

    // A frame sent again after a loss arrives twice, and a bad copy of an
    // encrypted stream is the same as a lost one.
    if (reliable && !seenReliable_.insert(reliableIndex).second) {
      continue;
    }

    if (!split) {
      deliver(std::move(body), ordered, orderIndex, orderChannel, out);
      continue;
    }

    splitCounts_[splitId] = parts;
    splits_[splitId][index] = std::move(body);
    if (splits_[splitId].size() < parts) {
      continue;
    }
    std::vector<std::uint8_t> whole;
    for (auto &[partIndex, part] : splits_[splitId]) {
      whole.insert(whole.end(), part.begin(), part.end());
    }
    splits_.erase(splitId);
    splitCounts_.erase(splitId);
    deliver(std::move(whole), ordered, orderIndex, orderChannel, out);
  }
}

void RakClient::deliver(std::vector<std::uint8_t> &&message, const bool ordered,
                        const std::uint32_t orderIndex,
                        const std::uint8_t orderChannel,
                        std::vector<std::vector<std::uint8_t>> &out) {
  if (!ordered) {
    out.push_back(std::move(message));
    return;
  }
  Channel &channel = channels_[orderChannel % kChannels];
  if (orderIndex < channel.expected) {
    return;
  }
  if (orderIndex > channel.expected) {
    channel.held.emplace(orderIndex, std::move(message));
    return;
  }
  ++channel.expected;
  out.push_back(std::move(message));
  for (auto held = channel.held.find(channel.expected);
       held != channel.held.end(); held = channel.held.find(channel.expected)) {
    out.push_back(std::move(held->second));
    channel.held.erase(held);
    ++channel.expected;
  }
}

std::vector<std::vector<std::uint8_t>>
RakClient::poll(const std::int32_t millis) {
  std::vector<std::vector<std::uint8_t>> messages;
  const auto datagram = receiveRaw(millis);
  if (!datagram.has_value()) {
    return {};
  }
  handleFrames(*datagram, messages);

  std::vector<std::vector<std::uint8_t>> payloads;
  for (std::vector<std::uint8_t> &message : messages) {
    if (message.empty()) {
      continue;
    }
    if (message[0] == 0x15) {
      connected_ = false;
      continue;
    }
    if (message[0] == 0x00 && message.size() >= 9) {
      // A ping, answered, or the server decides we are gone.
      std::vector<std::uint8_t> pong;
      pong.push_back(0x03);
      pong.insert(pong.end(), message.begin() + 1, message.begin() + 9);
      pong.insert(pong.end(), message.begin() + 1, message.begin() + 9);
      sendMessage(pong, 0);
      continue;
    }
    if (message[0] != 0xfe) {
      continue;
    }
    payloads.emplace_back(message.begin() + 1, message.end());
  }
  return payloads;
}

void RakClient::disconnect() {
  if (!connected_) {
    return;
  }
  const std::array<std::uint8_t, 1> notification{0x15};
  sendMessage(notification);
  connected_ = false;
  socket_.close();
}

} // namespace basalt::swarm::bedrock
