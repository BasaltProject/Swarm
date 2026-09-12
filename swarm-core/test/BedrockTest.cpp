#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <basalt/swarm/Zlib.hpp>
#include <basalt/swarm/bedrock/BedrockCrypto.hpp>
#include <basalt/swarm/bedrock/BedrockTypes.hpp>
#include <basalt/swarm/bedrock/RakClient.hpp>

#include <doctest.h>

#include <string>
#include <vector>

using namespace basalt::swarm;
using namespace basalt::swarm::bedrock;

TEST_CASE("what a server list line says, and what a line that is not one says") {
    const auto entry = Advertisement::parse("MCPE;Basalt;2168;1.26.40;3;20;12345;second;Survival;1;19132;19133;");
    REQUIRE(entry.has_value());
    CHECK(entry->motd == "Basalt");
    CHECK(entry->protocol == 2168);
    CHECK(entry->version == "1.26.40");
    CHECK(entry->online == 3);
    CHECK(entry->max == 20);
    CHECK(entry->port == 19132);

    CHECK_FALSE(Advertisement::parse("MCPE;Basalt;2168").has_value());
    CHECK_FALSE(Advertisement::parse("MCJE;Basalt;2168;1.26.40;0;20;1;;Survival;1;19132;19133;").has_value());
}

TEST_CASE("the little-endian numbers bedrock writes come back the same") {
    ByteWriter writer;
    writeVarUInt(writer, 300);
    writeVarInt(writer, -7);
    writeShortLE(writer, 513);
    writeIntLE(writer, -2);
    writeString(writer, "Basalt");

    ByteReader reader{writer.bytes()};
    CHECK(readVarUInt(reader) == 300);
    CHECK(readVarInt(reader) == -7);
    CHECK(readShortLE(reader) == 513);
    CHECK(reader.readByte() == 0xfe);
    CHECK(reader.readByte() == 0xff);
    CHECK(reader.readByte() == 0xff);
    CHECK(reader.readByte() == 0xff);
    CHECK(readString(reader) == "Basalt");
    CHECK_FALSE(reader.failed());
}

TEST_CASE("a raw deflate batch comes back as what went in") {
    std::vector<std::uint8_t> data(2000);
    for (std::size_t index = 0; index < data.size(); ++index) {
        data[index] = static_cast<std::uint8_t>(index % 7);
    }
    const auto compressed = zlib::compressRaw(data);
    REQUIRE(compressed.has_value());
    CHECK(compressed->size() < data.size());

    const auto back = zlib::decompressRaw(*compressed);
    REQUIRE(back.has_value());
    CHECK(*back == data);
}

TEST_CASE("a token the bot signs is one the bot can check, and a changed one "
          "is not") {
    const auto keys = KeyPair::generate();
    REQUIRE(keys.has_value());

    const auto token = keys->signJwt(R"({"salt":"abcd"})");
    REQUIRE(token.has_value());
    const auto parts = decodeJwt(*token);
    REQUIRE(parts.has_value());
    CHECK(jsonString(parts->header, "alg") == "ES384");
    CHECK(jsonString(parts->payload, "salt") == "abcd");
    CHECK(verifyJwt(*parts, keys->publicKeyDer()));

    JwtParts tampered = *parts;
    tampered.signingInput += "x";
    CHECK_FALSE(verifyJwt(tampered, keys->publicKeyDer()));
}

TEST_CASE("both sides of a key exchange land on the same cipher") {
    const auto ours = KeyPair::generate();
    const auto theirs = KeyPair::generate();
    REQUIRE(ours.has_value());
    REQUIRE(theirs.has_value());

    const auto secret = ours->sharedSecret(theirs->publicKeyDer());
    const auto other = theirs->sharedSecret(ours->publicKeyDer());
    REQUIRE(secret.has_value());
    REQUIRE(other.has_value());
    CHECK(*secret == *other);

    const std::vector<std::uint8_t> salt{9, 8, 7};
    Cipher sender{deriveSessionKey(salt, *secret)};
    Cipher receiver{deriveSessionKey(salt, *other)};

    const std::string text = "a batch";
    const std::span<const std::uint8_t> plain{reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
    std::vector<std::uint8_t> encrypted;
    sender.encrypt(plain, encrypted);
    CHECK(encrypted.size() == text.size() + 8);

    std::vector<std::uint8_t> decrypted;
    REQUIRE(receiver.decrypt(encrypted, decrypted));
    CHECK(std::string(decrypted.begin(), decrypted.end()) == text);

    // The counter moves with every packet, so the same text never encrypts the
    // same way twice.
    std::vector<std::uint8_t> again;
    sender.encrypt(plain, again);
    CHECK(again != encrypted);
    REQUIRE(receiver.decrypt(again, decrypted));
    CHECK(std::string(decrypted.begin(), decrypted.end()) == text);
}

TEST_CASE("base64 in both alphabets, and what is not base64 at all") {
    const std::vector<std::uint8_t> bytes{0xfb, 0xff, 0x00, 0x10};
    CHECK(base64Encode(bytes) == "+/8AEA==");
    CHECK(base64UrlEncode(bytes) == "-_8AEA");

    const auto back = base64Decode("-_8AEA");
    REQUIRE(back.has_value());
    CHECK(*back == bytes);
    CHECK_FALSE(base64Decode("not base64!").has_value());
}
