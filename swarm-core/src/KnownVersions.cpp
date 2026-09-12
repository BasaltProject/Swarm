#include <basalt/swarm/KnownVersions.hpp>

#include <charconv>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace basalt::swarm::knownVersions {
namespace {

[[nodiscard]] std::string_view valueFor(const std::span<const Entry> table, const std::int32_t protocol) {
    for (const Entry& entry : table) {
        if (entry.protocol == protocol) {
            return entry.value;
        }
    }
    return {};
}

/// Splits on spaces without copying the words that are not kept.
template <typename Yield>
void forEachWord(const std::string_view text, Yield&& yield) {
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t space = text.find(' ', at);
        const std::size_t end = space == std::string_view::npos ? text.size() : space;
        if (end > at) {
            yield(text.substr(at, end - at));
        }
        at = end + 1;
    }
}

/// `name:number`, split at the *last* colon: every name here starts with `minecraft:`.
[[nodiscard]] std::map<std::string, std::int32_t, std::less<>> pairsOf(const std::string_view text) {
    std::map<std::string, std::int32_t, std::less<>> parsed;
    forEachWord(text, [&parsed](const std::string_view word) {
        const std::size_t at = word.rfind(':');
        if (at == std::string_view::npos || at == 0) {
            return;
        }
        std::int32_t value = 0;
        const std::string_view number = word.substr(at + 1);
        if (std::from_chars(number.data(), number.data() + number.size(), value).ec == std::errc{}) {
            parsed.emplace(std::string{word.substr(0, at)}, value);
        }
    });
    return parsed;
}

[[nodiscard]] std::set<std::string, std::less<>> wordsOf(const std::string_view text) {
    std::set<std::string, std::less<>> parsed;
    forEachWord(text, [&parsed](const std::string_view word) { parsed.emplace(word); });
    return parsed;
}

/// One parsed table per protocol, made on first use and kept.
///
/// A swarm is many threads asking the same questions about the same one or two versions, so the
/// cache is shared and the lock is taken once per version rather than once per question.
template <typename Value, typename Parse>
[[nodiscard]] const Value& cached(std::map<std::int32_t, Value>& cache, std::mutex& lock, const std::int32_t protocol,
                                  Parse&& parse) {
    const std::lock_guard<std::mutex> guard(lock);
    const auto found = cache.find(protocol);
    if (found != cache.end()) {
        return found->second;
    }
    return cache.emplace(protocol, parse()).first->second;
}

std::mutex idsLock;
std::map<std::int32_t, std::map<std::string, std::int32_t, std::less<>>> idsCache;
std::mutex entriesLock;
std::map<std::int32_t, std::set<std::string, std::less<>>> entriesCache;
std::mutex keysLock;
std::map<std::int32_t, std::set<std::string, std::less<>>> keysCache;
std::mutex namesLock;
std::map<std::int32_t, std::set<std::string, std::less<>>> namesCache;
std::mutex sizesLock;
std::map<std::int32_t, std::map<std::string, std::int32_t, std::less<>>> sizesCache;

} // namespace

bool isChecked(const std::int32_t protocol) { return !valueFor(tables::registryNames(), protocol).empty(); }

std::int32_t packetId(const std::int32_t protocol, const std::string_view packet) {
    const auto& ids =
        cached(idsCache, idsLock, protocol, [protocol] { return pairsOf(valueFor(tables::packetIds(), protocol)); });
    const auto found = ids.find(packet);
    return found == ids.end() ? -1 : found->second;
}

bool knows(const std::int32_t protocol, const std::string_view registry) {
    const auto& names = cached(namesCache, namesLock, protocol,
                               [protocol] { return wordsOf(valueFor(tables::registryNames(), protocol)); });
    return names.empty() || names.contains(registry);
}

bool hasEntry(const std::int32_t protocol, const std::string_view registry, const std::string_view entry) {
    const auto& entries = cached(entriesCache, entriesLock, protocol,
                                 [protocol] { return wordsOf(valueFor(tables::entryNames(), protocol)); });
    if (entries.empty()) {
        return true;
    }
    const std::string prefix = std::string{registry} + "/";
    const auto first = entries.lower_bound(prefix);
    if (first == entries.end() || !first->starts_with(prefix)) {
        // Nothing is kept for that registry, so nothing can be said about its entries.
        return true;
    }
    return entries.contains(prefix + std::string{entry});
}

bool hasEntryKey(const std::int32_t protocol, const std::string_view registry, const std::string_view member) {
    const auto& keys =
        cached(keysCache, keysLock, protocol, [protocol] { return wordsOf(valueFor(tables::entryKeys(), protocol)); });
    if (keys.empty()) {
        return true;
    }
    const std::string prefix = std::string{registry} + "=";
    const auto first = keys.lower_bound(prefix);
    if (first == keys.end() || !first->starts_with(prefix)) {
        // Nothing is kept about that registry's shape, so nothing can be said about its members.
        return true;
    }
    return keys.contains(prefix + std::string{member});
}

std::int32_t size(const std::int32_t protocol, const std::string_view registry) {
    const auto& counts = cached(sizesCache, sizesLock, protocol,
                                [protocol] { return pairsOf(valueFor(tables::registrySizes(), protocol)); });
    const auto found = counts.find(registry);
    return found == counts.end() ? -1 : found->second;
}

} // namespace basalt::swarm::knownVersions
