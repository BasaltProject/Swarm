#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <basalt/swarm/Bytes.hpp>
#include <basalt/swarm/Checks.hpp>

#include <doctest.h>

#include <string>
#include <vector>

using basalt::swarm::ByteReader;
using basalt::swarm::ByteWriter;
using basalt::swarm::Checks;

namespace {

constexpr std::int32_t kProtocol1_21_10 = 773;
constexpr std::int32_t kProtocol1_21_11 = 774;
constexpr std::int32_t kProtocol26_1 = 775;

/// `minecraft:resource`, the parser that names a registry.
constexpr std::int32_t kResourceParser = 46;

/// One past the last item 26.1 has, which is what took a real client down.
constexpr std::int32_t kItemPast26_1 = 1507;

} // namespace

/// The checks against the four things that actually took a real client down.
///
/// Each of these is a packet a server really sent and a client really refused, written here so that
/// the next one is found by a bot and not by somebody reading a disconnect report.
TEST_CASE("a tag group naming a registry the version does not have is caught") {
    std::vector<std::string> complaints;
    Checks checks{kProtocol1_21_10, [&complaints](const std::string& what) { complaints.push_back(what); }};
    ByteWriter body;
    body.writeVarInt(1);
    body.writeVarString("minecraft:timeline");
    body.writeVarInt(0);

    ByteReader reader{body.bytes()};
    checks.tags(reader);
    CHECK(checks.problems() == 1);
    REQUIRE(complaints.size() == 1);
    CHECK(complaints[0].find("minecraft:timeline") != std::string::npos);
}

TEST_CASE("a command argument naming one is caught too") {
    std::vector<std::string> complaints;
    Checks checks{kProtocol1_21_11, [&complaints](const std::string& what) { complaints.push_back(what); }};
    ByteWriter body;
    body.writeVarInt(2);
    // The root, with one child.
    body.writeByte(0);
    body.writeVarInt(1);
    body.writeVarInt(1);
    // An argument node that names a registry 1.21.11 does not have.
    body.writeByte(2);
    body.writeVarInt(0);
    body.writeVarString("clock");
    body.writeVarInt(kResourceParser);
    body.writeVarString("minecraft:world_clock");
    body.writeVarInt(0);

    ByteReader reader{body.bytes()};
    checks.commands(reader);
    CHECK(checks.problems() == 1);
    REQUIRE(complaints.size() == 1);
    CHECK(complaints[0].find("minecraft:world_clock") != std::string::npos);
}

TEST_CASE("an item past the end of that version's list is caught") {
    std::vector<std::string> complaints;
    Checks checks{kProtocol26_1, [&complaints](const std::string& what) { complaints.push_back(what); }};
    ByteWriter body;
    body.writeVarInt(0);
    body.writeVarInt(1);
    body.writeVarInt(2);
    body.writeVarInt(6);
    body.writeVarInt(5);
    body.writeVarInt(kItemPast26_1);
    body.writeVarInt(1);
    body.writeVarInt(0);
    body.writeVarInt(0);

    ByteReader reader{body.bytes()};
    checks.updateRecipes(reader);
    CHECK(checks.problems() == 1);
    REQUIRE(complaints.size() == 1);
    CHECK(complaints[0].find(std::to_string(kItemPast26_1)) != std::string::npos);
}

TEST_CASE("and a registry that version never had is caught when it is sent one") {
    Checks checks{kProtocol1_21_10, [](const std::string&) {}};
    ByteWriter body;
    body.writeVarString("minecraft:world_clock");
    body.writeVarInt(0);

    ByteReader reader{body.bytes()};
    checks.registryData(reader);
    CHECK(checks.problems() == 1);
}

TEST_CASE("what a version does have raises nothing") {
    Checks checks{kProtocol1_21_10, [](const std::string&) {}};
    ByteWriter body;
    body.writeVarInt(1);
    body.writeVarString("minecraft:block");
    body.writeVarInt(1);
    body.writeVarString("minecraft:logs");
    body.writeVarInt(1);
    body.writeVarInt(1);

    ByteReader reader{body.bytes()};
    checks.tags(reader);
    CHECK(checks.problems() == 0);
}

TEST_CASE("a version this tool knows nothing about is never reported as broken") {
    // The point of the whole table: silence about a version is not evidence against a server.
    Checks checks{1, [](const std::string&) {}};
    ByteWriter body;
    body.writeVarInt(1);
    body.writeVarString("minecraft:anything_at_all");
    body.writeVarInt(0);

    ByteReader reader{body.bytes()};
    checks.tags(reader);
    CHECK(checks.problems() == 0);
    CHECK_FALSE(checks.checkable());
}

/// The fifth: an element whose *shape* the version does not have.
///
/// 26.1 gave every animal variant a second set of assets for its baby, and gave a timeline a clock.
/// A client older than that reads a registry element against the shape it was compiled with, so a
/// member it has never seen costs it the whole registry and the connection with it.
TEST_CASE("a registry element made of members the version never had is caught") {
    std::vector<std::string> complaints;
    Checks checks{kProtocol1_21_11, [&complaints](const std::string& what) { complaints.push_back(what); }};

    ByteWriter body;
    body.writeVarString("minecraft:wolf_variant");
    body.writeVarInt(1);
    body.writeVarString("minecraft:pale");
    body.writeByte(1);  // The element is there.
    body.writeByte(10); // A compound, which is what an element is.
    for (const std::string& member : {std::string{"assets"}, std::string{"baby_assets"}}) {
        body.writeByte(10); // Each member is a compound of its own.
        body.writeShort(static_cast<std::int16_t>(member.size()));
        body.writeBytes({reinterpret_cast<const std::uint8_t*>(member.data()), member.size()});
        body.writeByte(0); // Empty, which is enough to be walked past.
    }
    body.writeByte(0); // The end of the element.

    ByteReader reader{body.bytes()};
    checks.registryData(reader);
    REQUIRE(checks.problems() == 1);
    CHECK(complaints[0].find("baby_assets") != std::string::npos);
}

/// And the same element, sent to the version that invented it, is not a problem.
TEST_CASE("what a version does have is not complained about") {
    std::vector<std::string> complaints;
    Checks checks{kProtocol26_1, [&complaints](const std::string& what) { complaints.push_back(what); }};

    ByteWriter body;
    body.writeVarString("minecraft:wolf_variant");
    body.writeVarInt(1);
    body.writeVarString("minecraft:pale");
    body.writeByte(1);
    body.writeByte(10);
    const std::string member = "baby_assets";
    body.writeByte(10);
    body.writeShort(static_cast<std::int16_t>(member.size()));
    body.writeBytes({reinterpret_cast<const std::uint8_t*>(member.data()), member.size()});
    body.writeByte(0);
    body.writeByte(0);

    ByteReader reader{body.bytes()};
    checks.registryData(reader);
    CHECK(checks.problems() == 0);
}
