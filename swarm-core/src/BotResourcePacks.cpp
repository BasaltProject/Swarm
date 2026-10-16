#include <basalt/swarm/Bot.hpp>
#include <basalt/swarm/Nbt.hpp>

namespace basalt::swarm {
namespace {
bool skipString(ByteReader& reader, std::int32_t maximumBytes) {
    const auto length = reader.readVarInt();
    if (reader.failed() || length < 0 || length > maximumBytes) {
        return false;
    }
    reader.skip(static_cast<std::size_t>(length));
    return !reader.failed();
}
} // namespace

bool Bot::declineResourcePack(ByteReader& body, std::int32_t responseId) {
    if (responseId < 0 || body.failed() || body.remaining() < 20) {
        return false;
    }
    const auto payload = body.rest();
    ByteReader fields{payload};
    fields.skip(16);
    if (!skipString(fields, 32767 * 3) || !skipString(fields, 40 * 3)) {
        return false;
    }
    const auto required = fields.readByte();
    const auto hasPrompt = fields.readByte();
    if (fields.failed() || required > 1 || hasPrompt > 1) {
        return false;
    }
    if (hasPrompt != 0) {
        nbt::skip(fields);
    }
    if (fields.failed() || fields.remaining() != 0) {
        return false;
    }
    ByteWriter response;
    response.writeBytes(payload.first(16));
    response.writeVarInt(1);
    say("Bot " + std::to_string(options_.id) + " declined resource pack.");
    return sendPacket(responseId, response);
}

} // namespace basalt::swarm
