// Optilisp S-expression reader (Layer 1: syntax).
//
// The reader is deliberately generic: it recognizes only the surface grammar
//   program  := form*
//   form     := list | atom
//   list     := '(' form* ')'
//   atom     := symbol | keyword | string | number | boolean | nil
// with ';' line comments. Domain knowledge (define-kernel et al.) lives in
// loader.cpp, keeping syntax and semantics separate.
//
// Lexical checks run before sfsexp AST parsing; any error discards all forms.
// sfsexp is used to validate/parse each top-level form and no sfsexp pointers
// escape: the returned Expr tree is fully value-owned.

#include <mlvm/optilisp/sexpr.hpp>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include "sexp.h"

namespace mlvm::optilisp {
namespace {

std::string escape_string(std::string_view value) {
    std::string out = "\"";
    for (char c : value) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\0': out += "\\0"; break;
        default:
            if (static_cast<unsigned char>(c) < 32) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\x%02x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out + '"';
}

bool is_integer_literal(std::string_view text) {
    if (text.empty()) return false;
    std::size_t i = 0;
    if (text[i] == '+' || text[i] == '-') {
        if (text.size() == 1) return false;
        i = 1;
    }
    for (; i < text.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    return true;
}

bool is_float_literal(std::string_view text) {
    if (text.empty() || text == "+" || text == "-") return false;
    std::string_view body = text;
    if (body[0] == '+' || body[0] == '-') body.remove_prefix(1);
    bool has_dot_or_exp = body.find('.') != std::string_view::npos ||
                          body.find('e') != std::string_view::npos ||
                          body.find('E') != std::string_view::npos;
    if (!has_dot_or_exp) return false;
    // strtod is the arbiter; reject non-finite results at the call site.
    char* end = nullptr;
    std::string copy(body);
    std::strtod(copy.c_str(), &end);
    return end != nullptr && *end == '\0';
}

// Decode Optilisp string escapes. Returns false on invalid escape.
bool decode_string(std::string_view raw, std::string& out, std::string& bad_escape) {
    out.clear();
    for (std::size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i + 1 >= raw.size()) {
            bad_escape = "truncated escape sequence";
            return false;
        }
        char esc = raw[++i];
        switch (esc) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '0': out += '\0'; break;
        default: bad_escape = std::string("invalid escape sequence '\\") + esc + "'"; return false;
        }
    }
    return true;
}

struct Scanner {
    std::string_view source;
    std::string file;
    ParseLimits limits;
    Diagnostics diagnostics;
    bool failed = false;

    void error(std::string code, std::string message, std::size_t line, std::size_t col) {
        diagnostics.push_back({std::move(code), std::move(message), {file, line, col}});
        failed = true;
    }

    // Lexical pre-scan: bounds, escapes, balance, depth. No forms produced.
    bool prescan() {
        if (source.size() > limits.max_bytes) {
            error("optilisp.limit", "source exceeds 1 MiB limit", 1, 1);
            return false;
        }
        std::size_t line = 1, col = 1, depth = 0;
        bool in_string = false, escaped = false, in_comment = false;
        for (std::size_t i = 0; i < source.size(); ++i) {
            char c = source[i];
            if (in_comment) {
                if (c == '\n') {
                    in_comment = false;
                    ++line;
                    col = 1;
                } else {
                    ++col;
                }
                continue;
            }
            if (in_string) {
                if (escaped) {
                    if (c != 'n' && c != 't' && c != 'r' && c != '"' && c != '\\' && c != '0') {
                        error("optilisp.syntax",
                              std::string("invalid escape sequence '\\") + c + "'", line, col);
                        return false;
                    }
                    escaped = false;
                    ++col;
                } else if (c == '\\') {
                    escaped = true;
                    ++col;
                } else if (c == '"') {
                    in_string = false;
                    ++col;
                } else if (c == '\n') {
                    // Strings may not span lines without an escape; the reader
                    // treats a raw newline inside a string as unterminated only
                    // at EOF, but we track lines accurately regardless.
                    ++line;
                    col = 1;
                } else {
                    ++col;
                }
                continue;
            }
            if (c == ';') {
                in_comment = true;
                ++col;
            } else if (c == '"') {
                in_string = true;
                escaped = false;
                ++col;
            } else if (c == '(') {
                if (++depth > limits.max_depth) {
                    error("optilisp.limit", "nesting exceeds 128 levels", line, col);
                    return false;
                }
                ++col;
            } else if (c == ')') {
                if (depth == 0) {
                    error("optilisp.syntax", "unexpected closing parenthesis", line, col);
                    return false;
                }
                --depth;
                ++col;
            } else if (c == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        if (in_string) {
            error("optilisp.syntax", "unterminated string literal", line, col);
            return false;
        }
        if (depth != 0) {
            error("optilisp.syntax", "unterminated list (missing ')')", line, col);
            return false;
        }
        return true;
    }
};

// Tokenizer producing positioned tokens for our value-owned parse.
struct Token {
    enum class Kind { lparen, rparen, atom, string };
    Kind kind;
    std::string text; // atom spelling or decoded string value
    std::string raw;  // raw atom text (for number classification)
    bool quoted = false;
    std::size_t line = 1, col = 1;
};

struct Tokenizer {
    std::string_view source;
    std::string file;
    std::size_t pos = 0, line = 1, col = 1;
    Diagnostics* diagnostics = nullptr;
    bool failed = false;

    void tok_error(std::string message, std::size_t l, std::size_t c) {
        if (diagnostics)
            diagnostics->push_back({"optilisp.syntax", std::move(message), {file, l, c}});
        failed = true;
    }

    void advance(std::size_t n = 1) {
        for (std::size_t i = 0; i < n && pos < source.size(); ++i) {
            if (source[pos] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
            ++pos;
        }
    }

    void skip_trivia() {
        while (pos < source.size()) {
            char c = source[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
                advance();
            } else if (c == ';') {
                while (pos < source.size() && source[pos] != '\n') advance();
            } else {
                break;
            }
        }
    }

    bool eof() {
        // skip_trivia is applied by callers; this is a raw check.
        return pos >= source.size();
    }

    // Read one token; returns false at EOF (no error) or on fatal error.
    bool next(Token& out) {
        skip_trivia();
        if (pos >= source.size()) return false;
        char c = source[pos];
        if (c == '(') {
            out = {Token::Kind::lparen, "(", "(", false, line, col};
            advance();
            return true;
        }
        if (c == ')') {
            out = {Token::Kind::rparen, ")", ")", false, line, col};
            advance();
            return true;
        }
        if (c == '"') {
            std::size_t sl = line, sc = col;
            advance(); // opening quote
            std::string raw;
            while (pos < source.size() && source[pos] != '"') {
                char ch = source[pos];
                if (ch == '\\') {
                    if (pos + 1 >= source.size()) {
                        tok_error("unterminated escape sequence", sl, sc);
                        return false;
                    }
                    raw += ch;
                    advance();
                    raw += source[pos];
                    advance();
                } else {
                    raw += ch;
                    advance();
                }
            }
            if (pos >= source.size()) {
                tok_error("unterminated string literal", sl, sc);
                return false;
            }
            advance(); // closing quote
            std::string decoded, bad;
            if (!decode_string(raw, decoded, bad)) {
                tok_error(bad, sl, sc);
                return false;
            }
            out = {Token::Kind::string, decoded, raw, true, sl, sc};
            return true;
        }
        if (c == ':') {
            std::size_t sl = line, sc = col;
            advance();
            std::size_t start = pos;
            while (pos < source.size()) {
                char ch = source[pos];
                if (std::isspace(static_cast<unsigned char>(ch)) || ch == '(' || ch == ')' ||
                    ch == '"' || ch == ';')
                    break;
                advance();
            }
            std::string name(source.substr(start, pos - start));
            if (name.empty()) {
                tok_error("expected a keyword name after ':'", sl, sc);
                return false;
            }
            out = {Token::Kind::atom, ":" + name, ":" + name, false, sl, sc};
            return true;
        }
        // Bare atom.
        std::size_t sl = line, sc = col;
        std::size_t start = pos;
        while (pos < source.size()) {
            char ch = source[pos];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '(' || ch == ')' || ch == '"' ||
                ch == ';')
                break;
            advance();
        }
        std::string text(source.substr(start, pos - start));
        out = {Token::Kind::atom, text, text, false, sl, sc};
        return true;
    }
};

Expr atom_to_expr(const Token& tok, const std::string& file) {
    Location loc{file, tok.line, tok.col};
    if (tok.kind == Token::Kind::string) return {ExprKind::string, tok.text, {}, loc};
    const std::string& text = tok.text;
    if (!text.empty() && text[0] == ':') return {ExprKind::keyword, text.substr(1), {}, loc};
    if (text == "true") return {ExprKind::boolean, "true", {}, loc};
    if (text == "false") return {ExprKind::boolean, "false", {}, loc};
    if (text == "nil") return {ExprKind::nil, "nil", {}, loc};
    if (is_integer_literal(text)) return {ExprKind::number, text, {}, loc};
    if (is_float_literal(text)) {
        double v = std::strtod(text.c_str(), nullptr);
        if (!std::isfinite(v)) {
            // Non-finite literals fail deterministically (mirrors the
            // Python catalog's E-NUMBER check).
            return {ExprKind::symbol, text, {}, loc}; // flagged by caller
        }
        return {ExprKind::number, text, {}, loc};
    }
    return {ExprKind::symbol, text, {}, loc};
}

struct TreeParser {
    std::vector<Token> tokens;
    std::size_t pos = 0;
    std::string file;
    ParseLimits limits;
    Diagnostics* diagnostics = nullptr;
    std::size_t nodes = 0;
    bool failed = false;
    bool non_finite = false;

    void error(std::string message, std::size_t l, std::size_t c) {
        if (diagnostics)
            diagnostics->push_back({"optilisp.syntax", std::move(message), {file, l, c}});
        failed = true;
    }

    Expr parse_form(std::size_t depth) {
        if (pos >= tokens.size()) {
            error("unexpected end of input", 1, 1);
            return {ExprKind::nil, "nil", {}, {file, 1, 1}};
        }
        if (++nodes > limits.max_nodes) {
            error("node limit exceeded", tokens[pos].line, tokens[pos].col);
            return {ExprKind::nil, "nil", {}, {file, 1, 1}};
        }
        if (depth > limits.max_depth) {
            error("nesting exceeds 128 levels", tokens[pos].line, tokens[pos].col);
            return {ExprKind::nil, "nil", {}, {file, 1, 1}};
        }
        Token tok = tokens[pos];
        if (tok.kind == Token::Kind::rparen) {
            error("unexpected ')'", tok.line, tok.col);
            ++pos;
            return {ExprKind::nil, "nil", {}, {file, tok.line, tok.col}};
        }
        if (tok.kind == Token::Kind::lparen) {
            ++pos;
            Expr list{ExprKind::list, "", {}, {file, tok.line, tok.col}};
            while (pos < tokens.size() && tokens[pos].kind != Token::Kind::rparen) {
                if (tokens[pos].kind == Token::Kind::lparen || tokens[pos].kind == Token::Kind::atom ||
                    tokens[pos].kind == Token::Kind::string) {
                    list.items.push_back(parse_form(depth + 1));
                    if (failed) return list;
                } else {
                    break;
                }
            }
            if (pos >= tokens.size() || tokens[pos].kind != Token::Kind::rparen) {
                error("unexpected end of input: missing ')'", tok.line, tok.col);
                return list;
            }
            ++pos;
            return list;
        }
        ++pos;
        Expr e = atom_to_expr(tok, file);
        // Detect non-finite floats that slipped through as symbols.
        if (e.kind == ExprKind::symbol &&
            (e.value == "inf" || e.value == "+inf" || e.value == "-inf" || e.value == "nan" ||
             e.value == "+nan" || e.value == "-nan" || e.value == "Infinity" || e.value == "NaN")) {
            non_finite = true;
        }
        if (e.kind == ExprKind::symbol && is_float_literal(e.value)) {
            double v = std::strtod(e.value.c_str(), nullptr);
            if (!std::isfinite(v)) non_finite = true;
        }
        return e;
    }
};

// Validate one top-level form with sfsexp. The tree is discarded; the
// value-owned Expr comes from our positioned tokenizer.
bool check_with_sfsexp(std::string_view form_text, Diagnostics& diagnostics, const std::string& file,
                       std::size_t line, std::size_t col) {
    if (form_text.empty()) return true;
    std::string owned(form_text);
    // sfsexp may overwrite NULs? It handles embedded escapes as raw text.
    sexp_t* parsed = parse_sexp(owned.data(), owned.size());
    if (parsed == nullptr) {
        diagnostics.push_back(
            {"optilisp.syntax", "sfsexp could not parse form", {file, line, col}});
        return false;
    }
    // Reject binary/squote atoms: Optilisp has no binary or quoted forms.
    bool ok = true;
    std::function<void(sexp_t*)> visit = [&](sexp_t* node) {
        if (!ok || node == nullptr) return;
        if (node->ty == SEXP_VALUE) {
            if (node->aty == SEXP_BINARY || node->aty == SEXP_SQUOTE) ok = false;
        } else if (node->ty == SEXP_LIST) {
            for (sexp_t* item = node->list; item != nullptr; item = item->next) {
                if (item->ty == SEXP_VALUE) {
                    if (item->aty == SEXP_BINARY || item->aty == SEXP_SQUOTE) {
                        ok = false;
                        return;
                    }
                } else {
                    visit(item);
                    if (!ok) return;
                }
            }
        }
        // Siblings are visited by the caller loop for the top level; nested
        // next-pointers inside a list are covered above. Top-level siblings
        // only occur when our splitter failed, which cannot happen.
    };
    visit(parsed);
    destroy_sexp(parsed);
    if (!ok)
        diagnostics.push_back(
            {"optilisp.syntax", "unsupported quoted or binary atom", {file, line, col}});
    return ok;
}

} // namespace

std::string Diagnostic::render() const {
    std::ostringstream out;
    if (!location.file.empty())
        out << location.file << ':' << location.line << ':' << location.column << ": ";
    else
        out << "<unknown>: ";
    out << '[' << code << "] " << message;
    return out.str();
}

bool Expr::operator==(const Expr& other) const {
    if (kind != other.kind || value != other.value || items.size() != other.items.size())
        return false;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (!(items[i] == other.items[i])) return false;
    return true;
}

bool Expr::symbol(std::string_view spelling) const {
    return kind == ExprKind::symbol && value == spelling;
}

std::string Expr::print() const {
    switch (kind) {
    case ExprKind::list: {
        std::string out = "(";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i) out += ' ';
            out += items[i].print();
        }
        return out + ')';
    }
    case ExprKind::keyword: return ":" + value;
    case ExprKind::string: return escape_string(value);
    case ExprKind::number:
    case ExprKind::symbol: return value;
    case ExprKind::boolean: return value;
    case ExprKind::nil: return "nil";
    }
    return "";
}

ParseResult parse(std::string_view source, std::string file, ParseLimits limits) {
    ParseResult result;
    Scanner scanner{std::string(source), file, limits, {}, false};
    // Copy the view into the scanner (it holds a string).
    scanner.source = std::string(source);
    if (!scanner.prescan()) {
        result.diagnostics = std::move(scanner.diagnostics);
        return result;
    }

    // Tokenize fully (positions retained for diagnostics).
    Tokenizer tokenizer{std::string(source), std::move(file), 0, 1, 1, &result.diagnostics, false};
    tokenizer.source = std::string(source);
    std::vector<Token> tokens;
    for (;;) {
        Token tok;
        std::size_t before = tokenizer.pos;
        bool have = tokenizer.next(tok);
        if (tokenizer.failed) return result;
        if (!have) break;
        tokens.push_back(std::move(tok));
        if (tokenizer.pos == before) break; // defensive: no progress
    }

    // Split tokens into top-level forms at depth 0. Each form is either a
    // balanced list or a single atom. Record the source slice for sfsexp.
    // Reconstruct slices from token boundaries using the original source and
    // token line/col (sfsexp validation uses the slice text).
    std::vector<std::vector<Token>> forms;
    std::size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i].kind == Token::Kind::rparen) {
            result.diagnostics.push_back(
                {"optilisp.syntax", "unexpected ')'", {tokenizer.file, tokens[i].line, tokens[i].col}});
            result.forms.clear();
            return result;
        }
        if (tokens[i].kind == Token::Kind::lparen) {
            std::size_t depth = 0, j = i;
            do {
                if (tokens[j].kind == Token::Kind::lparen) ++depth;
                if (tokens[j].kind == Token::Kind::rparen) --depth;
                ++j;
            } while (j < tokens.size() && depth > 0);
            if (depth != 0) {
                result.diagnostics.push_back({"optilisp.syntax", "unterminated list (missing ')')",
                                               {tokenizer.file, tokens[i].line, tokens[i].col}});
                result.forms.clear();
                return result;
            }
            forms.emplace_back(tokens.begin() + i, tokens.begin() + j);
            i = j;
        } else {
            forms.emplace_back(tokens.begin() + i, tokens.begin() + i + 1);
            ++i;
        }
    }

    // Parse each form with our tree parser (locations) after sfsexp validation.
    // To validate with sfsexp we need the raw source slice per form. Recover it
    // by re-printing tokens? Instead, re-scan the original source to find each
    // form's byte range. Simpler: use the tokenizer positions. We stored only
    // line/col, not offsets, so re-derive slices by walking the source with a
    // second offset-tracking pass. For robustness, validate the *printed* form
    // text (canonical spacing) with sfsexp: it is grammatically equivalent and
    // avoids offset bookkeeping. String values round-trip through escaping.
    TreeParser parser{{}, 0, tokenizer.file, limits, &result.diagnostics, 0, false, false};
    for (auto& ftoks : forms) {
        parser.tokens = ftoks;
        parser.pos = 0;
        // Build a canonical slice for sfsexp validation from tokens.
        std::string slice;
        {
            // Reconstruct approximate source: atoms/strings need quoting.
            // Rather than exact bytes, print tokens canonically; sfsexp
            // accepts canonical spacing.
            for (std::size_t k = 0; k < ftoks.size(); ++k) {
                const auto& t = ftoks[k];
                if (t.kind == Token::Kind::lparen)
                    slice += '(';
                else if (t.kind == Token::Kind::rparen)
                    slice += ')';
                else if (t.kind == Token::Kind::string)
                    slice += escape_string(t.text);
                else
                    slice += t.text;
                if (k + 1 < ftoks.size()) {
                    auto nk = ftoks[k + 1].kind;
                    if (t.kind != Token::Kind::lparen && nk != Token::Kind::rparen) slice += ' ';
                }
            }
        }
        std::size_t fl = ftoks.front().line, fc = ftoks.front().col;
        if (!check_with_sfsexp(slice, result.diagnostics, parser.file, fl, fc)) {
            result.forms.clear();
            return result;
        }
        Expr form = parser.parse_form(1);
        if (parser.failed) {
            result.forms.clear();
            return result;
        }
        if (parser.pos != ftoks.size()) {
            result.diagnostics.push_back({"optilisp.syntax", "trailing tokens in form",
                                           {parser.file, fl, fc}});
            result.forms.clear();
            return result;
        }
        result.forms.push_back(std::move(form));
        if (result.forms.size() * 16 > limits.max_nodes && result.forms.size() > limits.max_nodes) {
            result.diagnostics.push_back(
                {"optilisp.limit", "node limit exceeded", {parser.file, fl, fc}});
            result.forms.clear();
            return result;
        }
    }
    if (parser.non_finite) {
        result.diagnostics.push_back(
            {"optilisp.number", "non-finite numeric literal", {tokenizer.file, 1, 1}});
        result.forms.clear();
        return result;
    }
    if (parser.nodes > limits.max_nodes) {
        result.diagnostics.push_back(
            {"optilisp.limit", "node limit exceeded", {tokenizer.file, 1, 1}});
        result.forms.clear();
        return result;
    }
    return result;
}

ParseResult parse_file(const std::string& path, ParseLimits limits) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        ParseResult result;
        result.diagnostics.push_back({"optilisp.io", "cannot open file '" + path + "'", {path, 1, 1}});
        return result;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        ParseResult result;
        result.diagnostics.push_back({"optilisp.io", "cannot read file '" + path + "'", {path, 1, 1}});
        return result;
    }
    return parse(contents.str(), path, limits);
}

} // namespace mlvm::optilisp
