#include <cephyr/frontend.hpp>
#include <AbstractSyntaxTree.h>
#include "../parse/ast.hpp"
#include "../parse/cparser/error.hpp"
#include "../parse/cparser/nodes.hpp"
#include "types.hpp"
#include <algorithm>
#include <climits>
#include <set>

namespace cephyr {
namespace {
using namespace cparser;
using Node = ASTNode;

bool absent(Node* node) { return !node || node->getKind() == NK_UNKNOWN; }
std::vector<Node*> elements(Node* node) {
    if (absent(node)) return {};
    if (node->getKind() == NK_LIST) return static_cast<SequenceASTNode*>(node)->getElements();
    return {node};
}
std::string name(Node* node) {
    return node && node->getKind() == NK_IDENT_NODE ? static_cast<IdentASTNode*>(node)->getValue() : "";
}
struct Binary { const char* operation; Node* left; Node* right; };
std::optional<Binary> binary(Node* node) {
    // cparser models each binary operator with a distinct node class.
#define BINARY(kind, type, operation) case kind: { auto* n = static_cast<type*>(node); return Binary{operation, n->getLhs(), n->getRhs()}; }
    switch (node->getKind()) {
    BINARY(NK_PLUS_EXPR, PlusExprASTNode, "arith.addi")
    BINARY(NK_MINUS_EXPR, MinusExprASTNode, "arith.subi")
    BINARY(NK_MULT_EXPR, MultExprASTNode, "arith.muli")
    BINARY(NK_TRUNC_DIV_EXPR, TruncDivExprASTNode, "arith.divsi")
    BINARY(NK_TRUNC_MOD_EXPR, TruncModExprASTNode, "arith.remsi")
    BINARY(NK_BIT_AND_EXPR, BitAndExprASTNode, "arith.and")
    BINARY(NK_BIT_IOR_EXPR, BitIorExprASTNode, "arith.or")
    BINARY(NK_BIT_XOR_EXPR, BitXorExprASTNode, "arith.xor")
    BINARY(NK_LSHIFT_EXPR, LShiftExprASTNode, "c.shl")
    BINARY(NK_RSHIFT_EXPR, RShiftExprASTNode, "c.shr")
    BINARY(NK_LT_EXPR, LtExprASTNode, "c.lt")
    BINARY(NK_LE_EXPR, LeExprASTNode, "c.le")
    BINARY(NK_GT_EXPR, GtExprASTNode, "c.gt")
    BINARY(NK_GE_EXPR, GeExprASTNode, "c.ge")
    BINARY(NK_EQ_EXPR, EqExprASTNode, "c.eq")
    BINARY(NK_NE_EXPR, NeExprASTNode, "c.ne")
    BINARY(NK_LOG_AND_EXPR, LogAndExprASTNode, "&&")
    BINARY(NK_LOG_OR_EXPR, LogOrExprASTNode, "||")
    default: return std::nullopt;
    }
#undef BINARY
}
// `c.shl`/`c.shr` share the `c.` prefix with the comparisons but are not relational.
bool is_relational(std::string_view op) {
    return op == "c.lt" || op == "c.le" || op == "c.gt" || op == "c.ge" ||
           op == "c.eq" || op == "c.ne";
}

// An lvalue is an address plus the C type of the object it designates. Locals
// are addressed by frame slot; globals by symbol; array elements by gep.
struct Value {
    mlvm::ValueId id = 0;
    const Type* type = nullptr;
    explicit operator bool() const { return id != 0; }
};
struct Object {
    enum class Kind : std::uint8_t { Local, Global };
    Kind kind = Kind::Local;
    unsigned slot = 0;
    std::string symbol;
    const Type* type = nullptr;
    bool constant = false;
};
struct Lvalue {
    mlvm::ValueId address = 0;
    Object object{};
    const Type* type = nullptr;
};

class Lowering {
    mlvm::Module module;
    mlvm::Region global_data; // append last to keep function/block references stable
    mlvm::Identifier file;
    TypeContext types;
    std::vector<std::map<std::string, const Type*>> typedefs;
    struct Signature {
        bool defined = false, internal = false, variadic = false;
        const Type* result = nullptr;
        std::vector<const Type*> parameters;
    };
    std::map<std::string, Signature> functions;
    std::set<std::string> defined_globals;
    std::set<std::string> initialized_globals;
    std::map<std::string, Object> file_scope;   // globals visible at file scope
    std::vector<std::map<std::string, Object>> scopes;
    std::map<std::string, mlvm::Identifier> interned;
    const Type* current_result = nullptr;
    unsigned slots = 0, loops = 0, nesting = 0, conditional = 0;
    unsigned strings = 0;

    [[noreturn]] void fail(Node* node, std::string message) {
        throw parser_detail::Error(std::move(message), node ? std::max(1, node->getLineNum()) : 1);
    }
    mlvm::SourceLocation where(Node* node) { return {file, static_cast<unsigned>(std::max(1, node->getLineNum())), 1}; }
    mlvm::Identifier intern(std::string_view spelling) {
        auto it = interned.find(std::string(spelling));
        if (it != interned.end()) return it->second;
        auto id = module.context->intern(spelling);
        interned.emplace(std::string(spelling), id);
        return id;
    }
    std::vector<mlvm::Identifier> type_list(const Type* type) {
        std::vector<mlvm::Identifier> list;
        if (type && type->kind != Type::Kind::Void) list.push_back(intern(type->name()));
        return list;
    }
    mlvm::Operation& emit(mlvm::Block& block, std::string op, std::vector<mlvm::ValueId> operands,
                          const Type* result, Node* node) {
        const auto results = type_list(result);
        return module.append(block, std::move(op), operands, results, node ? where(node) : mlvm::SourceLocation{});
    }
    Value constant(mlvm::Block& block, std::int64_t value, const Type* type, Node* node) {
        auto& op = emit(block, "arith.constant", {}, type, node);
        op.attributes["value"] = std::to_string(value);
        return {op.results[0], type};
    }
    const Type* int32() { return types.integer(32, true); }
    const Type* uint32() { return types.integer(32, false); }
    const Type* long_type() { return types.integer(64, true); }
    const Type* void_pointer() { return types.pointer(types.void_type()); }

    // Global initializers are hex-encoded byte strings for the MLVM object.
    std::string encode(std::uint64_t bits, unsigned width) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out;
        for (unsigned i = 0; i < width; ++i) {
            const auto byte = static_cast<unsigned>((bits >> (8 * i)) & 0xff);
            out += digits[byte >> 4];
            out += digits[byte & 15];
        }
        return out;
    }
    std::string encode_bytes(const std::string& bytes) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out;
        for (unsigned char byte : bytes) {
            out += digits[byte >> 4];
            out += digits[byte & 15];
        }
        return out;
    }
    void define_global(const std::string& symbol, const Type* type, const char* section, const std::string& init, Node* node) {
        if (global_data.blocks.empty()) module.add_block(global_data);
        auto& op = emit(global_data.blocks.front(), "c.global", {}, nullptr, node);
        op.attributes["sym"] = symbol;
        op.attributes["size"] = std::to_string(type->size());
        op.attributes["align"] = std::to_string(type->align());
        op.attributes["section"] = section;
        op.attributes["init"] = init;
        op.attributes["internal"] = "true";
    }

    // ---- types ----
    const Type* type_of(Node* node) {
        if (node && node->getKind() == NK_IDENT_NODE) {
            const auto spelling = name(node);
            for (auto it = typedefs.rbegin(); it != typedefs.rend(); ++it)
                if (it->contains(spelling)) return it->at(spelling);
            fail(node, "unknown type name '" + spelling + "'");
        }
        if (absent(node)) fail(node, "expected a type");
        switch (node->getKind()) {
        case NK_VOID_TYPE: return types.void_type();
        case NK_POINTER_TYPE:
            return types.pointer(type_of(static_cast<PointerTypeASTNode*>(node)->getBaseType()));
        case NK_INTEGRAL_TYPE: {
            auto* integer = static_cast<IntegralTypeASTNode*>(node);
            const unsigned width = integer->getAlignment() * 8;
            if (width == 0) return types.bool_type();
            if (width != 8 && width != 16 && width != 32 && width != 64)
                fail(node, "unsupported integer width");
            return types.integer(width, integer->getIsSigned());
        }
        case NK_ARRAY_TYPE: {
            auto* array = static_cast<ArrayTypeASTNode*>(node);
            if (array->getExpr() == NULL_AST_NODE) return types.pointer(type_of(array->getElementType()));
            auto size = unevaluated(array->getExpr());
            if (!size.valid || size.value < 0 || size.value > (1 << 20))
                fail(node, "array bound must be an integer constant in 0..1048576");
            return types.array(type_of(array->getElementType()), static_cast<unsigned>(size.value));
        }
        default: fail(node, "type is not yet supported by Cephyr lowering");
        }
    }
    static bool type_node(Node* node) {
        if (!node) return false;
        switch (node->getKind()) {
        case NK_VOID_TYPE: case NK_POINTER_TYPE: case NK_INTEGRAL_TYPE: case NK_ARRAY_TYPE: return true;
        default: return false;
        }
    }
    // Little-endian hex encoding of one scalar into a global initializer image.
    void put(std::string& bytes, unsigned offset, std::uint64_t bits, unsigned width) {
        static constexpr char digits[] = "0123456789abcdef";
        if (width > 8) fail(nullptr, "initializer wider than 64 bits is not yet supported");
        for (unsigned i = 0; i < width; ++i) {
            const auto byte = static_cast<unsigned>((bits >> (8 * i)) & 0xff);
            bytes[2 * (offset + i)] = digits[byte >> 4];
            bytes[2 * (offset + i) + 1] = digits[byte & 15];
        }
    }
    // Fills the image of an object with static storage. Unmentioned elements and
    // absent initializers stay zero, matching C's aggregate initialization.
    void initialize_object(Node* init, const Type* type, std::string& bytes, unsigned offset, Node* node) {
        if (absent(init)) return;
        if (type->kind == Type::Kind::Array) {
            const Type* element = type->pointee;
            const auto unit = element ? element->size() : 0;
            if (!unit) fail(node, "initialization of an array of incomplete type");
            if (init->getKind() == NK_STRING_CONST && element->is_integer()) {
                const std::string text = static_cast<StringConstASTNode*>(init)->getString();
                if (text.size() > type->count) fail(node, "string literal is longer than its array");
                for (unsigned i = 0; i < text.size(); ++i)
                    put(bytes, offset + i * unit, static_cast<unsigned char>(text[i]), unit);
                return;
            }
            const auto items = elements(init);
            if (items.size() > type->count) fail(node, "too many initializers for an array");
            for (unsigned i = 0; i < items.size(); ++i)
                initialize_object(items[i], element, bytes, offset + i * unit, node);
            return;
        }
        if (type->kind == Type::Kind::Pointer) {
            // A static pointer initializer must be a null pointer constant.
            if (!integer_constant_expression(init)) fail(init, "pointer initializer is not a constant expression");
            const auto value = unevaluated(init);
            if (!value.valid || value.value != 0) fail(init, "only a null static pointer initializer is supported");
            put(bytes, offset, 0, type->size());
            return;
        }
        if (!type->is_scalar()) fail(node, "aggregate initializer is not yet supported");
        if (!integer_constant_expression(init)) fail(init, "initializer is not an integer constant expression");
        const auto value = unevaluated(init);
        if (!value.valid) fail(init, "initializer is not an integer constant");
        put(bytes, offset, static_cast<std::uint64_t>(value.value), type->size());
    }
    std::string static_image(Node* init, const Type* type, Node* node) {
        std::string bytes(type->size() * 2, '0');
        initialize_object(init, type, bytes, 0, node);
        return bytes;
    }
    // Runtime counterpart for automatic aggregates.
    void initialize_local(Node* init, const Type* type, const Lvalue& target, mlvm::Block& block, Node* node) {
        if (absent(init) && type->kind != Type::Kind::Array) {
            store_lvalue(target, constant(block, 0, type, node), block, node);
            return;
        }
        if (type->kind == Type::Kind::Array) {
            const Type* element = type->pointee;
            const auto unit = element ? element->size() : 0;
            if (!unit) fail(node, "initialization of an array of incomplete type");
            if (!absent(init) && init->getKind() == NK_STRING_CONST && element->is_integer()) {
                const std::string text = static_cast<StringConstASTNode*>(init)->getString();
                if (text.size() > type->count) fail(node, "string literal is longer than its array");
                for (unsigned i = 0; i < type->count; ++i) {
                    auto& address = emit(block, "c.gep", {target.address, constant(block, i, long_type(), node).id},
                                        types.pointer(element), node);
                    address.attributes["scale"] = std::to_string(unit);
                    Lvalue slot{.address = address.results[0], .object = target.object, .type = element};
                    store_lvalue(slot, constant(block, i < text.size() ? static_cast<unsigned char>(text[i]) : 0,
                                                element, node), block, node);
                }
                return;
            }
            const auto items = elements(init);
            if (items.size() > type->count) fail(node, "too many initializers for an array");
            for (unsigned i = 0; i < type->count; ++i) {
                auto& address = emit(block, "c.gep", {target.address, constant(block, i, long_type(), node).id},
                                    types.pointer(element), node);
                address.attributes["scale"] = std::to_string(unit);
                Lvalue slot{.address = address.results[0], .object = target.object, .type = element};
                initialize_local(i < items.size() ? items[i] : nullptr, element, slot, block, node);
            }
            return;
        }
        store_lvalue(target, expression(init, block), block, node);
    }
    // sizeof accepts an object, a type name, or any other expression.
    const Type* sizeof_type(Node* operand) {
        if (!operand) fail(operand, "sizeof requires an operand");
        if (operand->getKind() == NK_STRING_CONST)
            return types.array(types.integer(8, true), static_cast<unsigned>(static_cast<StringConstASTNode*>(operand)->getString().size() + 1));
        // cparser wraps a parenthesized type name in a one-element sequence.
        if (operand->getKind() == NK_LIST) {
            const auto items = elements(operand);
            if (items.size() != 1) fail(operand, "sizeof requires a single type name");
            operand = items.front();
        }
        if (type_node(operand)) return type_of(operand);
        if (operand->getKind() == NK_IDENT_NODE) return lookup_object(operand).type;
        return expression_type(operand);
    }
    void check_object_flags(Node* node, unsigned allowed) {
        if (node->getFlags() & ~allowed)
            fail(node, "unsupported qualifier or storage class");
    }

    // ---- conversions ----
    Value convert(mlvm::Block& block, Value value, const Type* target, Node* node, const char* reason) {
        if (target && target->kind == Type::Kind::Void) return {};
        if (!value) fail(node, std::string("void expression used as a value in ") + reason);
        if (!target || target->kind == Type::Kind::Void) return value;
        if (*value.type == *target) return value;
        if (target->kind == Type::Kind::Bool) {
            auto& truth = emit(block, "c.ne", {value.id, constant(block, 0, value.type, node).id}, int32(), node);
            value = {truth.results[0], int32()};
        }
        if (value.type->kind == Type::Kind::Array) return value;  // already decayed by the caller
        // Pointer/integer conversions are explicit reinterpretations of the same
        // machine register; MLVM's `c.cast` carries them without truncation.
        auto& op = emit(block, "c.cast", {value.id}, target, node);
        op.attributes["from"] = std::string(value.type->name());
        op.attributes["to"] = std::string(target->name());
        op.attributes["signed"] = value.type->is_integer() && value.type->is_signed ? "true" : "false";
        return {op.results[0], target};
    }
    Value promote_value(mlvm::Block& block, Value value, Node* node) {
        if (!value.type || !value.type->is_integer() || value.type->width >= 32) return value;
        return convert(block, value, int32(), node, "integer promotion");
    }
    // Control conditions are i32 truth values: any scalar wider than int
    // compares against zero, and pointers compare against null. Sub-word
    // integers promote as usual; the backend tests the full register.
    Value truth_value(mlvm::Block& block, Value value, Node* node) {
        if (!value || !value.type) return value;
        if (value.type->kind == Type::Kind::Pointer) {
            auto zero = convert(block, constant(block, 0, int32(), node), value.type, node, "condition");
            return comparison(block, "c.ne", value, zero, node);
        }
        value = promote_value(block, value, node);
        if (!value.type || (value.type->is_integer() && value.type->width == 32)) return value;
        // NOTE: convert-to-Bool yields _Bool storage (i8); conditions need the
        // i32 truth value, so compare directly instead.
        return comparison(block, "c.ne", value, constant(block, 0, value.type, node), node);
    }

    // ---- objects and lvalues ----
    Object lookup_object(Node* node) {
        const auto spelling = name(node);
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (it->contains(spelling)) return it->at(spelling);
        auto global = file_scope.find(spelling);
        if (global != file_scope.end()) return global->second;
        fail(node, "undeclared object '" + spelling + "'");
    }
    mlvm::ValueId local_address(mlvm::Block& block, unsigned slot, const Type* type, Node* node) {
        auto& op = emit(block, "c.address_of", {}, types.pointer(type), node);
        op.attributes["kind"] = "local";
        op.attributes["slot"] = std::to_string(slot);
        // Explicit object size lets the backend reserve whole-array frames.
        const auto bytes = type->size() ? type->size() : 1U;
        op.attributes["bytes"] = std::to_string(bytes);
        return op.results[0];
    }
    mlvm::ValueId global_address(mlvm::Block& block, const std::string& symbol, const Type* type, Node* node) {
        auto& op = emit(block, "c.address_of", {}, types.pointer(type), node);
        op.attributes["kind"] = "global";
        op.attributes["sym"] = symbol;
        return op.results[0];
    }
    Lvalue lvalue(Node* node, mlvm::Block& block) {
        if (node && node->getKind() == NK_IDENT_NODE) {
            auto object = lookup_object(node);
            const auto address = object.kind == Object::Kind::Global
                ? global_address(block, object.symbol, object.type, node)
                : local_address(block, object.slot, object.type, node);
            return Lvalue{.address = address, .object = object, .type = object.type};
        }
        if (node && node->getKind() == NK_ARRAY_REF) {
            auto* reference = static_cast<ArrayRefASTNode*>(node);
            auto base = expression(reference->getExpr(), block);
            auto address = pointer_add(block, base, expression(reference->getIndex(), block), node);
            return Lvalue{.address = address.id, .type = base.type->pointee};
        }
        if (node && node->getKind() == NK_INDIRECT_REF) {
            auto pointer = expression(static_cast<IndirectRefASTNode*>(node)->getExpr(), block);
            if (pointer.type->kind != Type::Kind::Pointer) fail(node, "dereference requires a pointer");
            return Lvalue{.address = pointer.id, .type = pointer.type->pointee};
        }
        fail(node, "expression is not an assignable lvalue");
    }
    // Array-to-pointer decay: an array object becomes a pointer to its element.
    Value decay(const Lvalue& target) { return {target.address, types.pointer(target.type->pointee)}; }
    // An array names its own storage; a pointer variable must be loaded first.
    Value pointer_base(const Lvalue& base, mlvm::Block& block, Node* node) {
        return base.type->kind == Type::Kind::Array ? decay(base) : load_value(base, block, node);
    }
    Lvalue subscript(const Lvalue& base, Node* index_node, mlvm::Block& block, Node* node) {
        if (base.type->kind != Type::Kind::Array && base.type->kind != Type::Kind::Pointer)
            fail(node, "subscripted value is not an array or pointer");
        const Type* element = base.type->pointee;
        if (!element || !element->size()) fail(node, "subscript of an incomplete type");
        auto pointer = pointer_base(base, block, node);
        auto index = expression(index_node, block);
        auto wide = convert(block, index, long_type(), node, "subscript");
        auto& op = emit(block, "c.gep", {pointer.id, wide.id}, pointer.type, node);
        op.attributes["scale"] = std::to_string(element->size());
        return Lvalue{.address = op.results[0], .object = base.object, .type = element};
    }
    Value load_value(const Lvalue& target, mlvm::Block& block, Node* node) {
        if (target.type->kind == Type::Kind::Array) return decay(target);
        if (!target.type->is_scalar()) fail(node, "expression is not a scalar value");
        auto& op = emit(block, "c.ptrload", {target.address}, target.type, node);
        op.attributes["type"] = std::string(target.type->name());
        return {op.results[0], target.type};
    }
    void store_lvalue(const Lvalue& target, Value value, mlvm::Block& block, Node* node) {
        if (!target.address) fail(node, "cannot assign to this expression");
        if (target.object.constant) fail(node, "assignment to a const-qualified object");
        if (!target.type->is_scalar()) fail(node, "assignment target must be scalar");
        value = convert(block, value, target.type, node, "assignment");
        emit(block, "c.ptrstore", {value.id, target.address}, nullptr, node);
    }

    // ---- expressions ----
    // Confirms that `constant_value` can evaluate `node`: reports a constant
    // division by zero or an out-of-range constant shift, and returns false for
    // anything that is not an integer constant expression.
    bool safe_constant(Node* node) {
        if (auto bin = binary(node)) {
            const std::string_view op = bin->operation;
            if (op == "&&" || op == "||") {
                // The right operand is not evaluated when the left one decides.
                if (!safe_constant(bin->left)) return false;
                if (integer_constant_expression(bin->left)) {
                    const auto left = unevaluated(bin->left);
                    if (left.valid && ((op == "&&" && left.value == 0) || (op == "||" && left.value != 0)))
                        return true;
                }
                return safe_constant(bin->right);
            }
            if (!safe_constant(bin->left) || !safe_constant(bin->right)) return false;
            if (op == "arith.divsi" || op == "arith.remsi") {
                if (integer_constant_expression(bin->right)) {
                    const auto divisor = unevaluated(bin->right);
                    if (divisor.valid && divisor.value == 0)
                        fail(bin->right, "division by zero in a constant expression");
                }
                return true; // a runtime divisor is not a constant-expression concern
            }
            if (op == "c.shl" || op == "c.shr") {
                if (integer_constant_expression(bin->right)) {
                    const auto amount = unevaluated(bin->right);
                    if (amount.valid && (amount.value < 0 || amount.value >= promoted_width(expression_type(bin->left))))
                        fail(bin->right, "shift amount is out of range for " + expression_type(bin->left)->spelling());
                }
                return true;
            }
            return true;
        }
        switch (node->getKind()) {
        case NK_INTEGER_CONST: case NK_CHAR_CONST: case NK_SIZEOF_EXPR: return true;
        case NK_BIT_NOT_EXPR: return safe_constant(static_cast<BitNotExprASTNode*>(node)->getExpr());
        case NK_LOG_NOT_EXPR: return safe_constant(static_cast<LogNotExprASTNode*>(node)->getExpr());
        case NK_CAST_EXPR: return safe_constant(static_cast<CastExprASTNode*>(node)->getExpr());
        case NK_COND_EXPR: {
            auto* cond = static_cast<CondExprASTNode*>(node);
            return integer_constant_expression(cond->getCondition()) &&
                   safe_constant(cond->getThenClause()) && safe_constant(cond->getElseClause());
        }
        case NK_GENERIC_SELECTION: return safe_constant(generic_selection(static_cast<GenericSelectionASTNode*>(node)));
        default: return false;
        }
    }
    // The lexer has selected the C11 candidate width and signedness.
    const Type* literal_type(IntegerConstASTNode* literal) {
        return types.integer(literal->getWidth(), !literal->isUnsigned());
    }
    // C11 6.3.1.1: operands narrower than int are promoted before shifting.
    static unsigned promoted_width(const Type* type) {
        if (!type || !type->is_integer()) return 32;
        return type->width < 32U ? 32U : type->width;
    }
    Value comparison(mlvm::Block& block, std::string_view op, Value left, Value right, Node* node) {
        const bool pointers = left.type->kind == Type::Kind::Pointer && right.type->kind == Type::Kind::Pointer;
        if (!pointers && (!left.type->is_integer() || !right.type->is_integer()))
            fail(node, "comparison requires integer operands");
        auto common = pointers ? left.type : usual_arithmetic(left.type, right.type);
        left = convert(block, left, common, node, "comparison");
        right = convert(block, right, common, node, "comparison");
        auto& result = emit(block, std::string(op), {left.id, right.id}, int32(), node);
        result.attributes["unsigned"] = pointers || !common->is_signed ? "true" : "false";
        return {result.results[0], int32()};
    }
    Value arithmetic(mlvm::Block& block, std::string_view op, Value left, Value right, Node* node) {
        if (!left.type->is_integer() || !right.type->is_integer())
            fail(node, "binary operator requires integer operands");
        auto common = usual_arithmetic(left.type, right.type);
        left = convert(block, left, common, node, "arithmetic");
        right = convert(block, right, common, node, "arithmetic");
        if (!common->is_signed && op == "arith.divsi") op = "arith.divui";
        if (!common->is_signed && op == "arith.remsi") op = "arith.remui";
        auto& result = emit(block, std::string(op), {left.id, right.id}, common, node);
        return {result.results[0], common};
    }
    Value pointer_add(mlvm::Block& block, Value pointer, Value index, Node* node) {
        if (pointer.type->kind != Type::Kind::Pointer) fail(node, "pointer arithmetic requires a pointer");
        if (!pointer.type->pointee || !pointer.type->pointee->size()) fail(node, "arithmetic on a pointer to an incomplete type");
        auto wide = convert(block, index, long_type(), node, "pointer offset");
        auto& op = emit(block, "c.gep", {pointer.id, wide.id}, pointer.type, node);
        op.attributes["scale"] = std::to_string(pointer.type->pointee->size());
        return {op.results[0], pointer.type};
    }
    Value select(mlvm::Block& block, Value condition, Node* yes, Node* no, Node* node,
                  bool logical, bool is_or, const Type* result_type = nullptr) {
        condition = truth_value(block, condition, node);
        auto result = result_type ? result_type : int32();
        auto& op = emit(block, result->kind == Type::Kind::Void ? "c.if" : "c.select", {condition.id}, result, node);
        op.regions.resize(2);
        for (unsigned branch = 0; branch < 2; ++branch) {
            auto& body = module.add_block(op.regions[branch]);
            ++conditional;
            Value value;
            if (logical && ((is_or && branch == 0) || (!is_or && branch == 1)))
                value = constant(body, is_or ? 1 : 0, result, node);
            else {
                value = expression(branch == 0 ? yes : no, body);
                if (logical) value = truth_value(body, value, node);
                else value = convert(body, value, result, node, "conditional");
            }
            emit(body, "c.yield", value ? std::vector<mlvm::ValueId>{value.id} : std::vector<mlvm::ValueId>{}, nullptr, node);
            --conditional;
        }
        return op.results.empty() ? Value{} : Value{op.results[0], result};
    }
    Value call_expression(CallExprASTNode* call, mlvm::Block& block) {
        const auto callee = name(call->getExpr());
        for (const auto& scope : scopes) if (scope.contains(callee)) fail(call, "called object '" + callee + "' is not a function");
        auto it = functions.find(callee);
        if (it == functions.end()) fail(call, "call to undeclared function '" + callee + "'");
        const auto signature = it->second;
        if (signature.variadic) fail(call, "variadic calls are not yet supported");
        auto args = elements(call->getArgs());
        if (args.size() != signature.parameters.size())
            fail(call, "argument count mismatch for '" + callee + "'");
        std::vector<mlvm::ValueId> operands;
        for (std::size_t i = 0; i < args.size(); ++i) {
            auto value = expression(args[i], block);
            value = convert(block, value, signature.parameters[i], args[i], "argument");
            operands.push_back(value.id);
        }
        auto& op = emit(block, "c.call", operands, signature.result, call);
        op.attributes["sym"] = callee;
        if (signature.result->kind == Type::Kind::Void) return {};
        return {op.results[0], signature.result};
    }

    // ---- static queries ----
    // Evaluates an integer constant expression directly on the AST so that
    // no IR operations, values, or use-def entries are allocated.
    std::int64_t constant_value(Node* node) {
        if (auto bin = binary(node)) {
            const std::string_view op = bin->operation;
            if (op == "&&" || op == "||") {
                const auto a = constant_value(bin->left);
                if (op == "&&" && !a) return 0;
                if (op == "||" && a) return 1;
                return constant_value(bin->right) != 0;
            }
            const bool shift = op == "c.shl" || op == "c.shr";
            const auto common = shift ? promote(expression_type(bin->left)) :
                usual_arithmetic(expression_type(bin->left), expression_type(bin->right));
            const auto a = truncate(constant_value(bin->left), common);
            const auto b = shift ? constant_value(bin->right) : truncate(constant_value(bin->right), common);
            const auto ua = static_cast<std::uint64_t>(a), ub = static_cast<std::uint64_t>(b);
            auto finish = [&](std::int64_t value, bool overflow = false) {
                const auto narrowed = truncate(value, common);
                if (common->is_signed && (overflow || narrowed != value)) fail(node, "overflow in a constant expression");
                return narrowed;
            };
            if (op == "arith.addi" || op == "arith.subi" || op == "arith.muli") {
                if (!common->is_signed) return finish(static_cast<std::int64_t>(
                    op == "arith.addi" ? ua + ub : op == "arith.subi" ? ua - ub : ua * ub));
                std::int64_t value = 0;
                const bool overflow = op == "arith.addi" ? __builtin_add_overflow(a, b, &value) :
                    op == "arith.subi" ? __builtin_sub_overflow(a, b, &value) : __builtin_mul_overflow(a, b, &value);
                return finish(value, overflow);
            }
            if (op == "arith.and") return finish(a & b);
            if (op == "arith.or") return finish(a | b);
            if (op == "arith.xor") return finish(a ^ b);
            if (op == "c.lt") return common->is_signed ? a < b : ua < ub;
            if (op == "c.le") return common->is_signed ? a <= b : ua <= ub;
            if (op == "c.gt") return common->is_signed ? a > b : ua > ub;
            if (op == "c.ge") return common->is_signed ? a >= b : ua >= ub;
            if (op == "c.eq") return a == b;
            if (op == "c.ne") return a != b;
            if (op == "c.shl" || op == "c.shr") {
                if (b < 0 || b >= common->width) fail(node, "shift amount is out of range");
                if (op == "c.shr") return common->is_signed ? (a >> b) : static_cast<std::int64_t>(ua >> b);
                const auto max = common->width == 64 ? INT64_MAX : (std::int64_t{1} << (common->width - 1)) - 1;
                return finish(static_cast<std::int64_t>(ua << b), common->is_signed && (a < 0 || a > (max >> b)));
            }
            if (b == 0) fail(node, "division by zero in a constant expression");
            if (!common->is_signed) return finish(static_cast<std::int64_t>(op == "arith.divsi" ? ua / ub : ua % ub));
            if (a == INT64_MIN && b == -1) fail(node, "signed overflow in a constant expression");
            return finish(op == "arith.divsi" ? a / b : a % b);
        }
        switch (node->getKind()) {
        case NK_INTEGER_CONST: return static_cast<IntegerConstASTNode*>(node)->getValue();
        case NK_CHAR_CONST: return static_cast<CharConstASTNode*>(node)->getValue();
        case NK_BIT_NOT_EXPR: return truncate(~constant_value(static_cast<BitNotExprASTNode*>(node)->getExpr()), expression_type(node));
        case NK_LOG_NOT_EXPR: return !constant_value(static_cast<LogNotExprASTNode*>(node)->getExpr());
        case NK_CAST_EXPR: {
            auto target = type_of(static_cast<CastExprASTNode*>(node)->getType());
            auto value = constant_value(static_cast<CastExprASTNode*>(node)->getExpr());
            if (!target->is_integer()) fail(node, "cast to a non-integer type in a constant expression");
            return truncate(value, target);
        }
        case NK_COND_EXPR: {
            auto* cond = static_cast<CondExprASTNode*>(node);
            return constant_value(constant_value(cond->getCondition()) ? cond->getThenClause() : cond->getElseClause());
        }
        case NK_SIZEOF_EXPR: return sizeof_type(static_cast<SizeOfExprASTNode*>(node)->getExpr())->size();
        case NK_GENERIC_SELECTION: return constant_value(generic_selection(static_cast<GenericSelectionASTNode*>(node)));
        default: fail(node, "expression is not an integer constant");
        }
    }
    static std::int64_t truncate(std::int64_t value, const Type* type) {
        if (type->kind == Type::Kind::Bool) return value != 0;
        const unsigned bits = type->width;
        if (bits >= 64) return value;
        const auto mask = (std::int64_t{1} << bits) - 1;
        value &= mask;
        if (type->is_signed && (value & (std::int64_t{1} << (bits - 1))))
            value -= (std::int64_t{1} << bits);
        return value;
    }
    struct Constant { bool valid = false; std::int64_t value = 0; };
    Constant unevaluated(Node* node) {
        if (!integer_constant_expression(node)) return {};
        return Constant{true, constant_value(node)};
    }
    bool integer_constant_expression(Node* node) {
        if (auto bin = binary(node)) {
            const std::string_view op = bin->operation;
            if ((op == "&&" || op == "||") && integer_constant_expression(bin->left)) {
                auto left = unevaluated(bin->left);
                if (left.valid && ((op == "&&" && !left.value) || (op == "||" && left.value)))
                    return integer_constant_expression(bin->right);
            } else if (op != "&&" && op != "||") {
                return integer_constant_expression(bin->left) && integer_constant_expression(bin->right);
            } else return false;
        }
        switch (node->getKind()) {
        case NK_INTEGER_CONST: case NK_CHAR_CONST: return true;
        case NK_BIT_NOT_EXPR: return integer_constant_expression(static_cast<BitNotExprASTNode*>(node)->getExpr());
        case NK_LOG_NOT_EXPR: return integer_constant_expression(static_cast<LogNotExprASTNode*>(node)->getExpr());
        case NK_CAST_EXPR: return type_of(static_cast<CastExprASTNode*>(node)->getType())->is_integer() &&
                                  integer_constant_expression(static_cast<CastExprASTNode*>(node)->getExpr());
        case NK_SIZEOF_EXPR: return true;
        case NK_COND_EXPR: {
            auto* cond = static_cast<CondExprASTNode*>(node);
            if (!integer_constant_expression(cond->getCondition())) return false;
            auto value = unevaluated(cond->getCondition());
            return value.valid && integer_constant_expression(value.value ? cond->getThenClause() : cond->getElseClause());
        }
        case NK_GENERIC_SELECTION: return integer_constant_expression(generic_selection(static_cast<GenericSelectionASTNode*>(node)));
        default: return false;
        }
    }
    void static_assertion(StaticAssertASTNode* node) {
        if (!integer_constant_expression(node->getExpr()))
            fail(node, "_Static_assert requires an integer constant expression");
        auto value = unevaluated(node->getExpr());
        if (!value.valid) fail(node, "_Static_assert requires an integer constant expression");
        if (!value.value) fail(node, "static assertion failed: " + node->getMessage());
    }
    const Type* expression_type(Node* node) {
        if (auto bin = binary(node)) {
            if (is_relational(bin->operation) || bin->operation == std::string_view("&&") ||
                bin->operation == std::string_view("||")) return int32();
            if (bin->operation == std::string_view("c.shl") || bin->operation == std::string_view("c.shr"))
                return promote(expression_type(bin->left));
            return usual_arithmetic(expression_type(bin->left), expression_type(bin->right));
        }
        switch (node->getKind()) {
        case NK_INTEGER_CONST: return literal_type(static_cast<IntegerConstASTNode*>(node));
        case NK_CHAR_CONST: return int32();
        case NK_STRING_CONST: return types.pointer(types.integer(8, false));
        case NK_IDENT_NODE: {
            auto object = lookup_object(node);
            return object.type->kind == Type::Kind::Array ? types.pointer(object.type->pointee) : object.type;
        }
        case NK_ARRAY_REF: {
            auto* reference = static_cast<ArrayRefASTNode*>(node);
            auto base = expression_type(reference->getExpr());
            if (base->kind != Type::Kind::Array && base->kind != Type::Kind::Pointer)
                fail(node, "subscripted value is not an array or pointer");
            return base->pointee;
        }
        case NK_INDIRECT_REF: {
            auto* indirect = static_cast<IndirectRefASTNode*>(node);
            auto base = expression_type(indirect->getExpr());
            if (base->kind != Type::Kind::Pointer) fail(node, "dereference requires a pointer");
            return base->pointee;
        }
        case NK_ADDR_EXPR: return types.pointer(expression_type(static_cast<AddrExprASTNode*>(node)->getExpr()));
        case NK_CAST_EXPR: return type_of(static_cast<CastExprASTNode*>(node)->getType());
        case NK_COND_EXPR: {
            auto* cond = static_cast<CondExprASTNode*>(node);
            return usual_arithmetic(expression_type(cond->getThenClause()), expression_type(cond->getElseClause()));
        }
        case NK_SIZEOF_EXPR: return long_type();
        case NK_CALL_EXPR: {
            auto* call = static_cast<CallExprASTNode*>(node);
            auto it = functions.find(name(call->getExpr()));
            if (it == functions.end()) fail(node, "call to undeclared function '" + name(call->getExpr()) + "'");
            return it->second.result;
        }
        case NK_BIT_NOT_EXPR: {
            auto operand = expression_type(static_cast<BitNotExprASTNode*>(node)->getExpr());
            return operand->is_integer() && operand->width < 32 ? int32() : operand;
        }
        case NK_PREINCREMENT_EXPR: case NK_PREDECREMENT_EXPR:
        case NK_POSTINCREMENT_EXPR: case NK_POSTDECREMENT_EXPR: {
            Node* operand = nullptr;
            switch (node->getKind()) {
            case NK_PREINCREMENT_EXPR: operand = static_cast<PreincrementExprASTNode*>(node)->getExpr(); break;
            case NK_PREDECREMENT_EXPR: operand = static_cast<PredecrementExprASTNode*>(node)->getExpr(); break;
            case NK_POSTINCREMENT_EXPR: operand = static_cast<PostincrementExprASTNode*>(node)->getExpr(); break;
            default: operand = static_cast<PostdecrementExprASTNode*>(node)->getExpr(); break;
            }
            return expression_type(operand);
        }
        case NK_ASSIGN_EXPR: return expression_type(static_cast<AssignExprASTNode*>(node)->getLhs());
        case NK_LIST: {
            auto items = elements(node);
            if (items.empty()) fail(node, "empty comma expression");
            return expression_type(items.back());
        }
        case NK_LOG_NOT_EXPR: return int32();
        case NK_GENERIC_SELECTION: return expression_type(generic_selection(static_cast<GenericSelectionASTNode*>(node)));
        default: fail(node, "cannot determine the type of this expression");
        }
    }
    Node* generic_selection(GenericSelectionASTNode* generic) {
        const auto control = expression_type(generic->getControl());
        Node *selected = nullptr, *fallback = nullptr;
        bool seen = false;
        for (auto* item : elements(generic->getAssociations())) {
            auto* association = static_cast<GenericAssociationASTNode*>(item);
            if (absent(association->getType())) {
                if (fallback) fail(generic, "duplicate default generic association");
                fallback = association->getExpr();
            } else {
                auto type = type_of(association->getType());
                const bool compatible = type->kind == control->kind &&
                    (!type->is_integer() || (type->width == control->width && type->is_signed == control->is_signed));
                if (!compatible) continue;
                if (seen) fail(generic, "duplicate compatible generic association");
                seen = true;
                selected = association->getExpr();
            }
            expression_type(association->getExpr());
        }
        if (!selected) selected = fallback;
        if (!selected) fail(generic, "no matching generic association");
        return selected;
    }

    Value expression(Node* node, mlvm::Block& block) {
        if (absent(node)) fail(node, "expected expression");
        struct Depth { unsigned& value; ~Depth() { --value; } } depth{nesting};
        if (++nesting > 128) fail(node, "expression nesting exceeds 128");
        if (auto bin = binary(node)) {
            const std::string_view op = bin->operation;
            // Fold integral constant expressions into a single materialization.
            if (op != "&&" && op != "||" && integer_constant_expression(node) && safe_constant(node)) {
                const auto folded = constant_value(node);
                const auto* folded_type = expression_type(node);
                if (folded_type && folded_type->is_integer() && folded != truncate(folded, folded_type))
                    fail(node, "overflow in a constant expression");
                return constant(block, folded, folded_type && folded_type->is_integer() ? folded_type : int32(), node);
            }
            if (op == "&&" || op == "||") {
                // A constant left operand decides the result, so the right
                // operand is never lowered and its traps are never diagnosed.
                if (integer_constant_expression(bin->left) && safe_constant(bin->left)) {
                    const auto decided = constant_value(bin->left);
                    if (op == "&&" && decided == 0) return constant(block, 0, int32(), node);
                    if (op == "||" && decided != 0) return constant(block, 1, int32(), node);
                }
                auto left = promote_value(block, expression(bin->left, block), node);
                const bool is_or = op == "||";
                // `a && b` evaluates b on the true branch; `a || b` on the false one.
                return select(block, left, is_or ? nullptr : bin->right, is_or ? bin->right : nullptr,
                              node, true, is_or);
            }
            auto left = expression(bin->left, block);
            auto right = expression(bin->right, block);
            const bool left_pointer = left.type->kind == Type::Kind::Pointer;
            const bool right_pointer = right.type->kind == Type::Kind::Pointer;
            if (is_relational(op)) {
                // A null pointer constant compares against any pointer.
                if (left_pointer && right.type->is_integer()) {
                    if (!integer_constant_expression(bin->right) || constant_value(bin->right) != 0)
                        fail(node, "comparison mixes a pointer and a nonconstant integer");
                    right = convert(block, right, left.type, node, "comparison");
                } else if (right_pointer && left.type->is_integer()) {
                    if (!integer_constant_expression(bin->left) || constant_value(bin->left) != 0)
                        fail(node, "comparison mixes a pointer and a nonconstant integer");
                    left = convert(block, left, right.type, node, "comparison");
                }
                return comparison(block, op, left, right, node);
            }
            if (left_pointer && right_pointer) {
                if (op != "arith.subi") fail(node, "pointer supports only subtraction");
                const auto scale = static_cast<std::int64_t>(left.type->pointee->size());
                if (!scale) fail(node, "pointer difference on an incomplete type");
                const auto left_address = convert(block, left, long_type(), node, "pointer difference");
                const auto right_address = convert(block, right, long_type(), node, "pointer difference");
                auto& difference = emit(block, "arith.subi", {left_address.id, right_address.id}, long_type(), node);
                auto& divided = emit(block, "arith.divsi",
                                    {difference.results[0], constant(block, scale, long_type(), node).id}, long_type(), node);
                return {divided.results[0], long_type()};
            }
            if (left_pointer) {
                if (op == "arith.subi") {
                    right = convert(block, right, long_type(), node, "pointer offset");
                    right = arithmetic(block, "arith.subi", constant(block, 0, long_type(), node), right, node);
                } else if (op != "arith.addi") fail(node, "pointer supports only addition/subtraction");
                return pointer_add(block, left, right, node);
            }
            if (right_pointer) {
                if (op != "arith.addi") fail(node, "pointer supports only addition");
                return pointer_add(block, right, left, node);
            }
            if (op == "c.shl" || op == "c.shr") {
                left = promote_value(block, left, node);
                // The shift amount converts to the promoted left type; the
                // verifier and backend require matching operand types.
                right = convert(block, promote_value(block, right, node), left.type, node, "shift amount");
                if (integer_constant_expression(bin->right)) {
                    const auto amount = unevaluated(bin->right);
                    if (amount.valid && (amount.value < 0 || amount.value >= promoted_width(left.type)))
                        fail(node, "shift amount is out of range for " + left.type->spelling());
                }
                auto& result = emit(block, std::string(op), {left.id, right.id}, left.type, node);
                result.attributes["unsigned"] = left.type->is_signed ? "false" : "true";
                return {result.results[0], left.type};
            }
            return arithmetic(block, op, left, right, node);
        }
        switch (node->getKind()) {
        case NK_INTEGER_CONST: {
            const auto value = static_cast<IntegerConstASTNode*>(node)->getValue();
            return constant(block, value, literal_type(static_cast<IntegerConstASTNode*>(node)), node);
        }
        case NK_CHAR_CONST: return constant(block, static_cast<CharConstASTNode*>(node)->getValue(), int32(), node);
        case NK_STRING_CONST: {
            auto* literal = static_cast<StringConstASTNode*>(node);
            std::string bytes = literal->getString();
            bytes.push_back('\0');
            const auto element = types.integer(8, false);
            const auto symbol = "_ZLstr" + std::to_string(strings++);
            define_global(symbol, types.array(element, static_cast<unsigned>(bytes.size())), "rodata",
                          encode_bytes(bytes), node);
            return {global_address(block, symbol, types.pointer(element), node), types.pointer(element)};
        }
        case NK_IDENT_NODE: return load_value(lvalue(node, block), block, node);
        case NK_ARRAY_REF: return load_value(lvalue(node, block), block, node);
        case NK_INDIRECT_REF: {
            auto* indirect = static_cast<IndirectRefASTNode*>(node);
            auto pointer = expression(indirect->getExpr(), block);
            if (pointer.type->kind != Type::Kind::Pointer) fail(node, "dereference requires a pointer");
            Lvalue target{.address = pointer.id, .type = pointer.type->pointee};
            return load_value(target, block, node);
        }
        case NK_ADDR_EXPR: {
            auto target = lvalue(static_cast<AddrExprASTNode*>(node)->getExpr(), block);
            return {target.address, types.pointer(target.type)};
        }
        case NK_LIST: {
            Value result{};
            for (auto* item : elements(node)) result = expression(item, block);
            return result;
        }
        case NK_ASSIGN_EXPR: {
            auto* assign = static_cast<AssignExprASTNode*>(node);
            auto target = lvalue(assign->getLhs(), block);
            auto value = expression(assign->getRhs(), block);
            store_lvalue(target, value, block, node);
            return load_value(target, block, node);
        }
        case NK_PREINCREMENT_EXPR: case NK_PREDECREMENT_EXPR:
        case NK_POSTINCREMENT_EXPR: case NK_POSTDECREMENT_EXPR: {
            Node* operand = nullptr;
            switch (node->getKind()) {
            case NK_PREINCREMENT_EXPR: operand = static_cast<PreincrementExprASTNode*>(node)->getExpr(); break;
            case NK_PREDECREMENT_EXPR: operand = static_cast<PredecrementExprASTNode*>(node)->getExpr(); break;
            case NK_POSTINCREMENT_EXPR: operand = static_cast<PostincrementExprASTNode*>(node)->getExpr(); break;
            case NK_POSTDECREMENT_EXPR: operand = static_cast<PostdecrementExprASTNode*>(node)->getExpr(); break;
            default: break;
            }
            auto target = lvalue(operand, block);
            auto old = load_value(target, block, node);
            const bool increment = node->getKind() == NK_PREINCREMENT_EXPR || node->getKind() == NK_POSTINCREMENT_EXPR;
            const auto one = constant(block, increment ? 1 : -1, long_type(), node);
            // Pointer operands step by one element, not by one byte.
            auto next = old.type->kind == Type::Kind::Pointer
                ? pointer_add(block, old, one, node)
                : arithmetic(block, increment ? "arith.addi" : "arith.subi", old,
                             constant(block, 1, int32(), node), node);
            store_lvalue(target, next, block, node);
            const bool post = node->getKind() == NK_POSTINCREMENT_EXPR || node->getKind() == NK_POSTDECREMENT_EXPR;
            return post ? old : load_value(target, block, node);
        }
        case NK_BIT_NOT_EXPR: {
            auto value = promote_value(block, expression(static_cast<BitNotExprASTNode*>(node)->getExpr(), block), node);
            return arithmetic(block, "arith.xor", value, constant(block, -1, value.type, node), node);
        }
        case NK_LOG_NOT_EXPR: {
            auto value = expression(static_cast<LogNotExprASTNode*>(node)->getExpr(), block);
            return comparison(block, "c.eq", value, constant(block, 0, value.type, node), node);
        }
        case NK_CAST_EXPR: {
            auto* cast = static_cast<CastExprASTNode*>(node);
            auto value = expression(cast->getExpr(), block);
            auto target = type_of(cast->getType());
            if (target->kind == Type::Kind::Void) return {};
            return convert(block, value, target, node, "cast");
        }
        case NK_COND_EXPR: {
            auto* cond = static_cast<CondExprASTNode*>(node);
            auto condition = promote_value(block, expression(cond->getCondition(), block), node);
            auto result = usual_arithmetic(expression_type(cond->getThenClause()), expression_type(cond->getElseClause()));
            return select(block, condition, cond->getThenClause(), cond->getElseClause(), node, false, false, result);
        }
        case NK_SIZEOF_EXPR: {
            const auto type = sizeof_type(static_cast<SizeOfExprASTNode*>(node)->getExpr());
            if (!type->size()) fail(node, "sizeof applied to an incomplete type");
            return constant(block, type->size(), long_type(), node);
        }
        case NK_CALL_EXPR: return call_expression(static_cast<CallExprASTNode*>(node), block);
        case NK_GENERIC_SELECTION:
            return expression(generic_selection(static_cast<GenericSelectionASTNode*>(node)), block);
        default: fail(node, "expression is not yet supported by C11 semantic lowering");
        }
    }

    // ---- statements ----
    bool terminated(const mlvm::Block& block) {
        return !block.operations.empty() && module.context->operation(block.operations.back().name)->terminator;
    }
    void close_block(mlvm::Block& block, Node* node) {
        if (!terminated(block)) emit(block, "c.yield", {}, nullptr, node);
    }
    void statement(Node* node, mlvm::Block& block, bool scope = true) {
        if (absent(node)) return;
        if (terminated(block)) fail(node, "statement after an unconditional transfer is not yet supported");
        switch (node->getKind()) {
        case NK_NOP_EXPR: return;
        case NK_STATIC_ASSERT: static_assertion(static_cast<StaticAssertASTNode*>(node)); return;
        case NK_TYPE_DECL: {
            auto* decl = static_cast<TypeDeclASTNode*>(node);
            typedefs.emplace_back().emplace(name(decl->getName()), type_of(decl->getBody()));
            return;
        }
        case NK_LIST: for (auto* item : elements(node)) statement(item, block); return;
        case NK_COMPOUND_STMT: {
            auto* body = static_cast<CompoundStmtASTNode*>(node);
            if (scope) scopes.emplace_back();
            statement(body->getDecls(), block);
            statement(body->getStmts(), block);
            if (scope) scopes.pop_back();
            return;
        }
        case NK_VAR_DECL: declare_local(static_cast<VarDeclASTNode*>(node), block); return;
        case NK_RETURN_STMT: {
            auto* ret = static_cast<ReturnStmtASTNode*>(node);
            const bool is_void = current_result->kind == Type::Kind::Void;
            if (is_void != absent(ret->getExpr())) fail(node, "return expression does not match function result");
            std::vector<mlvm::ValueId> values;
            if (!is_void) {
                auto value = convert(block, expression(ret->getExpr(), block), current_result, node, "return");
                values.push_back(value.id);
            }
            emit(block, "func.return", values, nullptr, node);
            return;
        }
        case NK_IF_STMT: {
            auto* branch = static_cast<IfStmtASTNode*>(node);
            auto condition = truth_value(block, expression(branch->getCondition(), block), node);
            auto& op = emit(block, "c.if", {condition.id}, nullptr, node);
            op.regions.resize(2);
            for (unsigned i = 0; i < 2; ++i) {
                auto& body = module.add_block(op.regions[i]);
                ++conditional;
                statement(i == 0 ? branch->getThenClause() : branch->getElseClause(), body);
                --conditional;
                close_block(body, node);
            }
            return;
        }
        case NK_WHILE_STMT: case NK_DO_STMT: case NK_FOR_STMT: {
            Node *condition = nullptr, *body_node = nullptr, *step = nullptr, *init = nullptr;
            const bool is_do = node->getKind() == NK_DO_STMT;
            if (node->getKind() == NK_WHILE_STMT) {
                auto* loop = static_cast<WhileStmtASTNode*>(node); condition = loop->getCondition(); body_node = loop->getBody();
            } else if (is_do) {
                auto* loop = static_cast<DoStmtASTNode*>(node); condition = loop->getCondition(); body_node = loop->getBody();
            } else {
                auto* loop = static_cast<ForStmtASTNode*>(node);
                condition = loop->getCondition(); body_node = loop->getBody(); step = loop->getStep(); init = loop->getInit();
            }
            scopes.emplace_back();
            statement(init, block);
            auto& op = emit(block, "c.loop", {}, nullptr, node);
            op.attributes["kind"] = is_do ? "do" : "while";
            op.regions.resize(3);
            auto& test = module.add_block(op.regions[0]);
            auto value = absent(condition) ? constant(test, 1, int32(), node)
                                           : promote_value(test, expression(condition, test), node);
            emit(test, "c.yield", {value.id}, nullptr, node);
            auto& body_block = module.add_block(op.regions[1]);
            ++loops; ++conditional;
            statement(body_node, body_block);
            --conditional; --loops;
            close_block(body_block, node);
            auto& increment = module.add_block(op.regions[2]);
            ++conditional;
            if (!absent(step)) expression(step, increment);
            --conditional;
            close_block(increment, node);
            scopes.pop_back();
            return;
        }
        case NK_BREAK_STMT: case NK_CONTINUE_STMT:
            if (!loops) fail(node, "break/continue requires an enclosing loop");
            emit(block, node->getKind() == NK_BREAK_STMT ? "c.break" : "c.continue", {}, nullptr, node);
            return;
        default: expression(node, block); return;
        }
    }
    void declare_local(VarDeclASTNode* decl, mlvm::Block& block) {
        const auto spelling = name(decl->getName());
        if (spelling.empty()) fail(decl, "expected a named local");
        const unsigned flags = decl->getType()->getFlags();
        if (flags & (SCS_STATIC | SCS_EXTERN)) { declare_static(decl, block); return; }
        check_object_flags(decl->getType(), TQ_CONST | TQ_VOLATILE | SCS_AUTO | SCS_REGISTER);
        if (scopes.back().contains(spelling)) fail(decl, "duplicate declaration of '" + spelling + "'");
        if (slots >= 65536) fail(decl, "too many local objects");
        auto type = type_of(decl->getType());
        // `const T *p` qualifies the pointee, not the pointer object.
        Object object{.kind = Object::Kind::Local, .slot = slots++, .symbol = {}, .type = type,
                      .constant = (flags & TQ_CONST) != 0 && type->kind != Type::Kind::Pointer};
        scopes.back().emplace(spelling, object);
        if (type->kind != Type::Kind::Array && !type->is_scalar())
            fail(decl, "aggregate locals are not yet supported");
        if (absent(decl->getInit())) return;
        Lvalue target{.address = local_address(block, object.slot, type, decl), .object = object, .type = type};
        target.object.constant = false; // initialization may write a const object
        if (type->kind == Type::Kind::Array) initialize_local(decl->getInit(), type, target, block, decl);
        else store_lvalue(target, expression(decl->getInit(), block), block, decl);
    }
    void declare_static(VarDeclASTNode* decl, mlvm::Block&) {
        const auto spelling = name(decl->getName());
        if (scopes.back().contains(spelling)) fail(decl, "duplicate declaration of '" + spelling + "'");
        auto type = type_of(decl->getType());
        if (decl->getType()->getFlags() & SCS_EXTERN) {
            if (!absent(decl->getInit())) fail(decl, "block extern declaration cannot have an initializer");
            scopes.back().emplace(spelling, Object{.kind = Object::Kind::Global, .slot = 0, .symbol = spelling,
                .type = type, .constant = (decl->getType()->getFlags() & TQ_CONST) != 0});
            return;
        }
        const auto symbol = "_ZLstatic_" + spelling + "_" + std::to_string(strings++);
        scopes.back().emplace(spelling, Object{.kind = Object::Kind::Global, .slot = 0, .symbol = symbol,
                                               .type = type, .constant = (decl->getType()->getFlags() & TQ_CONST) != 0});
        const auto bytes = static_image(decl->getInit(), type, decl);
        const bool is_const = (decl->getType()->getFlags() & TQ_CONST) != 0;
        define_global(symbol, type, is_const ? "rodata" : (bytes.empty() ? "bss" : "data"), bytes, decl);
    }

    Signature signature_of(FunctionDeclASTNode* fn, Node* node) {
        const auto symbol = name(fn->getName());
        if (symbol.empty()) fail(node, "expected function name");
        const unsigned flags = fn->getType()->getFlags();
        if (flags & ~(SCS_STATIC | SCS_EXTERN | TQ_CONST)) fail(node, "unsupported function storage class or qualifier");
        if ((flags & SCS_STATIC) && (flags & SCS_EXTERN)) fail(node, "conflicting function storage classes");
        auto result = type_of(fn->getType());
        auto parameters = elements(fn->getPrms());
        std::vector<const Type*> parameter_types;
        if (parameters.size() == 1 && parameters[0]->getKind() == NK_PARM_DECL) {
            auto* parameter = static_cast<ParmDeclASTNode*>(parameters[0]);
            if (type_of(parameter->getType())->kind == Type::Kind::Void && absent(parameter->getName())) parameters.clear();
        }
        if (parameters.size() > 6) fail(node, "System V lowering supports up to six parameters");
        for (auto* parameter : parameters) {
            if (parameter->getKind() != NK_PARM_DECL) fail(parameter, "expected a parameter");
            auto type = type_of(static_cast<ParmDeclASTNode*>(parameter)->getType());
            if (!type->is_scalar()) fail(parameter, "parameter must have a scalar type");
            parameter_types.push_back(type);
        }
        return Signature{!fn->isForwardDeclaration(), (flags & SCS_STATIC) != 0, false, result, parameter_types};
    }
public:
    Lowering(mlvm::Context& context, mlvm::Identifier file) : module(context), file(file), types(context) {}
    mlvm::Module run(Node* root) {
        auto& top = module.add_block(module.body);
        for (auto* node : elements(root)) {
            switch (node->getKind()) {
            case NK_STATIC_ASSERT: static_assertion(static_cast<StaticAssertASTNode*>(node)); continue;
            case NK_TYPE_DECL: {
                auto* decl = static_cast<TypeDeclASTNode*>(node);
                typedefs.emplace_back().emplace(name(decl->getName()), type_of(decl->getBody()));
                continue;
            }
            case NK_VAR_DECL: declare_global(static_cast<VarDeclASTNode*>(node), top); continue;
            case NK_FUNCTION_DECL: declare_function(static_cast<FunctionDeclASTNode*>(node), top); continue;
            default: fail(node, "unexpected declaration at file scope");
            }
        }
        for (auto& block : global_data.blocks) module.body.blocks.push_back(std::move(block));
        return std::move(module);
    }
    void declare_global(VarDeclASTNode* decl, mlvm::Block& top) {
        const auto spelling = name(decl->getName());
        if (spelling.empty()) fail(decl, "expected a named global");
        const unsigned flags = decl->getType()->getFlags();
        if (flags & ~(SCS_STATIC | SCS_EXTERN | TQ_CONST | TQ_VOLATILE))
            fail(decl, "unsupported global storage class");
        if ((flags & SCS_STATIC) && (flags & SCS_EXTERN)) fail(decl, "conflicting global storage classes");
        auto type = type_of(decl->getType());
        if (!type->is_scalar() && type->kind != Type::Kind::Array && type->kind != Type::Kind::Pointer)
            fail(decl, "global type is not yet supported");
        if (file_scope.contains(spelling) && *file_scope.at(spelling).type != *type)
            fail(decl, "conflicting declaration of '" + spelling + "'");
        file_scope[spelling] = Object{.kind = Object::Kind::Global, .slot = 0, .symbol = spelling,
                                     .type = type, .constant = (flags & TQ_CONST) != 0};
        if ((flags & SCS_EXTERN) && absent(decl->getInit())) return;
        const bool initialized = !absent(decl->getInit());
        if (initialized && !initialized_globals.insert(spelling).second)
            fail(decl, "duplicate definition of '" + spelling + "'");
        if (!defined_globals.insert(spelling).second) {
            if (initialized) for (auto& op : top.operations)
                if (op.name == "c.global" && op.attributes.at("sym") == spelling)
                    op.attributes["init"] = static_image(decl->getInit(), type, decl);
            return;
        }
        auto& op = emit(top, "c.global", {}, nullptr, decl);
        op.attributes["sym"] = spelling;
        op.attributes["size"] = std::to_string(type->size());
        op.attributes["align"] = std::to_string(type->align());
        const bool is_const = (flags & TQ_CONST) != 0;
        op.attributes["section"] = is_const ? "rodata" : "data";
        if (flags & SCS_STATIC) op.attributes["internal"] = "true";
        file_scope[spelling] = Object{.kind = Object::Kind::Global, .slot = 0, .symbol = spelling,
                                     .type = type, .constant = is_const};
        op.attributes["init"] = static_image(decl->getInit(), type, decl);
    }
    void declare_function(FunctionDeclASTNode* fn, mlvm::Block& top) {
        const auto symbol = name(fn->getName());
        auto signature = signature_of(fn, fn);
        auto it = functions.find(symbol);
        if (it == functions.end()) functions[symbol] = signature;
        else {
            auto& known = it->second;
            if (known.parameters.size() != signature.parameters.size() || *known.result != *signature.result)
                fail(fn, "conflicting declaration of '" + symbol + "'");
            for (std::size_t i = 0; i < known.parameters.size(); ++i)
                if (*known.parameters[i] != *signature.parameters[i])
                    fail(fn, "conflicting parameter type in '" + symbol + "'");
            if (signature.defined && known.defined) fail(fn, "duplicate definition of '" + symbol + "'");
            known.defined |= signature.defined;
            known.internal = known.internal && signature.internal;
            signature = known;
        }
        if (fn->isForwardDeclaration()) {
            auto& declaration = emit(top, "c.declare", {}, nullptr, fn);
            declaration.attributes["sym"] = symbol;
            declaration.attributes["result"] = std::string(signature.result->name());
            declaration.attributes["parameters"] = std::to_string(signature.parameters.size());
            return;
        }
        auto& function = emit(top, "builtin.func", {}, nullptr, fn);
        function.attributes["sym"] = symbol;
        function.attributes["result"] = std::string(signature.result->name());
        function.attributes["linkage"] = signature.internal ? "internal" : "external";
        function.regions.resize(1);
        std::vector<mlvm::Identifier> argument_types;
        for (auto* type : signature.parameters) argument_types.push_back(intern(type->name()));
        auto& entry = module.add_block(function.regions[0], argument_types);
        slots = 0; scopes.clear(); scopes.emplace_back(); current_result = signature.result;
        auto parameters = elements(fn->getPrms());
        if (parameters.size() == 1 && parameters[0]->getKind() == NK_PARM_DECL &&
            type_of(static_cast<ParmDeclASTNode*>(parameters[0])->getType())->kind == Type::Kind::Void &&
            absent(static_cast<ParmDeclASTNode*>(parameters[0])->getName())) parameters.clear();
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            auto* parameter = static_cast<ParmDeclASTNode*>(parameters[i]);
            const auto spelling = name(parameter->getName());
            auto type = type_of(parameter->getType());
            Object object{.kind = Object::Kind::Local, .slot = slots++, .symbol = {}, .type = type,
                          .constant = (parameter->getType()->getFlags() & TQ_CONST) != 0 &&
                                      type->kind != Type::Kind::Pointer};
            scopes.back().emplace(spelling, object);
            Lvalue target{.address = local_address(entry, object.slot, type, parameter), .object = object, .type = type};
            target.object.constant = false;
            store_lvalue(target, {entry.arguments[i], type}, entry, parameter);
        }
        statement(fn->getBody(), entry, false);
        if (!terminated(entry)) {
            if (signature.result->kind == Type::Kind::Void) emit(entry, "func.return", {}, nullptr, fn);
            else {
                auto zero = constant(entry, 0, signature.result, fn);
                emit(entry, "func.return", {zero.id}, nullptr, fn);
            }
        }
        function.attributes["locals"] = std::to_string(slots);
    }
};
} // namespace

std::optional<mlvm::Module> lower(mlvm::Context& context, mlvm::SourceManager& sources, mlvm::Identifier file, mlvm::DiagnosticEngine& diagnostics) {
    if (!mlvm::register_builtin_dialects(context, diagnostics)) return std::nullopt;
    std::optional<mlvm::Module> module;
    if (!parser_detail::with_ast(sources, file, diagnostics, [&](Node* root) { module = Lowering(context, file).run(root); })) return std::nullopt;
    if (!mlvm::verify(*module, diagnostics)) return std::nullopt;
    return module;
}
} // namespace cephyr
