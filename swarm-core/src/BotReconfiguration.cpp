#include <basalt/swarm/Bot.hpp>

namespace basalt::swarm {

bool Bot::beginConfiguration(ByteReader& body) {
    const auto& profile = options_.profile;
    if (!profile.hasConfigurationPhase || profile.play.serverboundConfigurationAcknowledged < 0 ||
        body.failed() || body.remaining() != 0) {
        return false;
    }
    if (!sendEmpty(profile.play.serverboundConfigurationAcknowledged)) {
        return false;
    }
    state_ = ProtocolState::Configuration;
    entities_.clear();
    entitiesSeen_.clear();
    x_ = 0.0;
    y_ = 0.0;
    z_ = 0.0;
    yaw_ = 0.0F;
    pitch_ = 0.0F;
    yVelocity_ = 0.0;
    wandering_ = false;
    wanderTicks_ = 0;
    onGround_ = false;
    positionReceived_ = false;
    sequence_ = 0;
    return sendClientInformation();
}

} // namespace basalt::swarm
