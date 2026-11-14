#pragma once

#include <basalt/swarm/bedrock/BedrockTypes.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>

namespace basalt::swarm::bedrock {

/// What a Bedrock client keeps across a dimension change, and the walk through one as the client does it: loading
/// screen, the server's acknowledgement, the columns around the arrival, then the done action. Whatever was shown
/// before a change and is still shown once it settles, without the server having shown it again, leaked through.
class TransferWatch {
public:
    using Send = std::function<void(Packet, std::span<const std::uint8_t>)>;
    using Line = std::function<void(const std::string&)>;

    TransferWatch(Send send, Line say, Line problem);

    void entity(std::uint64_t id, bool shown, std::int64_t now);
    void listed(const std::string& uuid, bool shown, std::int64_t now);
    void bossBar(std::int64_t id, bool shown, std::int64_t now);
    void objective(const std::string& name, bool shown, std::int64_t now);
    void effect(std::int32_t id, bool shown, std::int64_t now);
    void window(std::int32_t id, std::int64_t now);
    void weather(bool thunder, bool on, std::int64_t now);
    void inventory(std::uint32_t containerId, std::int32_t filled, std::int64_t now);

    void changed(std::int32_t dimension, float x, float z, std::uint64_t self, std::int64_t now);
    void acknowledged(std::int64_t now);
    void column(std::int32_t x, std::int32_t z, std::int32_t dimension, std::uint32_t subChunks);
    void tick(std::int64_t now);

    [[nodiscard]] std::int32_t completed() const noexcept { return completed_; }

    [[nodiscard]] bool settling() const noexcept { return pending_.has_value() || settleAt_ != 0; }

private:
    void settle();

    struct Pending {
        std::int32_t dimension = 0;
        std::int32_t columnX = 0;
        std::int32_t columnZ = 0;
        std::uint64_t self = 0;
        std::int64_t startedAt = 0;
        bool acknowledged = false;
        bool arrived = false;
    };

    Send send_;
    Line say_;
    Line problem_;
    std::map<std::uint64_t, std::int64_t> entities_;
    std::map<std::string, std::int64_t> listed_;
    std::map<std::int64_t, std::int64_t> bossBars_;
    std::map<std::string, std::int64_t> objectives_;
    std::map<std::int32_t, std::int64_t> effects_;
    std::map<std::uint32_t, std::int64_t> inventories_;
    std::map<std::uint32_t, std::int32_t> filled_;
    std::int32_t window_ = -1;
    std::int64_t windowAt_ = 0;
    bool rain_ = false;
    bool thunder_ = false;
    std::int64_t weatherAt_ = 0;
    std::optional<Pending> pending_;
    std::int64_t transferStarted_ = 0;
    std::int64_t settleAt_ = 0;
    std::int32_t completed_ = 0;
};

} // namespace basalt::swarm::bedrock
