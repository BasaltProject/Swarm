#include <basalt/swarm/Bytes.hpp>
#include <basalt/swarm/bedrock/BedrockTransfer.hpp>

#include <cmath>
#include <utility>

namespace basalt::swarm::bedrock {
namespace {

constexpr std::int32_t kDimensionChangeDone = 14;
constexpr std::int32_t kLoadingScreenStart = 1;
constexpr std::int32_t kLoadingScreenEnd = 2;
constexpr std::int64_t kAcknowledgementWait = 5000;
constexpr std::int64_t kLoadingGiveUp = 20000;
constexpr std::int64_t kSettleMillis = 2500;

template <typename Key>
std::int32_t stale(const std::map<Key, std::int64_t>& shown, const std::int64_t since) {
    std::int32_t count = 0;
    for (const auto& [key, at] : shown) {
        if (at < since) {
            ++count;
        }
    }
    return count;
}

template <typename Key>
void mark(std::map<Key, std::int64_t>& shown, const Key& key, const bool on, const std::int64_t now) {
    if (on) {
        shown[key] = now;
    } else {
        shown.erase(key);
    }
}

void loadingScreen(const TransferWatch::Send& send, const std::int32_t phase) {
    ByteWriter body;
    writeVarInt(body, phase);
    body.writeByte(0);
    send(Packet::ServerboundLoadingScreen, body.bytes());
}

} // namespace

TransferWatch::TransferWatch(Send send, Line say, Line problem)
    : send_(std::move(send)), say_(std::move(say)), problem_(std::move(problem)) {}

void TransferWatch::entity(const std::uint64_t id, const bool shown, const std::int64_t now) {
    mark(entities_, id, shown, now);
}

void TransferWatch::listed(const std::string& uuid, const bool shown, const std::int64_t now) {
    mark(listed_, uuid, shown, now);
}

void TransferWatch::bossBar(const std::int64_t id, const bool shown, const std::int64_t now) {
    mark(bossBars_, id, shown, now);
}

void TransferWatch::objective(const std::string& name, const bool shown, const std::int64_t now) {
    mark(objectives_, name, shown, now);
}

void TransferWatch::effect(const std::int32_t id, const bool shown, const std::int64_t now) {
    mark(effects_, id, shown, now);
}

void TransferWatch::window(const std::int32_t id, const std::int64_t now) {
    window_ = id;
    windowAt_ = now;
}

void TransferWatch::weather(const bool thunder, const bool on, const std::int64_t now) {
    (thunder ? thunder_ : rain_) = on;
    if (on) {
        weatherAt_ = now;
    }
}

void TransferWatch::inventory(const std::uint32_t containerId, const std::int32_t filled, const std::int64_t now) {
    inventories_[containerId] = now;
    filled_[containerId] = filled;
}

void TransferWatch::changed(const std::int32_t dimension, const float x, const float z, const std::uint64_t self,
                            const std::int64_t now) {
    if (!pending_.has_value()) {
        loadingScreen(send_, kLoadingScreenStart);
        if (settleAt_ == 0) {
            transferStarted_ = now;
        }
    }
    Pending next;
    next.dimension = dimension;
    next.columnX = static_cast<std::int32_t>(std::floor(x)) >> 4;
    next.columnZ = static_cast<std::int32_t>(std::floor(z)) >> 4;
    next.self = self;
    next.startedAt = now;
    pending_ = next;
    settleAt_ = 0;
}

void TransferWatch::acknowledged(const std::int64_t now) {
    if (!pending_.has_value()) {
        problem_("the server acknowledged a dimension change this client is not in");
        return;
    }
    pending_->acknowledged = true;
    say_("the dimension change was acknowledged after " + std::to_string(now - pending_->startedAt) + " ms");
}

void TransferWatch::column(const std::int32_t x, const std::int32_t z, const std::int32_t dimension,
                           const std::uint32_t /*subChunks*/) {
    if (pending_.has_value() && dimension == pending_->dimension && x == pending_->columnX && z == pending_->columnZ) {
        pending_->arrived = true;
    }
}

void TransferWatch::tick(const std::int64_t now) {
    if (pending_.has_value()) {
        const bool waited = now - pending_->startedAt > kAcknowledgementWait;
        if (pending_->arrived && (pending_->acknowledged || waited)) {
            if (!pending_->acknowledged) {
                problem_("the server never acknowledged the change to dimension " +
                         std::to_string(pending_->dimension));
            }
            ByteWriter done;
            writeVarULong(done, pending_->self);
            writeVarInt(done, kDimensionChangeDone);
            for (int position = 0; position < 2; ++position) {
                writeVarInt(done, 0);
                writeVarUInt(done, 0);
                writeVarInt(done, 0);
            }
            writeVarInt(done, 0);
            send_(Packet::PlayerAction, done.bytes());
            loadingScreen(send_, kLoadingScreenEnd);
            say_("arrived in dimension " + std::to_string(pending_->dimension) + " after " +
                 std::to_string(now - pending_->startedAt) + " ms");
            ++completed_;
            pending_.reset();
            settleAt_ = now + kSettleMillis;
        } else if (now - pending_->startedAt > kLoadingGiveUp) {
            problem_("stuck loading dimension " + std::to_string(pending_->dimension) +
                     ": the column under the arrival never came");
            pending_.reset();
            settleAt_ = now + kSettleMillis;
        }
    }
    if (settleAt_ != 0 && now >= settleAt_) {
        settleAt_ = 0;
        settle();
    }
}

void TransferWatch::settle() {
    const std::int64_t since = transferStarted_;
    const auto leaked = [this](const std::int32_t count, const std::string& what) {
        if (count > 0) {
            problem_(std::to_string(count) + " " + what + " from before the move still shown");
        }
    };
    leaked(stale(entities_, since), "entity(ies)");
    leaked(stale(listed_, since), "player list line(s)");
    leaked(stale(bossBars_, since), "boss bar(s)");
    leaked(stale(objectives_, since), "scoreboard objective(s)");
    leaked(stale(effects_, since), "effect(s)");
    if (window_ >= 0 && windowAt_ < since) {
        problem_("window " + std::to_string(window_) + " from before the move is still open");
    }
    if ((rain_ || thunder_) && weatherAt_ < since) {
        problem_("the weather from before the move is still falling");
    }
    for (const auto& [container, at] : inventories_) {
        if (at < since && filled_[container] > 0) {
            problem_("window " + std::to_string(container) + " still holds " + std::to_string(filled_[container]) +
                     " stack(s) from before the move");
        }
    }
    say_("settled after the move: " + std::to_string(entities_.size()) + " entity(ies), " +
         std::to_string(listed_.size()) + " listed, " + std::to_string(bossBars_.size()) + " boss bar(s), " +
         std::to_string(objectives_.size()) + " objective(s), " + std::to_string(effects_.size()) +
         " effect(s), carried " + std::to_string(filled_[0]) + " stack(s)");
}

} // namespace basalt::swarm::bedrock
