#include "types.hpp"

namespace cephyr {
unsigned Type::size() const {
    switch (kind) {
    case Kind::Void: return 1;               // sizeof(void) is a GNU extension; keep 1 internally
    case Kind::Bool: return 1;
    case Kind::Integer: return width / 8;
    case Kind::Pointer: return 8;
    case Kind::Array: return count * (pointee ? pointee->size() : 0);
    case Kind::Function: return 1;
    }
    return 0;
}
unsigned Type::align() const {
    switch (kind) {
    case Kind::Void: case Kind::Bool: return 1;
    case Kind::Integer: return width / 8;
    case Kind::Pointer: return 8;
    case Kind::Array: return pointee ? pointee->align() : 1;
    case Kind::Function: return 1;
    }
    return 1;
}
std::string_view Type::name() const {
    switch (kind) {
    case Kind::Void: return "void";
    case Kind::Bool: return "i8";
    case Kind::Pointer: return "ptr";
    case Kind::Integer:
        switch (width) {
        case 8: return "i8";
        case 16: return "i16";
        case 32: return "i32";
        case 64: return "i64";
        default: return "";
        }
    case Kind::Array: return pointee ? pointee->name() : "";
    case Kind::Function: return "ptr";
    }
    return "";
}
bool Type::operator==(const Type& other) const {
    if (kind != other.kind) return false;
    switch (kind) {
    case Kind::Integer: return width == other.width && is_signed == other.is_signed;
    case Kind::Pointer: return pointee == other.pointee;
    case Kind::Array: return pointee == other.pointee && count == other.count;
    case Kind::Function:
        return result_is(other) && parameters == other.parameters;
    default: return true;
    }
}
std::string Type::spelling() const {
    switch (kind) {
    case Kind::Void: return "void";
    case Kind::Bool: return "_Bool";
    case Kind::Integer:
        return std::string(is_signed ? "" : "unsigned ") + std::to_string(width) + "-bit integer";
    case Kind::Pointer: return std::string(pointee && pointee->kind != Kind::Void ? pointee->spelling() : "void") + " *";
    case Kind::Array: return std::string(pointee ? pointee->spelling() : "?") + "[" + std::to_string(count) + "]";
    case Kind::Function: return "function";
    }
    return "?";
}

namespace {
// Conversions need the target's 32-bit integer type before any TypeContext
// exists; MLVM's i32 is the fixed-width reference for both signednesses.
const Type kSigned32{Type{.kind = Type::Kind::Integer, .width = 32, .is_signed = true}};
const Type kUnsigned32{Type{.kind = Type::Kind::Integer, .width = 32, .is_signed = false}};
} // namespace
const Type* integer_width_32(bool is_signed) { return is_signed ? &kSigned32 : &kUnsigned32; }

const Type* TypeContext::make(Type type) {
    // Intern by structure so pointer identity implies type equality. Composite
    // keys use the interned pointee address, which is itself canonical.
    std::string key;
    switch (type.kind) {
    case Type::Kind::Void: key = "void"; break;
    case Type::Kind::Bool: key = "bool"; break;
    case Type::Kind::Integer: key = "i" + std::to_string(type.width) + (type.is_signed ? "s" : "u"); break;
    case Type::Kind::Pointer: key = "ptr:" + std::to_string(reinterpret_cast<std::uintptr_t>(type.pointee)); break;
    case Type::Kind::Array: key = "arr:" + std::to_string(reinterpret_cast<std::uintptr_t>(type.pointee)) + ":" + std::to_string(type.count); break;
    case Type::Kind::Function: {
        key = "fn:";
        for (const auto* parameter : type.parameters) key += std::to_string(reinterpret_cast<std::uintptr_t>(parameter)) + ",";
        key += "->" + std::to_string(reinterpret_cast<std::uintptr_t>(type.pointee));
        break;
    }
    }
    auto it = types_.find(key);
    if (it != types_.end()) return it->second.get();
    auto owned = std::make_unique<Type>(std::move(type));
    const Type* result = owned.get();
    types_.emplace(std::move(key), std::move(owned));
    return result;
}
const Type* TypeContext::void_type() { return make(Type{.kind = Type::Kind::Void}); }
const Type* TypeContext::bool_type() { return make(Type{.kind = Type::Kind::Bool}); }
const Type* TypeContext::integer(unsigned width, bool is_signed) {
    // Width 32 is the conversion reference type; share the canonical instance
    // so pointer identity agrees with promote()/usual_arithmetic() results.
    if (width == 32) return integer_width_32(is_signed);
    return make(Type{.kind = Type::Kind::Integer, .width = width, .is_signed = is_signed});
}
const Type* TypeContext::pointer(const Type* pointee) {
    return make(Type{.kind = Type::Kind::Pointer, .pointee = pointee});
}
const Type* TypeContext::array(const Type* element, unsigned count) {
    return make(Type{.kind = Type::Kind::Array, .pointee = element, .count = count});
}
const Type* TypeContext::function(const Type* result, std::vector<const Type*> parameters) {
    return make(Type{.kind = Type::Kind::Function, .pointee = result, .parameters = std::move(parameters)});
}

const Type* promote(const Type* type) {
    if (!type || !type->is_integer()) return type;
    if (type->kind == Type::Kind::Bool || type->width < 32) return integer_width_32(true);
    return type;
}
const Type* usual_arithmetic(const Type* left, const Type* right) {
    if (!left || !right) return left ? left : right;
    if (left->kind == Type::Kind::Pointer) return left;
    if (right->kind == Type::Kind::Pointer) return right;
    left = promote(left);
    right = promote(right);
    if (left->width == right->width) return left->is_signed ? right : left;
    const Type* wider = left->width > right->width ? left : right;
    const Type* narrower = left->width > right->width ? right : left;
    if (wider->is_signed && wider->width > narrower->width) return wider;
    return wider; // unsigned wins at equal-or-greater rank
}
} // namespace cephyr
