#include <basalt/swarm/Bot.hpp>
#include <basalt/swarm/Dialogs.hpp>
#include <basalt/swarm/Nbt.hpp>
#include <basalt/swarm/Zlib.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>

namespace basalt::swarm {
namespace {

/// A tick, which is what the walk runs at and what the socket waits for between
/// reads.
constexpr std::int32_t kTickMillis = 50;

/// The most a single frame may claim to be. Vanilla's own cap, and without it a
/// bad length is a bot asking for a gigabyte.
constexpr std::int32_t kMaxFrameBytes = 8 * 1024 * 1024;

/// parity: `LivingEntity`'s own numbers, so a bot falls and jumps the way a
/// player does.
constexpr double kGravity = 0.08;
constexpr double kDrag = 0.98;
constexpr double kTerminalVelocity = -3.92;
constexpr double kJumpVelocity = 0.42;
constexpr double kGroundSpeed = 0.15;
constexpr double kAirSpeed = 0.02;

/// How far a server lets a player reach an entity, which is what a click beyond
/// is refused for.
constexpr double kInteractReach = 3.0;

/// The floor a bot bounces off when it has nothing else. Without it a bot that
/// spawned over a hole falls out of the world for the rest of the run.
constexpr double kFloor = 63.0;

[[nodiscard]] std::vector<std::uint8_t> withVarInt(const std::int32_t value, const std::span<const std::uint8_t> body) {
    ByteWriter framed;
    framed.writeVarInt(value);
    framed.writeBytes(body);
    const std::span<const std::uint8_t> bytes = framed.bytes();
    return {bytes.begin(), bytes.end()};
}

/// A point written as three numbers with commas between them.
[[nodiscard]] std::array<std::int32_t, 3> pointOf(const std::string& text) {
    std::array<std::int32_t, 3> point{0, 0, 0};
    std::size_t at = 0;
    for (std::size_t part = 0; part < 3 && at <= text.size(); ++part) {
        const std::size_t comma = std::min(text.find(',', at), text.size());
        point[part] = static_cast<std::int32_t>(std::strtol(text.substr(at, comma - at).c_str(), nullptr, 10));
        at = comma + 1;
    }
    return point;
}

} // namespace

Bot::Bot(Options options)
    : options_(std::move(options)),
      checks_(options_.profile.protocol,
              [this](const std::string& what) { say("Bot " + std::to_string(options_.id) + ": " + what); }),
      random_(std::random_device{}()) {
    checks_.dumpCommandsTo(options_.commandsDump);
}

void Bot::say(const std::string& line) const {
    if (options_.say) {
        options_.say(line);
    }
}

bool Bot::sendPacket(const std::int32_t packetId, const ByteWriter& body) {
    std::vector<std::uint8_t> payload = withVarInt(packetId, body.bytes());
    std::vector<std::uint8_t> framed;
    if (compressionThreshold_ >= 0) {
        if (static_cast<std::int32_t>(payload.size()) >= compressionThreshold_) {
            const std::expected<std::vector<std::uint8_t>, zlib::ZlibError> compressed = zlib::compress(payload);
            if (!compressed.has_value()) {
                say("Bot " + std::to_string(options_.id) +
                    " could not compress a packet: " + compressed.error().message);
                return false;
            }
            framed = withVarInt(static_cast<std::int32_t>(payload.size()), *compressed);
        } else {
            framed = withVarInt(0, payload);
        }
    } else {
        framed = std::move(payload);
    }
    const std::vector<std::uint8_t> out = withVarInt(static_cast<std::int32_t>(framed.size()), framed);
    return socket_.writeAll(out).has_value();
}

bool Bot::sendEmpty(const std::int32_t packetId) { return sendPacket(packetId, ByteWriter{}); }

bool Bot::sendHandshakeAndLogin() {
    ByteWriter handshake;
    handshake.writeVarInt(options_.profile.protocol);
    handshake.writeVarString(options_.host);
    handshake.writeShort(static_cast<std::int16_t>(options_.port));
    handshake.writeVarInt(2); // Next state: login.
    if (!sendPacket(0x00, handshake)) {
        return false;
    }

    ByteWriter hello;
    hello.writeVarString("Bot_" + std::to_string(options_.id));
    std::array<std::uint8_t, 16> uuid{};
    std::uniform_int_distribution<std::uint32_t> byte{0, 255};
    for (std::uint8_t& value : uuid) {
        value = static_cast<std::uint8_t>(byte(random_));
    }
    // Version 4, variant 1, which is what a client sends and what a server
    // checks.
    uuid[6] = static_cast<std::uint8_t>((uuid[6] & 0x0F) | 0x40);
    uuid[8] = static_cast<std::uint8_t>((uuid[8] & 0x3F) | 0x80);
    // The login start is the name alone until 1.19, gains an optional signature
    // key in 1.19, an optional uuid from 1.19.1 and a plain uuid in 1.20.2.
    const std::int32_t protocol = options_.profile.protocol;
    if (protocol >= 764) {
        hello.writeBytes(uuid);
    } else if (protocol >= 761) {
        hello.writeByte(1);
        hello.writeBytes(uuid);
    } else if (protocol >= 759) {
        hello.writeByte(0);
        if (protocol >= 760) {
            hello.writeByte(1);
            hello.writeBytes(uuid);
        }
    }
    if (!sendPacket(options_.profile.login.serverboundHello, hello)) {
        return false;
    }
    say("Bot " + std::to_string(options_.id) + " sent Login Start");
    return true;
}

bool Bot::sendClientInformation() {
    const ProtocolProfile& profile = options_.profile;
    const std::int32_t id = profile.hasConfigurationPhase ? profile.configuration.serverboundClientInformation
                                                          : profile.play.serverboundClientInformation;
    ByteWriter settings;
    settings.writeVarString("en_us");
    settings.writeByte(2);    // view distance
    settings.writeVarInt(0);  // chat mode
    settings.writeByte(1);    // chat colours
    settings.writeByte(0x7F); // skin parts
    if (profile.settingsHaveMainHand) {
        settings.writeVarInt(1);
    }
    if (profile.settingsHaveTextFiltering) {
        settings.writeByte(0);
    }
    if (profile.settingsHaveServerListing) {
        settings.writeByte(1);
    }
    if (profile.settingsHaveParticleStatus) {
        settings.writeVarInt(1);
    }
    return sendPacket(id, settings);
}

bool Bot::sendPositionLook() {
    ByteWriter body;
    body.writeDouble(x_);
    body.writeDouble(y_);
    body.writeDouble(z_);
    body.writeFloat(yaw_);
    body.writeFloat(pitch_);
    body.writeByte(onGround_ ? 1 : 0);
    return sendPacket(options_.profile.play.serverboundMovePosRot, body);
}

bool Bot::handlePacket(const std::int32_t packetId, ByteReader& body) {
    switch (state_) {
    case ProtocolState::Login:
        return handleLoginPacket(packetId, body);
    case ProtocolState::Configuration:
        return handleConfigurationPacket(packetId, body);
    case ProtocolState::Play:
        return handlePlayPacket(packetId, body);
    }
    return false;
}

bool Bot::handleLoginPacket(const std::int32_t packetId, ByteReader& body) {
    const ProtocolProfile& profile = options_.profile;
    const std::string name = "Bot " + std::to_string(options_.id);
    if (packetId == profile.login.clientboundDisconnect) {
        const std::span<const std::uint8_t> reason = body.rest();
        say(name + " disconnected in Login. Reason: " + std::string{reason.begin(), reason.end()});
        return false;
    }
    if (packetId == profile.login.clientboundFinished) {
        // Up to 1.15.2 the uuid is text, from 1.16 it is sixteen bytes, and the
        // property list is there from 1.19; anything else is a server that
        // answered in a shape this client could not read.
        const std::int32_t protocol = profile.protocol;
        bool readable = true;
        if (protocol < 735) {
            const std::string id = body.readVarString();
            readable = !body.failed() && id.size() == 36;
        } else {
            for (int i = 0; i < 16; ++i) {
                static_cast<void>(body.readByte());
            }
        }
        const std::string reported = body.readVarString();
        readable = readable && !body.failed() && reported.starts_with("Bot_");
        if (protocol >= 759 && readable) {
            const std::int32_t properties = body.readVarInt();
            readable = !body.failed() && properties >= 0;
            for (std::int32_t i = 0; readable && i < properties; ++i) {
                static_cast<void>(body.readVarString());
                static_cast<void>(body.readVarString());
                if (body.readByte() != 0) {
                    static_cast<void>(body.readVarString());
                }
                readable = !body.failed();
            }
        }
        // What follows the profile: nothing, a strict-errors flag in 1.20.5
        // to 1.21.1, the session id from 26.2.
        const std::size_t tail = protocol >= 776 ? 16 : (protocol == 766 || protocol == 767 ? 1 : 0);
        readable = readable && body.remaining() == tail;
        if (!readable) {
            say(name + " could not read the Login Success it was sent");
            return false;
        }
        static_cast<void>(body.rest());
        if (!profile.hasConfigurationPhase) {
            // Before 1.20.2 there is nothing to acknowledge: the game has already
            // started.
            say(name + " received Login Success. Already playing.");
            state_ = ProtocolState::Play;
            return sendClientInformation();
        }
        say(name + " received Login Success. Sending Acknowledged.");
        if (!sendEmpty(profile.login.serverboundAcknowledged)) {
            return false;
        }
        state_ = ProtocolState::Configuration;
        return sendClientInformation();
    }
    if (packetId == profile.login.clientboundCompression) {
        compressionThreshold_ = body.readVarInt();
        say(name + " enabled compression. Threshold: " + std::to_string(compressionThreshold_));
        return true;
    }
    static_cast<void>(body.rest());
    return true;
}

bool Bot::handleConfigurationPacket(const std::int32_t packetId, ByteReader& body) {
    const ProtocolProfile& profile = options_.profile;
    if (profile.configuration.clientboundResourcePackPush >= 0 &&
        packetId == profile.configuration.clientboundResourcePackPush) {
        return declineResourcePack(body, profile.configuration.serverboundResourcePackResponse);
    }
    const std::string name = "Bot " + std::to_string(options_.id);
    if (packetId == profile.configuration.clientboundDisconnect) {
        const std::span<const std::uint8_t> reason = body.rest();
        say(name + " was disconnected while configuring. Reason: " + std::string{reason.begin(), reason.end()});
        return false;
    }
    if (packetId == profile.configuration.clientboundKeepAlive) {
        ByteWriter answer;
        answer.writeBytes(body.rest());
        return sendPacket(profile.configuration.serverboundKeepAlive, answer);
    }
    // A server that waits for the client to catch up, as Fabric API does, sends a
    // ping and holds the join until the pong.
    if (packetId == profile.configuration.clientboundPing) {
        ByteWriter answer;
        answer.writeBytes(body.rest());
        return sendPacket(profile.configuration.serverboundPong, answer);
    }
    if (packetId == profile.configuration.clientboundFinish) {
        static_cast<void>(body.rest());
        if (!sendEmpty(profile.configuration.serverboundFinish)) {
            return false;
        }
        state_ = ProtocolState::Play;
        say(name + " joined the game (Play State)");
        return true;
    }
    if (profile.configuration.clientboundSelectKnownPacks >= 0 &&
        packetId == profile.configuration.clientboundSelectKnownPacks) {
        static_cast<void>(body.rest());
        ByteWriter none;
        none.writeVarInt(0);
        return sendPacket(profile.configuration.serverboundSelectKnownPacks, none);
    }
    if (const std::int32_t registryData = checks_.packetId("minecraft:registry_data");
        registryData >= 0 && packetId == registryData) {
        checks_.registryData(body);
        return true;
    }
    if (const std::int32_t updateTags = checks_.packetId("minecraft:update_tags");
        updateTags >= 0 && packetId == updateTags) {
        checks_.tags(body);
        return true;
    }
    static_cast<void>(body.rest());
    return true;
}

bool Bot::handlePlayPacket(const std::int32_t packetId, ByteReader& body) {
    const ProtocolProfile& profile = options_.profile;
    if (profile.play.clientboundResourcePackPush >= 0 && packetId == profile.play.clientboundResourcePackPush) {
        return declineResourcePack(body, profile.play.serverboundResourcePackResponse);
    }
    if (profile.play.clientboundStartConfiguration >= 0 && packetId == profile.play.clientboundStartConfiguration) {
        return beginConfiguration(body);
    }
    const std::string name = "Bot " + std::to_string(options_.id);
    if (profile.play.clientboundShowDialog >= 0 && packetId == profile.play.clientboundShowDialog) {
        say(name + " was shown a dialog.");
        if (options_.answerDialog.has_value() && profile.play.serverboundCustomClickAction >= 0) {
            const std::optional<dialogs::Click> click = dialogs::answer(body, *options_.answerDialog);
            if (!click.has_value()) {
                say(name + " could not read a button " + std::to_string(*options_.answerDialog) + " in that dialog.");
                return true;
            }
            ByteWriter answer;
            answer.writeVarString(click->id);
            answer.writeVarInt(static_cast<std::int32_t>(click->payload.size()));
            answer.writeBytes(click->payload);
            say(name + " pressed button " + std::to_string(*options_.answerDialog) + " (" + click->id + ").");
            return sendPacket(profile.play.serverboundCustomClickAction, answer);
        }
        static_cast<void>(body.rest());
        return true;
    }
    if (packetId == profile.play.clientboundDisconnect) {
        const std::span<const std::uint8_t> reason = body.rest();
        say(name + " was kicked. Reason: " + std::string{reason.begin(), reason.end()});
        return false;
    }
    if (packetId == profile.play.clientboundKeepAlive) {
        ByteWriter answer;
        answer.writeBytes(body.rest());
        return sendPacket(profile.play.serverboundKeepAlive, answer);
    }
    if (profile.play.clientboundSystemChat >= 0 && packetId == profile.play.clientboundSystemChat) {
        // What the server says to everyone: chat, a death, somebody arriving.
        // Since 1.20.3 the sentence travels as a tag, so it is read as one rather
        // than as json.
        const std::string words = nbt::text(body);
        const bool overlay = body.readByte() != 0;
        if (body.failed() || words.empty()) {
            static_cast<void>(body.rest());
            return true;
        }
        heard_.push_back(words);
        say(name + (overlay ? " sees over the hotbar: " : " heard: ") + words);
        static_cast<void>(body.rest());
        return true;
    }
    if (profile.play.clientboundAddEntity >= 0 && packetId == profile.play.clientboundAddEntity) {
        // Who else is here, as this client is told about them. A player entity is
        // what another player looks like from inside the game, whichever edition
        // they arrived on.
        const std::int32_t entityId = body.readVarInt();
        body.skip(16); // The uuid, which names the profile rather than the entity.
        const std::int32_t entityType = body.readVarInt();
        const double x = body.readDouble();
        const double y = body.readDouble();
        const double z = body.readDouble();
        if (body.failed()) {
            return true;
        }
        entities_.insert_or_assign(entityId, SeenEntity{.type = entityType, .x = x, .y = y, .z = z});
        if (entityType == profile.playerEntityType && entitiesSeen_.insert(entityId).second) {
            say(name + " can see another player at " + std::to_string(x) + ", " + std::to_string(y) + ", " +
                std::to_string(z) + ".");
        }
        static_cast<void>(body.rest());
        return true;
    }
    if (packetId == profile.play.clientboundPlayerPosition) {
        std::int32_t teleportId = 0;
        if (profile.positionStartsWithTeleportId) {
            teleportId = body.readVarInt();
            x_ = body.readDouble();
            y_ = body.readDouble();
            z_ = body.readDouble();
        } else {
            // Before 1.21.2 the id is at the end, after the angles and the relative
            // flags.
            x_ = body.readDouble();
            y_ = body.readDouble();
            z_ = body.readDouble();
            static_cast<void>(body.readFloat());
            static_cast<void>(body.readFloat());
            static_cast<void>(body.readByte());
            teleportId = profile.positionIsAcknowledged ? body.readVarInt() : 0;
        }
        yVelocity_ = 0.0;
        positionReceived_ = true;
        static_cast<void>(body.rest());

        if (profile.positionIsAcknowledged) {
            ByteWriter accept;
            accept.writeVarInt(teleportId);
            if (profile.acceptCarriesPosition) {
                accept.writeDouble(x_);
                accept.writeDouble(y_);
                accept.writeDouble(z_);
                accept.writeFloat(0.0F);
                accept.writeFloat(0.0F);
            }
            if (!sendPacket(profile.play.serverboundAcceptTeleportation, accept)) {
                return false;
            }
        }
        if (!options_.commandLine.empty() && !commandSent_ && profile.play.serverboundChatCommand >= 0) {
            commandSent_ = true;
            ByteWriter command;
            command.writeVarString(options_.commandLine);
            if (!sendPacket(profile.play.serverboundChatCommand, command)) {
                return false;
            }
        }
        if (!rawSent_ && !options_.rawPackets.empty()) {
            rawSent_ = true;
            for (const auto& [id, bytes] : options_.rawPackets) {
                ByteWriter raw;
                raw.writeBytes(bytes);
                if (!sendPacket(id, raw)) {
                    return false;
                }
            }
        }
        // Every sync puts the bot back on the spawn, so the target is claimed again
        // after each one or the swarm quietly collapses into a single spot.
        if (options_.target.has_value()) {
            x_ = options_.target->x;
            z_ = options_.target->z;
            return sendPositionLook();
        }
        return true;
    }
    if (const std::int32_t commands = checks_.packetId("minecraft:commands"); commands >= 0 && packetId == commands) {
        checks_.commands(body);
        return true;
    }
    if (const std::int32_t recipes = checks_.packetId("minecraft:update_recipes");
        recipes >= 0 && packetId == recipes) {
        checks_.updateRecipes(body);
        return true;
    }
    static_cast<void>(body.rest());
    return true;
}

void Bot::tickPhysics() {
    if (!positionReceived_) {
        return;
    }
    std::uniform_real_distribution<double> chance{0.0, 1.0};
    if (wanderTicks_ <= 0) {
        if (chance(random_) < 0.2) {
            wandering_ = true;
            wanderTicks_ = std::uniform_int_distribution<std::int32_t>{20, 99}(random_);
            yaw_ = static_cast<float>(chance(random_) * 360.0 - 180.0);
        } else {
            wandering_ = false;
            wanderTicks_ = std::uniform_int_distribution<std::int32_t>{20, 59}(random_);
        }
    } else {
        wanderTicks_--;
    }

    if (onGround_ && wandering_ && chance(random_) < 0.05) {
        yVelocity_ = kJumpVelocity;
        onGround_ = false;
    }
    if (!onGround_) {
        yVelocity_ = (yVelocity_ - kGravity) * kDrag;
        yVelocity_ = std::max(yVelocity_, kTerminalVelocity);
    }
    if (wandering_) {
        const double speed = onGround_ ? kGroundSpeed : kAirSpeed;
        const double radians = static_cast<double>(yaw_) * std::numbers::pi / 180.0;
        x_ += -std::sin(radians) * speed;
        z_ += std::cos(radians) * speed;
    }
    y_ += yVelocity_;
    if (y_ <= kFloor) {
        y_ = kFloor;
        yVelocity_ = 0.0;
        onGround_ = true;
    } else {
        onGround_ = false;
    }
    static_cast<void>(sendPositionLook());
}

void Bot::tickPlace() {
    if (options_.placeAt.empty() || placesSent_ >= options_.placeCount || !positionReceived_) {
        return;
    }
    if (options_.profile.play.serverboundUseItemOn < 0) {
        say("Bot " + std::to_string(options_.id) + " cannot place on " + options_.profile.label +
            ": this tool has not been told that version's packet number.");
        placesSent_ = options_.placeCount;
        return;
    }
    const std::int64_t now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    if (nextPlaceAt_ == 0) {
        nextPlaceAt_ = now + options_.interactEveryMillis;
    }
    if (now < nextPlaceAt_) {
        return;
    }
    const std::array<std::int32_t, 3> at = pointOf(options_.placeAt);
    // Each go moves one square up from the last, which is how a tower rises under
    // a player. With a side face that means climbing the wall rather than
    // stacking on the floor.
    const std::int64_t packed = ((static_cast<std::int64_t>(at[0]) & 0x3FFFFFF) << 38) |
                                ((static_cast<std::int64_t>(at[2]) & 0x3FFFFFF) << 12) |
                                (static_cast<std::int64_t>(at[1] + placesSent_) & 0xFFF);
    // Where on that face the click lands, which is the middle of the face itself.
    const std::int32_t face = options_.placeFace;
    const float cursorX = face == 4 ? 0.0F : (face == 5 ? 1.0F : 0.5F);
    const float cursorY = face == 0 ? 0.0F : (face == 1 ? 1.0F : 0.5F);
    const float cursorZ = face == 2 ? 0.0F : (face == 3 ? 1.0F : 0.5F);
    ByteWriter body;
    body.writeVarInt(0); // The main hand.
    body.writeLong(packed);
    body.writeVarInt(face);
    body.writeFloat(cursorX);
    body.writeFloat(cursorY);
    body.writeFloat(cursorZ);
    body.writeByte(0); // Not inside the block.
    body.writeByte(0); // Nothing to do with the world border.
    body.writeVarInt(++sequence_);
    if (!sendPacket(options_.profile.play.serverboundUseItemOn, body)) {
        return;
    }
    placesSent_++;
    nextPlaceAt_ = now + options_.interactEveryMillis;
    say("Bot " + std::to_string(options_.id) + " used what is in hand on " + std::to_string(at[0]) + "," +
        std::to_string(at[1] + placesSent_ - 1) + "," + std::to_string(at[2]) + ", " + std::to_string(placesSent_) +
        " of " + std::to_string(options_.placeCount) + ".");
}

void Bot::tickInteract() {
    if (!options_.interactType.has_value() || interactsSent_ >= options_.interactCount || !positionReceived_) {
        return;
    }
    const std::int64_t now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    if (nextInteractAt_ == 0) {
        nextInteractAt_ = now + options_.interactEveryMillis;
    }
    if (now < nextInteractAt_) {
        return;
    }
    if (options_.profile.play.serverboundInteract < 0) {
        say("Bot " + std::to_string(options_.id) + " cannot interact on " + options_.profile.label +
            ": this tool has not been told that version's packet number.");
        interactsSent_ = options_.interactCount;
        return;
    }
    std::int32_t nearest = -1;
    double nearestDistance = kInteractReach * kInteractReach;
    for (const auto& [entityId, seen] : entities_) {
        if (seen.type != *options_.interactType) {
            continue;
        }
        const double dx = seen.x - x_;
        const double dy = seen.y - y_;
        const double dz = seen.z - z_;
        const double distance = (dx * dx) + (dy * dy) + (dz * dz);
        if (distance <= nearestDistance) {
            nearest = entityId;
            nearestDistance = distance;
        }
    }
    if (nearest < 0) {
        return;
    }
    ByteWriter body;
    body.writeVarInt(nearest);
    body.writeVarInt(0);
    // The click location, as the quantised vector 26.2 uses: a single zero byte
    // is the whole of "nowhere in particular", which is what a right-click on an
    // animal carries anyway.
    body.writeByte(0);
    // Not sneaking: a sneaking click is a different interaction for several
    // blocks and mobs.
    body.writeByte(0);
    if (!sendPacket(options_.profile.play.serverboundInteract, body)) {
        return;
    }
    interactsSent_++;
    nextInteractAt_ = now + options_.interactEveryMillis;
    say("Bot " + std::to_string(options_.id) + " right-clicked entity " + std::to_string(nearest) + ", " +
        std::to_string(interactsSent_) + " of " + std::to_string(options_.interactCount) + ".");
}

bool Bot::drainFrames() {
    while (true) {
        ByteReader header{incoming_};
        const std::int32_t length = header.readVarInt();
        if (header.failed()) {
            // Not even the length has arrived yet.
            return true;
        }
        if (length < 0 || length > kMaxFrameBytes) {
            say("Bot " + std::to_string(options_.id) + " read a frame of " + std::to_string(length) + " bytes");
            return false;
        }
        const std::size_t headerSize = incoming_.size() - header.remaining();
        if (incoming_.size() < headerSize + static_cast<std::size_t>(length)) {
            return true;
        }
        std::vector<std::uint8_t> frame(incoming_.begin() + static_cast<std::ptrdiff_t>(headerSize),
                                        incoming_.begin() + static_cast<std::ptrdiff_t>(headerSize) + length);
        incoming_.erase(incoming_.begin(), incoming_.begin() + static_cast<std::ptrdiff_t>(headerSize) + length);

        std::vector<std::uint8_t> inflated;
        std::span<const std::uint8_t> payload{frame};
        if (compressionThreshold_ >= 0) {
            ByteReader compressed{frame};
            const std::int32_t uncompressed = compressed.readVarInt();
            const std::span<const std::uint8_t> rest = compressed.rest();
            if (compressed.failed()) {
                return false;
            }
            if (uncompressed == 0) {
                inflated.assign(rest.begin(), rest.end());
            } else {
                const std::expected<std::vector<std::uint8_t>, zlib::ZlibError> got =
                    zlib::decompress(rest, static_cast<std::size_t>(std::max(uncompressed, 0)));
                if (!got.has_value()) {
                    say("Bot " + std::to_string(options_.id) + ": " + got.error().message);
                    return false;
                }
                inflated = *got;
            }
            payload = inflated;
        }

        ByteReader body{payload};
        const std::int32_t packetId = body.readVarInt();
        if (body.failed()) {
            return false;
        }
        if (!handlePacket(packetId, body)) {
            return false;
        }
    }
}

void Bot::run() {
    std::expected<TcpClient, SocketError> opened = TcpClient::connect(options_.host, options_.port);
    if (!opened.has_value()) {
        say("Bot " + std::to_string(options_.id) + " could not connect: " + opened.error().message);
        return;
    }
    socket_ = std::move(*opened);
    say("Bot " + std::to_string(options_.id) + " connected to " + options_.host + ":" + std::to_string(options_.port));
    if (!sendHandshakeAndLogin()) {
        return;
    }

    std::array<std::uint8_t, 8192> chunk{};
    std::chrono::steady_clock::time_point nextTick = std::chrono::steady_clock::now();
    while (running_) {
        if (socket_.waitReadable(kTickMillis)) {
            const std::expected<std::size_t, SocketError> got = socket_.read(chunk);
            if (!got.has_value() || *got == 0) {
                say("Bot " + std::to_string(options_.id) + " disconnected.");
                return;
            }
            incoming_.insert(incoming_.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(*got));
            if (!drainFrames()) {
                return;
            }
        }
        // The walk runs on the clock rather than on how often the socket had
        // something, so a busy server and a quiet one move a bot at the same speed.
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (state_ == ProtocolState::Play && now >= nextTick) {
            nextTick = now + std::chrono::milliseconds{kTickMillis};
            if (options_.physicsEnabled) {
                tickPhysics();
            }
            tickInteract();
            tickPlace();
        }
    }
}

} // namespace basalt::swarm
