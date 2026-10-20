#include <basalt/swarm/Bytes.hpp>
#include <basalt/swarm/Dialogs.hpp>

#include <map>
#include <memory>
#include <utility>

namespace basalt::swarm::dialogs {
namespace {

enum class Kind : std::uint8_t {
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
    IntArray = 11,
    LongArray = 12
};

struct Tag {
    Kind kind = Kind::End;
    double number = 0.0;
    std::string text;
    std::vector<Tag> items;
    std::vector<std::string> names;
    std::vector<Tag> values;

    [[nodiscard]] const Tag* find(std::string_view name) const {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return &values[i];
            }
        }
        return nullptr;
    }
};

constexpr int kMaxDepth = 24;

Tag read(ByteReader& in, Kind kind, int depth);

std::string readName(ByteReader& in) {
    const auto length = static_cast<std::uint16_t>(in.readShort());
    std::string name;
    for (std::uint16_t i = 0; i < length && !in.failed(); ++i) {
        name.push_back(static_cast<char>(in.readByte()));
    }
    return name;
}

Tag read(ByteReader& in, const Kind kind, const int depth) {
    Tag tag;
    tag.kind = kind;
    if (depth > kMaxDepth) {
        static_cast<void>(in.readByte());
        return tag;
    }
    switch (kind) {
    case Kind::Byte:
        tag.number = static_cast<std::int8_t>(in.readByte());
        break;
    case Kind::Short:
        tag.number = in.readShort();
        break;
    case Kind::Int:
        tag.number = in.readInt();
        break;
    case Kind::Long:
        tag.number = static_cast<double>(in.readLong());
        break;
    case Kind::Float:
        tag.number = static_cast<double>(in.readFloat());
        break;
    case Kind::Double:
        tag.number = in.readDouble();
        break;
    case Kind::String:
        tag.text = readName(in);
        break;
    case Kind::ByteArray:
        in.skip(static_cast<std::size_t>(std::max(in.readInt(), 0)));
        break;
    case Kind::IntArray:
        in.skip(static_cast<std::size_t>(std::max(in.readInt(), 0)) * 4);
        break;
    case Kind::LongArray:
        in.skip(static_cast<std::size_t>(std::max(in.readInt(), 0)) * 8);
        break;
    case Kind::List: {
        const auto inner = static_cast<Kind>(in.readByte());
        const std::int32_t count = in.readInt();
        for (std::int32_t i = 0; i < count && !in.failed(); ++i) {
            tag.items.push_back(read(in, inner, depth + 1));
        }
        break;
    }
    case Kind::Compound:
        while (!in.failed()) {
            const auto next = static_cast<Kind>(in.readByte());
            if (next == Kind::End) {
                break;
            }
            tag.names.push_back(readName(in));
            tag.values.push_back(read(in, next, depth + 1));
        }
        break;
    case Kind::End:
        break;
    }
    return tag;
}

void writeName(ByteWriter& out, std::string_view name) {
    out.writeShort(static_cast<std::int16_t>(name.size()));
    out.writeBytes({reinterpret_cast<const std::uint8_t*>(name.data()), name.size()});
}

void writeMember(ByteWriter& out, const std::string& name, const Tag& value) {
    out.writeByte(static_cast<std::uint8_t>(value.kind));
    writeName(out, name);
    switch (value.kind) {
    case Kind::Byte:
        out.writeByte(static_cast<std::uint8_t>(static_cast<std::int8_t>(value.number)));
        break;
    case Kind::Short:
        out.writeShort(static_cast<std::int16_t>(value.number));
        break;
    case Kind::Int:
        out.writeInt(static_cast<std::int32_t>(value.number));
        break;
    case Kind::Long:
        out.writeLong(static_cast<std::int64_t>(value.number));
        break;
    case Kind::Float:
        out.writeFloat(static_cast<float>(value.number));
        break;
    case Kind::Double:
        out.writeDouble(value.number);
        break;
    case Kind::String:
        writeName(out, value.text);
        break;
    default:
        out.writeByte(0);
        break;
    }
}

[[nodiscard]] std::optional<Tag> initialOf(const Tag& input) {
    const Tag* type = input.find("type");
    if (type == nullptr) {
        return std::nullopt;
    }
    Tag value;
    if (type->text == "minecraft:text") {
        value.kind = Kind::String;
        if (const Tag* initial = input.find("initial"); initial != nullptr) {
            value.text = initial->text;
        }
        return value;
    }
    if (type->text == "minecraft:boolean") {
        value.kind = Kind::Byte;
        if (const Tag* initial = input.find("initial"); initial != nullptr) {
            value.number = initial->number;
        }
        return value;
    }
    if (type->text == "minecraft:number_range") {
        value.kind = Kind::Float;
        const Tag* initial = input.find("initial");
        const Tag* start = input.find("start");
        value.number = initial != nullptr ? initial->number : start != nullptr ? start->number : 0.0;
        return value;
    }
    if (type->text == "minecraft:single_option") {
        value.kind = Kind::String;
        if (const Tag* options = input.find("options"); options != nullptr) {
            for (const Tag& option : options->items) {
                const Tag* id = option.find("id");
                const Tag* initial = option.find("initial");
                if (id != nullptr && (value.text.empty() || (initial != nullptr && initial->number != 0.0))) {
                    value.text = id->text;
                }
            }
        }
        return value;
    }
    return std::nullopt;
}

} // namespace

std::optional<Click> answer(ByteReader& body, const std::int32_t button) {
    // A registry id travels as its number plus one; zero says the dialog follows
    // in full.
    if (body.readVarInt() != 0 || body.failed()) {
        return std::nullopt;
    }
    const auto rootKind = static_cast<Kind>(body.readByte());
    if (rootKind != Kind::Compound || body.failed()) {
        return std::nullopt;
    }
    const Tag dialog = read(body, Kind::Compound, 0);
    if (body.failed()) {
        return std::nullopt;
    }
    const Tag* actions = dialog.find("actions");
    if (actions == nullptr || button < 0 || static_cast<std::size_t>(button) >= actions->items.size()) {
        return std::nullopt;
    }
    const Tag& chosen = actions->items[static_cast<std::size_t>(button)];
    const Tag* action = chosen.find("action");
    const Tag* id = action == nullptr ? nullptr : action->find("id");
    if (id == nullptr) {
        return std::nullopt;
    }
    Click click;
    click.id = id->text;
    if (const Tag* label = chosen.find("label"); label != nullptr) {
        click.label = label->text;
    }
    ByteWriter out;
    out.writeByte(static_cast<std::uint8_t>(Kind::Compound));
    if (const Tag* additions = action->find("additions"); additions != nullptr) {
        for (std::size_t i = 0; i < additions->names.size(); ++i) {
            writeMember(out, additions->names[i], additions->values[i]);
        }
    }
    if (const Tag* inputs = dialog.find("inputs"); inputs != nullptr) {
        for (const Tag& input : inputs->items) {
            const Tag* key = input.find("key");
            const std::optional<Tag> value = initialOf(input);
            if (key != nullptr && value.has_value()) {
                writeMember(out, key->text, *value);
            }
        }
    }
    out.writeByte(0);
    click.payload.assign(out.bytes().begin(), out.bytes().end());
    return click;
}

} // namespace basalt::swarm::dialogs
