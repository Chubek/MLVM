#pragma once
#include <mlvm/ir.hpp>
#include <map>
#include <memory>
#include <string_view>

namespace cephyr {
// C11 scalar/pointer type model shared by the semantic checker and the IR
// emitter. Integers are sized per target; the pointer is a distinct IR type.
// `ptr` is the MLVM spelling for all object pointers.
struct Type {
    enum class Kind : std::uint8_t { Void, Bool, Integer, Pointer, Array, Function };
    Kind kind = Kind::Void;
    unsigned width = 0;            // Integer: bits
    bool is_signed = false;         // Integer
    const Type* pointee = nullptr;  // Pointer
    unsigned count = 0;             // Array
    std::vector<const Type*> parameters; // Function
    unsigned size() const;          // bytes, 0 when incomplete
    unsigned align() const;
    bool is_integer() const { return kind == Kind::Integer || kind == Kind::Bool; }
    bool is_scalar() const { return is_integer() || kind == Kind::Pointer; }
    bool complete() const { return kind != Kind::Array || count != 0; }
    std::string_view name() const;  // MLVM spelling: i8/i16/i32/i64/ptr/void
    bool result_is(const Type& other) const {
        return other.kind == Kind::Function && pointee && other.pointee && *pointee == *other.pointee;
    }
    bool operator==(const Type& other) const;
    std::string spelling() const;   // diagnostics
};

// Interns scalar, pointer, and array types so pointer identity is comparable.
class TypeContext {
public:
    explicit TypeContext(mlvm::Context& context) : context_(&context) {}
    const Type* void_type();
    const Type* bool_type();
    const Type* integer(unsigned width, bool is_signed);
    const Type* pointer(const Type* pointee);
    const Type* array(const Type* element, unsigned count);
    const Type* function(const Type* result, std::vector<const Type*> parameters);
private:
    mlvm::Context* context_;
    std::map<std::string, std::unique_ptr<Type>> types_;
    const Type* make(Type type);
};
// Usual arithmetic conversions and integer promotions (C11 6.3.1.8).
const Type* promote(const Type* type);
const Type* usual_arithmetic(const Type* left, const Type* right);
// Well-known width-32 integer types shared by the conversion helpers.
const Type* integer_width_32(bool is_signed);
} // namespace cephyr