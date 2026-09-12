#include <basalt/swarm/bedrock/BedrockCrypto.hpp>

#include <algorithm>
#include <cstring>
#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <utility>

namespace basalt::swarm::bedrock {
namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::size_t kCoordinate = 48;
constexpr std::size_t kSignature = kCoordinate * 2;
constexpr std::size_t kDerBuffer = 256;
constexpr std::size_t kTrailer = 8;

[[nodiscard]] std::array<std::uint8_t, kCoordinate> sha384(std::string_view text) {
    std::array<std::uint8_t, kCoordinate> digest{};
    mbedtls_sha512(reinterpret_cast<const unsigned char*>(text.data()), text.size(), digest.data(), 1);
    return digest;
}

} // namespace

std::string base64Encode(const std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    std::size_t at = 0;
    for (; at + 3 <= bytes.size(); at += 3) {
        const std::uint32_t block = (static_cast<std::uint32_t>(bytes[at]) << 16U) |
                                    (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) | bytes[at + 2];
        out += kAlphabet[(block >> 18U) & 0x3fU];
        out += kAlphabet[(block >> 12U) & 0x3fU];
        out += kAlphabet[(block >> 6U) & 0x3fU];
        out += kAlphabet[block & 0x3fU];
    }
    const std::size_t left = bytes.size() - at;
    if (left == 1) {
        const std::uint32_t block = static_cast<std::uint32_t>(bytes[at]) << 16U;
        out += kAlphabet[(block >> 18U) & 0x3fU];
        out += kAlphabet[(block >> 12U) & 0x3fU];
        out += "==";
    } else if (left == 2) {
        const std::uint32_t block =
            (static_cast<std::uint32_t>(bytes[at]) << 16U) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8U);
        out += kAlphabet[(block >> 18U) & 0x3fU];
        out += kAlphabet[(block >> 12U) & 0x3fU];
        out += kAlphabet[(block >> 6U) & 0x3fU];
        out += '=';
    }
    return out;
}

std::string base64UrlEncode(const std::span<const std::uint8_t> bytes) {
    std::string text = base64Encode(bytes);
    while (!text.empty() && text.back() == '=') {
        text.pop_back();
    }
    for (char& character : text) {
        if (character == '+') {
            character = '-';
        } else if (character == '/') {
            character = '_';
        }
    }
    return text;
}

std::optional<std::vector<std::uint8_t>> base64Decode(const std::string_view text) {
    std::vector<std::uint8_t> out;
    out.reserve((text.size() / 4) * 3);
    std::uint32_t block = 0;
    int digits = 0;
    for (const char raw : text) {
        if (raw == '=' || raw == '\n' || raw == '\r') {
            continue;
        }
        const char character = raw == '-' ? '+' : (raw == '_' ? '/' : raw);
        const std::size_t value = kAlphabet.find(character);
        if (value == std::string_view::npos) {
            return std::nullopt;
        }
        block = (block << 6U) | static_cast<std::uint32_t>(value);
        if (++digits == 4) {
            out.push_back(static_cast<std::uint8_t>(block >> 16U));
            out.push_back(static_cast<std::uint8_t>(block >> 8U));
            out.push_back(static_cast<std::uint8_t>(block));
            block = 0;
            digits = 0;
        }
    }
    if (digits == 2) {
        out.push_back(static_cast<std::uint8_t>(block >> 4U));
    } else if (digits == 3) {
        out.push_back(static_cast<std::uint8_t>(block >> 10U));
        out.push_back(static_cast<std::uint8_t>(block >> 2U));
    } else if (digits != 0) {
        return std::nullopt;
    }
    return out;
}

std::optional<std::string> jsonString(const std::string_view json, const std::string_view key) {
    const std::string needle = "\"" + std::string{key} + "\"";
    const std::size_t at = json.find(needle);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t cursor = json.find(':', at + needle.size());
    if (cursor == std::string_view::npos) {
        return std::nullopt;
    }
    ++cursor;
    while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t')) {
        ++cursor;
    }
    if (cursor >= json.size() || json[cursor] != '"') {
        return std::nullopt;
    }
    ++cursor;

    std::string value;
    while (cursor < json.size() && json[cursor] != '"') {
        if (json[cursor] == '\\' && cursor + 1 < json.size()) {
            ++cursor;
        }
        value += json[cursor++];
    }
    return value;
}

std::optional<JwtParts> decodeJwt(const std::string_view token) {
    const std::size_t first = token.find('.');
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second = token.find('.', first + 1);
    if (second == std::string_view::npos) {
        return std::nullopt;
    }

    const auto header = base64Decode(token.substr(0, first));
    const auto payload = base64Decode(token.substr(first + 1, second - first - 1));
    const auto signature = base64Decode(token.substr(second + 1));
    if (!header.has_value() || !payload.has_value() || !signature.has_value()) {
        return std::nullopt;
    }

    JwtParts parts;
    parts.header.assign(header->begin(), header->end());
    parts.payload.assign(payload->begin(), payload->end());
    parts.signature = *signature;
    parts.signingInput = std::string{token.substr(0, second)};
    return parts;
}

bool verifyJwt(const JwtParts& parts, const std::span<const std::uint8_t> publicKeyDer) {
    if (parts.signature.size() != kSignature || publicKeyDer.empty()) {
        return false;
    }

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    bool verified = false;

    if (mbedtls_pk_parse_public_key(&key, publicKeyDer.data(), publicKeyDer.size()) == 0 &&
        mbedtls_pk_get_type(&key) == MBEDTLS_PK_ECKEY) {
        mbedtls_ecp_group group;
        mbedtls_ecp_point point;
        mbedtls_mpi secret;
        mbedtls_mpi left;
        mbedtls_mpi right;
        mbedtls_ecp_group_init(&group);
        mbedtls_ecp_point_init(&point);
        mbedtls_mpi_init(&secret);
        mbedtls_mpi_init(&left);
        mbedtls_mpi_init(&right);

        if (mbedtls_ecp_export(mbedtls_pk_ec(key), &group, &secret, &point) == 0) {
            const auto digest = sha384(parts.signingInput);
            if (mbedtls_mpi_read_binary(&left, parts.signature.data(), kCoordinate) == 0 &&
                mbedtls_mpi_read_binary(&right, parts.signature.data() + kCoordinate, kCoordinate) == 0) {
                verified = mbedtls_ecdsa_verify(&group, digest.data(), digest.size(), &point, &left, &right) == 0;
            }
        }

        mbedtls_mpi_free(&right);
        mbedtls_mpi_free(&left);
        mbedtls_mpi_free(&secret);
        mbedtls_ecp_point_free(&point);
        mbedtls_ecp_group_free(&group);
    }

    mbedtls_pk_free(&key);
    return verified;
}

struct KeyPair::State {
    mbedtls_pk_context key{};
    mbedtls_entropy_context entropy{};
    mbedtls_ctr_drbg_context random{};
    std::vector<std::uint8_t> publicDer;

    State() {
        mbedtls_pk_init(&key);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&random);
    }

    ~State() {
        mbedtls_ctr_drbg_free(&random);
        mbedtls_entropy_free(&entropy);
        mbedtls_pk_free(&key);
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;
};

KeyPair::KeyPair() : state_(std::make_unique<State>()) {}

KeyPair::KeyPair(KeyPair&&) noexcept = default;

KeyPair& KeyPair::operator=(KeyPair&&) noexcept = default;

KeyPair::~KeyPair() = default;

std::optional<KeyPair> KeyPair::generate() {
    KeyPair pair;
    State& state = *pair.state_;

    const char* personalisation = "basalt-swarm-bedrock";
    if (mbedtls_ctr_drbg_seed(&state.random, mbedtls_entropy_func, &state.entropy,
                              reinterpret_cast<const unsigned char*>(personalisation),
                              std::strlen(personalisation)) != 0) {
        return std::nullopt;
    }
    if (mbedtls_pk_setup(&state.key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) {
        return std::nullopt;
    }
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP384R1, mbedtls_pk_ec(state.key), mbedtls_ctr_drbg_random,
                            &state.random) != 0) {
        return std::nullopt;
    }

    std::array<std::uint8_t, kDerBuffer> buffer{};
    const int written = mbedtls_pk_write_pubkey_der(&state.key, buffer.data(), buffer.size());
    if (written <= 0) {
        return std::nullopt;
    }
    // mbedTLS writes DER backwards, so the bytes end at the tail of the buffer.
    const auto* start = buffer.data() + buffer.size() - static_cast<std::size_t>(written);
    state.publicDer.assign(start, start + written);
    return pair;
}

std::span<const std::uint8_t> KeyPair::publicKeyDer() const noexcept { return state_->publicDer; }

std::string KeyPair::publicKeyBase64() const { return base64Encode(state_->publicDer); }

std::optional<std::vector<std::uint8_t>> KeyPair::sharedSecret(const std::span<const std::uint8_t> peerKeyDer) const {
    if (peerKeyDer.empty()) {
        return std::nullopt;
    }

    mbedtls_pk_context peer;
    mbedtls_pk_init(&peer);
    std::optional<std::vector<std::uint8_t>> result;

    if (mbedtls_pk_parse_public_key(&peer, peerKeyDer.data(), peerKeyDer.size()) == 0 &&
        mbedtls_pk_get_type(&peer) == MBEDTLS_PK_ECKEY) {
        mbedtls_ecp_group group;
        mbedtls_ecp_group peerGroup;
        mbedtls_ecp_point ours;
        mbedtls_ecp_point theirs;
        mbedtls_mpi ourSecret;
        mbedtls_mpi theirSecret;
        mbedtls_mpi shared;
        mbedtls_ecp_group_init(&group);
        mbedtls_ecp_group_init(&peerGroup);
        mbedtls_ecp_point_init(&ours);
        mbedtls_ecp_point_init(&theirs);
        mbedtls_mpi_init(&ourSecret);
        mbedtls_mpi_init(&theirSecret);
        mbedtls_mpi_init(&shared);

        if (mbedtls_ecp_export(mbedtls_pk_ec(state_->key), &group, &ourSecret, &ours) == 0 &&
            mbedtls_ecp_export(mbedtls_pk_ec(peer), &peerGroup, &theirSecret, &theirs) == 0 &&
            mbedtls_ecdh_compute_shared(&group, &shared, &theirs, &ourSecret, mbedtls_ctr_drbg_random,
                                        &state_->random) == 0) {
            std::vector<std::uint8_t> bytes(kCoordinate);
            if (mbedtls_mpi_write_binary(&shared, bytes.data(), bytes.size()) == 0) {
                result = std::move(bytes);
            }
        }

        mbedtls_mpi_free(&shared);
        mbedtls_mpi_free(&theirSecret);
        mbedtls_mpi_free(&ourSecret);
        mbedtls_ecp_point_free(&theirs);
        mbedtls_ecp_point_free(&ours);
        mbedtls_ecp_group_free(&peerGroup);
        mbedtls_ecp_group_free(&group);
    }

    mbedtls_pk_free(&peer);
    return result;
}

std::optional<std::string> KeyPair::signJwt(const std::string_view payloadJson) const {
    const std::string header = R"({"alg":"ES384","x5u":")" + publicKeyBase64() + R"("})";
    const std::string signingInput =
        base64UrlEncode({reinterpret_cast<const std::uint8_t*>(header.data()), header.size()}) + '.' +
        base64UrlEncode({reinterpret_cast<const std::uint8_t*>(payloadJson.data()), payloadJson.size()});

    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi secret;
    mbedtls_mpi left;
    mbedtls_mpi right;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&secret);
    mbedtls_mpi_init(&left);
    mbedtls_mpi_init(&right);

    std::optional<std::string> token;
    const auto digest = sha384(signingInput);
    if (mbedtls_ecp_export(mbedtls_pk_ec(state_->key), &group, &secret, &point) == 0 &&
        mbedtls_ecdsa_sign(&group, &left, &right, &secret, digest.data(), digest.size(), mbedtls_ctr_drbg_random,
                           &state_->random) == 0) {
        std::vector<std::uint8_t> signature(kSignature);
        if (mbedtls_mpi_write_binary(&left, signature.data(), kCoordinate) == 0 &&
            mbedtls_mpi_write_binary(&right, signature.data() + kCoordinate, kCoordinate) == 0) {
            token = signingInput + '.' + base64UrlEncode(signature);
        }
    }

    mbedtls_mpi_free(&right);
    mbedtls_mpi_free(&left);
    mbedtls_mpi_free(&secret);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return token;
}

SessionKey deriveSessionKey(const std::span<const std::uint8_t> salt, const std::span<const std::uint8_t> secret) {
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    mbedtls_sha256_starts(&context, 0);
    if (!salt.empty()) {
        mbedtls_sha256_update(&context, salt.data(), salt.size());
    }
    if (!secret.empty()) {
        mbedtls_sha256_update(&context, secret.data(), secret.size());
    }
    SessionKey key{};
    mbedtls_sha256_finish(&context, key.data());
    mbedtls_sha256_free(&context);
    return key;
}

struct Cipher::State {
    SessionKey key{};
    mbedtls_aes_context sending{};
    mbedtls_aes_context receiving{};
    std::array<std::uint8_t, 16> sendingCounter{};
    std::array<std::uint8_t, 16> receivingCounter{};
    std::array<std::uint8_t, 16> sendingStream{};
    std::array<std::uint8_t, 16> receivingStream{};
    std::size_t sendingOffset = 0;
    std::size_t receivingOffset = 0;
    std::uint64_t sent = 0;
    std::uint64_t received = 0;
    std::vector<std::uint8_t> scratch;
};

namespace {

[[nodiscard]] std::array<std::uint8_t, kTrailer> checksum(std::uint64_t counter, std::span<const std::uint8_t> payload,
                                                          const SessionKey& key) {
    std::array<std::uint8_t, 8> counterBytes{};
    for (std::size_t index = 0; index < 8; ++index) {
        counterBytes[index] = static_cast<std::uint8_t>((counter >> (index * 8)) & 0xffU);
    }

    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    mbedtls_sha256_starts(&context, 0);
    mbedtls_sha256_update(&context, counterBytes.data(), counterBytes.size());
    if (!payload.empty()) {
        mbedtls_sha256_update(&context, payload.data(), payload.size());
    }
    mbedtls_sha256_update(&context, key.data(), key.size());

    std::array<std::uint8_t, 32> digest{};
    mbedtls_sha256_finish(&context, digest.data());
    mbedtls_sha256_free(&context);

    std::array<std::uint8_t, kTrailer> out{};
    std::memcpy(out.data(), digest.data(), kTrailer);
    return out;
}

} // namespace

Cipher::Cipher(const SessionKey& key) : state_(std::make_unique<State>()) {
    state_->key = key;
    mbedtls_aes_init(&state_->sending);
    mbedtls_aes_init(&state_->receiving);
    mbedtls_aes_setkey_enc(&state_->sending, key.data(), 256);
    mbedtls_aes_setkey_enc(&state_->receiving, key.data(), 256);
    // The counter is the first twelve bytes of the key and then a two.
    std::memcpy(state_->sendingCounter.data(), key.data(), 12);
    state_->sendingCounter[15] = 2;
    state_->receivingCounter = state_->sendingCounter;
}

Cipher::~Cipher() {
    mbedtls_aes_free(&state_->sending);
    mbedtls_aes_free(&state_->receiving);
}

void Cipher::encrypt(const std::span<const std::uint8_t> payload, std::vector<std::uint8_t>& into) {
    const auto trailer = checksum(state_->sent++, payload, state_->key);
    state_->scratch.assign(payload.begin(), payload.end());
    state_->scratch.insert(state_->scratch.end(), trailer.begin(), trailer.end());

    into.resize(state_->scratch.size());
    mbedtls_aes_crypt_ctr(&state_->sending, state_->scratch.size(), &state_->sendingOffset,
                          state_->sendingCounter.data(), state_->sendingStream.data(), state_->scratch.data(),
                          into.data());
}

bool Cipher::decrypt(const std::span<const std::uint8_t> payload, std::vector<std::uint8_t>& into) {
    if (payload.size() < kTrailer) {
        return false;
    }
    into.resize(payload.size());
    mbedtls_aes_crypt_ctr(&state_->receiving, payload.size(), &state_->receivingOffset, state_->receivingCounter.data(),
                          state_->receivingStream.data(), payload.data(), into.data());

    const std::size_t body = into.size() - kTrailer;
    const auto expected = checksum(state_->received++, std::span{into}.first(body), state_->key);
    if (std::memcmp(expected.data(), into.data() + body, kTrailer) != 0) {
        return false;
    }
    into.resize(body);
    return true;
}

} // namespace basalt::swarm::bedrock
