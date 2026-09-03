#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <basalt/swarm/Protocols.hpp>

#include <set>

using basalt::swarm::ProtocolProfile;
namespace protocols = basalt::swarm::protocols;

/// Adding a version is adding an entry, so what is checked here is that an entry cannot be added
/// wrong: two versions sharing a number, or one arriving with no ids at all.
TEST_CASE("every version has its own number and a label") {
    std::set<std::int32_t> numbers;
    std::set<std::string> labels;
    for (const ProtocolProfile& profile : protocols::supported()) {
        CHECK(profile.protocol > 0);
        CHECK_FALSE(profile.label.empty());
        CHECK(numbers.insert(profile.protocol).second);
        CHECK(labels.insert(profile.label).second);
    }
    CHECK(numbers.size() == protocols::supported().size());
}

TEST_CASE("a version is found by its name and by its number") {
    const std::optional<ProtocolProfile> byName = protocols::find("1.21.4");
    REQUIRE(byName.has_value());
    CHECK(byName->protocol == 769);

    const std::optional<ProtocolProfile> byNumber = protocols::find("769");
    REQUIRE(byNumber.has_value());
    CHECK(byNumber->label == "1.21.4");

    CHECK_FALSE(protocols::find("not a version").has_value());
}

TEST_CASE("the newest is what a bot claims when nobody asked for anything") {
    CHECK(protocols::newest().protocol == 776);
    CHECK(protocols::newest().label == "26.2");
}

TEST_CASE("a number nobody has released is claimed anyway, with the newest ids behind it") {
    const ProtocolProfile guessed = protocols::pretend(9999);
    CHECK(guessed.protocol == 9999);
    CHECK(guessed.label == "unknown");
    CHECK(guessed.play.serverboundKeepAlive == protocols::newest().play.serverboundKeepAlive);
}

TEST_CASE("the oldest versions carry the flags that make them older") {
    const std::optional<ProtocolProfile> v1_8 = protocols::find("1.8");
    REQUIRE(v1_8.has_value());
    CHECK_FALSE(v1_8->positionIsAcknowledged);
    CHECK_FALSE(v1_8->settingsHaveMainHand);
    CHECK_FALSE(v1_8->hasConfigurationPhase);

    const std::optional<ProtocolProfile> v1_20_1 = protocols::find("1.20.1");
    REQUIRE(v1_20_1.has_value());
    CHECK_FALSE(v1_20_1->hasConfigurationPhase);

    const std::optional<ProtocolProfile> v1_20_2 = protocols::find("1.20.2");
    REQUIRE(v1_20_2.has_value());
    CHECK(v1_20_2->hasConfigurationPhase);
}
