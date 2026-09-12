#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace basalt::swarm::bedrock {

/// Base64, both alphabets: a Bedrock login carries keys in the standard one and
/// JWT parts in the URL-safe one.
[[nodiscard]] std::string base64Encode(std::span<const std::uint8_t> bytes);

[[nodiscard]] std::string base64UrlEncode(std::span<const std::uint8_t> bytes);

[[nodiscard]] std::optional<std::vector<std::uint8_t>> base64Decode(std::string_view text);

/// One string member of a flat JSON object, by name. A bot reads three fields
/// out of the tokens a server sends it, and carrying a JSON parser for that is
/// more than it needs.
[[nodiscard]] std::optional<std::string> jsonString(std::string_view json, std::string_view key);

/// The three parts of a compact JWS, decoded and not yet checked.
struct JwtParts {
    std::string header;
    std::string payload;
    std::vector<std::uint8_t> signature;
    std::string signingInput;
};

[[nodiscard]] std::optional<JwtParts> decodeJwt(std::string_view token);

/// Whether the token really was signed by the key it names, which is what a
/// client checks before it derives anything from it.
[[nodiscard]] bool verifyJwt(const JwtParts& parts, std::span<const std::uint8_t> publicKeyDer);

/// A key pair on secp384r1, the curve a Bedrock identity lives on.
class KeyPair {
public:
    KeyPair();
    KeyPair(const KeyPair&) = delete;
    KeyPair& operator=(const KeyPair&) = delete;
    KeyPair(KeyPair&&) noexcept;
    KeyPair& operator=(KeyPair&&) noexcept;
    ~KeyPair();

    [[nodiscard]] static std::optional<KeyPair> generate();

    [[nodiscard]] std::span<const std::uint8_t> publicKeyDer() const noexcept;

    [[nodiscard]] std::string publicKeyBase64() const;

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> sharedSecret(std::span<const std::uint8_t> peerKeyDer) const;

    [[nodiscard]] std::optional<std::string> signJwt(std::string_view payloadJson) const;

private:
    struct State;

    std::unique_ptr<State> state_;
};

using SessionKey = std::array<std::uint8_t, 32>;

[[nodiscard]] SessionKey deriveSessionKey(std::span<const std::uint8_t> salt, std::span<const std::uint8_t> secret);

/// The packet cipher: AES-256 in counter mode with the eight byte checksum
/// every batch carries.
class Cipher {
public:
    explicit Cipher(const SessionKey& key);
    Cipher(const Cipher&) = delete;
    Cipher& operator=(const Cipher&) = delete;
    ~Cipher();

    void encrypt(std::span<const std::uint8_t> payload, std::vector<std::uint8_t>& into);

    /// False when the checksum did not match, which for a bot is a problem worth
    /// reporting: it means one side counted a packet the other did not.
    [[nodiscard]] bool decrypt(std::span<const std::uint8_t> payload, std::vector<std::uint8_t>& into);

private:
    struct State;

    std::unique_ptr<State> state_;
};

} // namespace basalt::swarm::bedrock
