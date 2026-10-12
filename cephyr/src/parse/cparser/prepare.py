"""Apply reviewed embedding fixes to a build-local cparser-iso11 copy.

Replacements fail closed when the upstream source layout changes. Sources and
their copyright notices remain in the copy; the downloaded tree is never edited.
"""
from pathlib import Path
import shutil
import sys
import re
import json

source, target = map(Path, sys.argv[1:])
adapter = Path(__file__).parent
MARKER = "cephyr-adapter.json"
ADAPTED = "cephyr-cparser-iso11"


def update_adapter():
    """Idempotent migrations shared by pristine and previously adapted trees."""
    path = target / "include/common.h"
    text = path.read_text()
    if "#include <cstdarg>" not in text:
        text = text.replace("#include <cstring>", "#include <cstring>\n#include <cstdarg>")
    path.write_text(text)
    path = target / "include/Lexer.h"
    text = path.read_text().replace("        int ival;", "        long long ival;")
    if "integer_width" not in text:
        text = text.replace("    std::string sval;", "    unsigned integer_width = 32;\n    bool integer_unsigned = false;\n    std::string sval;")
    path.write_text(text)

    path = target / "include/ASTNode.h"
    text = path.read_text()
    begin = text.index("class IntegerConstASTNode :")
    end = text.index("\n};", begin) + 3
    text = text[:begin] + '''class IntegerConstASTNode : public ASTNode
{
    long long _value;
    bool _unsigned;
    unsigned _width;
public:
    IntegerConstASTNode(long long value, bool is_unsigned = false, unsigned width = 32)
        : _value(value), _unsigned(is_unsigned), _width(width) { kind = NK_INTEGER_CONST; }
    long long getValue() { return _value; }
    bool isUnsigned() { return _unsigned; }
    unsigned getWidth() { return _width; }
    void accept(TreeVisitor *v);
};''' + text[end:]
    begin = text.index("class StringConstASTNode :")
    end = text.index("\n};", begin)
    declaration = text[begin:end]
    if "getString()" not in declaration:
        declaration = declaration.replace("    const char *getValue() { return _value.c_str(); }",
            "    const char *getValue() { return _value.c_str(); }\n    const std::string &getString() const { return _value; }")
    text = text[:begin] + declaration + text[end:]
    path.write_text(text)
    path = target / "src/CParser.cpp"
    text = re.sub(r"new IntegerConstASTNode\(_tok->info\.ival[^;]*?\)",
                  "new IntegerConstASTNode(_tok->info.ival, _tok->integer_unsigned, _tok->integer_width)", path.read_text())
    text = text.replace("kind == TK_UNSIGNED || kind == TK_VOID)", "kind == TK_UNSIGNED || kind == TK_VOID || kind == TK__BOOL)")
    text = text.replace("        getTok();\n        res = new StringConstASTNode(_tok->sval);",
        "        std::string value;\n        do { getTok(); value += _tok->sval; } while (_sym == TK_STRING_LIT);\n        res = new StringConstASTNode(value);")
    text = re.sub(r"        // Is this a function call\?.*?        check\(TK_RPAR\);",
                  "        res = Expression();\n        check(TK_RPAR);", text, flags=re.S)
    text = text.replace("_sym == TK_LPAR && isTypeSpecifier(getLATok(2), 2)",
                        "_sym == TK_LPAR && (isTypeSpecifier(getLATok(2), 2) || isTypeQualifier(getLATok(2)))")
    text = text.replace("expr = UnaryExpression(typeName);\n    return cast", "expr = cast ? CastExpression() : UnaryExpression(typeName);\n    return cast")
    begin = text.index("    else if (_sym == TK_SIZEOF)")
    end = text.index("    else if (_sym == TK__ALIGNOF)", begin)
    text = text[:begin] + '''    else if (_sym == TK_SIZEOF)
    {
        getTok();
        if (_sym == TK_LPAR && (isTypeSpecifier(getLATok(2), 2) || isTypeQualifier(getLATok(2)))) {
            getTok();
            expr = DeclarationSpecifiers();
            while (_sym == TK_TIMES) { getTok(); expr = new PointerTypeASTNode(expr); }
            check(TK_RPAR);
        } else expr = UnaryExpression(NULL_AST_NODE);
        res = new SizeOfExprASTNode(expr);
    }
''' + text[end:]
    begin = text.index("ASTNode *CParser::DeclarationSpecifiers()")
    opening = text.index("{", begin)
    end = text.index("\n}", opening) + 2
    text = text[:opening] + "{\n" + (adapter / "specifiers.inc").read_text() + "\n}" + text[end:]
    path.write_text(text)
    path = target / "src/CLexer.cpp"
    text = path.read_text()
    for method, filename in (("readNumberLit", "number.inc"), ("readStringLit", "string.inc")):
        begin = text.index("void CLexer::" + method)
        opening = text.index("{", begin)
        end = text.index("\n}", opening) + 2
        text = text[:opening] + "{\n" + (adapter / filename).read_text() + "\n}" + text[end:]
    path.write_text(text)
    (target / MARKER).write_text(json.dumps({"adapter": ADAPTED, "version": 2}) + "\n")

# Historical build copies retain the base adapter. Apply current migrations on
# every configure, including copies recovered from an older build directory.
if (source / MARKER).is_file():
    manifest = (source / MARKER).read_text().strip()
    if manifest != ADAPTED:
        metadata = json.loads(manifest)
        if metadata.get("adapter") != ADAPTED or metadata.get("version") != 2:
            raise RuntimeError("unsupported cparser adapter manifest")
    for directory in ("include", "src"):
        shutil.copytree(source / directory, target / directory, dirs_exist_ok=True)
    for name in ("LICENSE", MARKER):
        if (source / name).is_file():
            shutil.copyfile(source / name, target / name)
    update_adapter()
    print(f"cparser-iso11 adapted tree updated: {source}")
    sys.exit(0)

for directory in ("include", "src"):
    shutil.copytree(source / directory, target / directory, dirs_exist_ok=True)
shutil.copyfile(source / "LICENSE", target / "LICENSE")


def replace(text, old, new):
    if old not in text:
        raise RuntimeError(f"cparser-iso11 adapter mismatch: {old[:100]!r}")
    return text.replace(old, new)


def function(text, signature, body):
    # Upstream function closing braces begin in column zero.
    begin = text.index(signature)
    opening = text.index("{", begin)
    end = text.index("\n}", opening) + 2
    return text[:opening] + "{\n" + body + "\n}" + text[end:]


for name in ("Parser", "Lexer", "AbstractSyntaxTree"):
    path = target / "src" / f"{name}.cpp"
    text = '#include "error.hpp"\n#include <cstdarg>\n' + path.read_text()
    if name == "Parser":
        text = function(text, "void Parser::parsingError", '''    ++_parsingErrors;
    const auto &token = _tokbuf[_laIdx];
    throw cephyr::parser_detail::Error(msg, token.line, token.col);''')
        text = replace(text, 'sprintf(msg, "expected \'%s\'", _name[expected]);',
                       'snprintf(msg, sizeof(msg), "expected token %u", expected);')
        text = replace(text, '    _sym = _lex->next(&_tokbuf[0]);',
                       '    _tok = &_tokbuf[0];\n    _sym = _lex->next(&_tokbuf[0]);')
    else:
        signature = f"void {name}::error"
        text = function(text, signature, '''    char message[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    throw cephyr::parser_detail::Error(message, lineNum);''')
        if name == "Lexer":
            text = replace(text, '''        printf("Fatal error: file '%s' does not exists!\\n", filename);
        exit(1);''', '''        throw cephyr::parser_detail::Error("cannot open parser input", 1);''')
    path.write_text(text)

path = target / "include/Lexer.h"
path.write_text(replace(path.read_text(), "~Lexer() {}", "virtual ~Lexer() { if (_fp && _fp != stdin) fclose(_fp); }"))

path = target / 'include/ASTNode.h'
text = replace(path.read_text(), '    NK_UNION_TYPE\n', '    NK_UNION_TYPE,\n    NK_STATIC_ASSERT,\n    NK_GENERIC_ASSOC,\n    NK_GENERIC_SELECTION\n')
# Exception-based embedding needs to reclaim nodes that were never attached to
# the root when a production failed. Retain upstream reference counting for the
# normal path and track live nodes per thread for orphan-root cleanup.
text = '#include <set>\n' + text
text = replace(text, '    ASTNodeKind getKind() { return kind; }', '''    inline static thread_local std::set<ASTNode *> cephyrLiveNodes;
    ASTNodeKind getKind() { return kind; }''')
text = replace(text, '    ASTNode()\n    {', '    ASTNode()\n    {\n        cephyrLiveNodes.insert(this);')
text = replace(text, '    ASTNode(ASTNodeKind Kind)\n    {', '    ASTNode(ASTNodeKind Kind)\n    {\n        cephyrLiveNodes.insert(this);')
text = replace(text, '    virtual ~ASTNode() {}', '    virtual ~ASTNode() { cephyrLiveNodes.erase(this); }')
text = replace(text, '''        if (!s_instance)
            s_instance = new NullASTNode();
        return s_instance;''', '''        static thread_local NullASTNode instance;
        return &instance;''')
path.write_text(text)
path = target / 'include/CParser.h'
path.write_text(replace(path.read_text(), '    ASTNode *GenericSelection();', '    ASTNode *GenericSelection();\n    ASTNode *StaticAssert();'))

path = target / "src/CParser.cpp"
text = '#include "nodes.hpp"\n' + path.read_text()
text = replace(text, '    ASTNode *tunit = NULL_AST_NODE;', '    ASTNode *tunit = new SequenceASTNode();')
text = replace(text, '''        case TK_MINUS:
            getTok();
            return NK_UNKNOWN;''', '''        case TK_MINUS:
            getTok();
            return NK_NEGATE_EXPR;''')
text = replace(text, '''        else if (kind == NK_BIT_NOT_EXPR)''', '''        else if (kind == NK_NEGATE_EXPR)
            res = new MinusExprASTNode(type, new IntegerConstASTNode(0), CastExpression());
        else if (kind == NK_UNKNOWN)
            res = CastExpression();
        else if (kind == NK_BIT_NOT_EXPR)''')
text = replace(text, '''    return res;
}

ASTNode *CParser::GenericSelection()''', '''    if (res == NULL_AST_NODE) parsingError("expected expression");
    return res;
}

ASTNode *CParser::GenericSelection()''')
text = replace(text, '''    return res;
}

ASTNodeKind CParser::UnaryOperator()''', '''    if (res == NULL_AST_NODE) parsingError("expected expression");
    return res;
}

ASTNodeKind CParser::UnaryOperator()''')
# Preserve comma expressions instead of silently dropping the left operand.
text = function(text, 'ASTNode *CParser::Expression()', '''    ASTNode *expr = AssignmentExpression();
    if (_sym != TK_COMMA) return expr;
    auto *list = new SequenceASTNode(expr);
    while (_sym == TK_COMMA) { getTok(); list->add(AssignmentExpression()); }
    return list;''')
# Upstream passes the pre-declarator type into the node constructor while
# calling Declarator() in the same argument list; evaluation order is
# unspecified, so pointer and array suffixes can be lost. Sequence explicitly.
text = function(text, 'ASTNode *CParser::InitDeclarator(', '''    ASTNode *init = NULL_AST_NODE;
    auto *declr = new VarDeclASTNode(NULL_AST_NODE, NULL_AST_NODE);
    ASTNode *name = Declarator(typeSpec);
    declr->setType(typeSpec);
    declr->setName(name);
    if (_sym == TK_ASSIGN) {
        getTok();
        init = Initializer();
        declr->setInit(init);
    }
    return declr;''')
text = function(text, 'ASTNode *CParser::ParameterDeclaration()', '''    auto *type = DeclarationSpecifiers();
    ASTNode *identifier = NULL_AST_NODE;
    if (_sym == TK_IDENT || _sym == TK_TIMES) identifier = Declarator(type);
    else if (_sym == TK_LPAR || _sym == TK_LBRACK) parsingError("abstract parameter declarator is not yet supported");
    return new ParmDeclASTNode(type, identifier);''')
text = function(text, 'ASTNode *CParser::ParameterTypeList()', '''    auto *parameters = ParameterList();
    if (_sym == TK_COMMA) parsingError("variadic function lowering is not yet supported");
    return parameters;''')
text = replace(text, '''    while (_sym == TK___ASM || _sym == TK___ATTRIBUTE__)
        GccDeclaratorExtension();''', '''    if (_sym == TK___ASM || _sym == TK___ATTRIBUTE__)
        parsingError("compiler-extension declarations are not supported");''')
text = function(text, 'ASTNode *CParser::FunctionBody()', '    return CompoundStatement();')
text = function(text, 'ASTNode *CParser::CompoundStatement()', '''    check(TK_LBRACE);
    _stb.openScope();
    auto *items = new SequenceASTNode();
    while (_sym != TK_RBRACE) {
        if (_sym == TK_EOF) parsingError("expected closing brace");
        if (isStorageClassSpecifier(_sym) || isTypeQualifier(_sym) || isTypeSpecifier(_sym, 1)) {
            auto *type = DeclarationSpecifiers();
            if (_inTypedef) {
                _inTypedef = false;
                auto *decl = new TypeDeclASTNode(Declarator(type), type);
                decl->declare(_ast);
                items->add(decl);
                check(TK_SEMICOLON);
            } else items->add(Declaration(type));
        } else if (_sym == TK__STATIC_ASSERT) items->add(StaticAssert());
        else items->add(Statement());
    }
    check(TK_RBRACE);
    _stb.closeScope();
    return new CompoundStmtASTNode(NULL_AST_NODE, items);''')
text = function(text, 'ASTNode *CParser::Statement()', '''    if (_sym == TK_LBRACE) return CompoundStatement();
    if (_sym == TK_IF || _sym == TK_SWITCH) return SelectionStatement();
    if (_sym == TK_FOR || _sym == TK_WHILE || _sym == TK_DO) return IterationStatement();
    if (_sym == TK_RETURN || _sym == TK_BREAK || _sym == TK_CONTINUE || _sym == TK_GOTO)
        return JumpStatement();
    if (_sym == TK_CASE || _sym == TK_DEFAULT || (_sym == TK_IDENT && getLATok(2) == TK_COLON))
        return LabeledStatement();
    if (_sym == TK_SEMICOLON) { getTok(); return new NopExprASTNode(); }
    return ExpressionStatement();''')
text = function(text, 'ASTNode *CParser::GenericSelection()', '''    check(TK__GENERIC);
    check(TK_LPAR);
    auto *control = AssignmentExpression();
    check(TK_COMMA);
    auto *associations = GenericAssocList();
    check(TK_RPAR);
    return new GenericSelectionASTNode(control, associations);''')
text = function(text, 'ASTNode *CParser::GenericAssociation()', '''    ASTNode *type = NULL_AST_NODE;
    if (_sym == TK_DEFAULT) getTok();
    else type = DeclarationSpecifiers();
    check(TK_COLON);
    return new GenericAssociationASTNode(type, AssignmentExpression());''')
text = replace(text, '        else if (_sym == TK_EOF)\n', '''        else if (_sym == TK__STATIC_ASSERT) {
            static_cast<SequenceASTNode *>(tunit)->add(StaticAssert());
        }
        else if (_sym == TK_EOF)
''')
text = replace(text, '} // namespace cparser', '''ASTNode *CParser::StaticAssert() {
    check(TK__STATIC_ASSERT);
    const int line = _tok->line;
    check(TK_LPAR);
    auto *expression = ConstantExpression();
    check(TK_COMMA);
    check(TK_STRING_LIT);
    auto *assertion = new StaticAssertASTNode(expression, _tok->sval);
    assertion->setLineNum(line);
    check(TK_RPAR);
    check(TK_SEMICOLON);
    return assertion;
}
} // namespace cparser''')
text = replace(text, '''        Statement();
    }

    return labstmt;''', '''        labstmt = new CaseLabelASTNode(NULL_AST_NODE, Statement());
    }

    return labstmt;''')
# Avoid losing signedness, qualifiers, or storage class on shared builtin types.
text = function(text, 'ASTNode *CParser::CastExpression(', '''    ASTNode *typeName = NULL_AST_NODE;
    ASTNode *expr = NULL_AST_NODE;
    bool cast = false;

    if (_sym == TK_LPAR && isTypeSpecifier(getLATok(2), 2))
    {
        getTok();
        cast = true;
        // DeclarationSpecifiers, not TypeSpecifier: casts name whole
        // multi-word types such as `unsigned char`, and any number of `*`.
        typeName = DeclarationSpecifiers();
        while (_sym == TK_TIMES) { getTok(); typeName = new PointerTypeASTNode(typeName); }
        check(TK_RPAR);
    }
    expr = UnaryExpression(typeName);
    return cast ? new CastExprASTNode(typeName, expr) : expr;''')
text = function(text, 'ASTNode *CParser::DeclarationSpecifiers()', (adapter / 'specifiers.inc').read_text())
# C99/C11 declaration in the for initializer.
text = replace(text, '''        if (_sym != TK_SEMICOLON)
            init = Expression();

        check(TK_SEMICOLON);''', '''        if (isTypeSpecifier(_sym, 1) || isTypeQualifier(_sym) || isStorageClassSpecifier(_sym))
            init = Declaration(DeclarationSpecifiers());
        else {
            if (_sym != TK_SEMICOLON) init = Expression();
            check(TK_SEMICOLON);
        }''')
path.write_text(text)

path = target / 'src/CLexer.cpp'
text = '#include <charconv>\n#include <climits>\n' + path.read_text()
for method, filename in [('readNumberLit', 'number.inc'), ('readStringLit', 'string.inc'), ('comment', 'comment.inc')]:
    text = function(text, f'void CLexer::{method}', (adapter / filename).read_text())
text = replace(text, "while (_ch != '\\n')", "while (_ch != '\\n' && _ch != EOF)")
path.write_text(text)
(target / MARKER).write_text(ADAPTED + "\n")
update_adapter()
