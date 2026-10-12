    unsigned flags = 0, longs = 0;
    ASTNode *type = NULL_AST_NODE;
    bool seenInt = false, seenSigned = false, seenUnsigned = false;
    for (;;) {
        if (isStorageClassSpecifier(_sym)) {
            if (_sym == TK__THREAD_LOCAL) parsingError("_Thread_local lowering is unavailable");
            StorageClassSpecifier(flags);
        } else if (isTypeQualifier(_sym)) {
            if (_sym == TK_RESTRICT) parsingError("restrict lowering is unavailable");
            TypeQualifier(flags);
        } else if (_sym == TK_INT || _sym == TK_SIGNED || _sym == TK_UNSIGNED) {
            bool &seen = _sym == TK_INT ? seenInt : _sym == TK_SIGNED ? seenSigned : seenUnsigned;
            if (seen) parsingError("duplicate type specifier");
            seen = true;
            getTok();
        } else if (_sym == TK__BOOL) {
            if (type != NULL_AST_NODE) parsingError("unsupported type specifier combination");
            getTok();
            type = new IntegralTypeASTNode(0, false); // distinct _Bool marker
        } else if (isTypeSpecifier(_sym, 1)) {
            if (type != NULL_AST_NODE) parsingError("unsupported type specifier combination");
            if (_sym == TK_IDENT) {
                // A typedef name carries no cparser type node; hand the spelling to
                // Cephyr, which resolves typedefs from the declaration order.
                type = new IdentASTNode(getLATokInfoSval(1), _tok->line);
                getTok();
            } else {
                type = TypeSpecifier();
                if (type == NULL_AST_NODE) parsingError("type is not represented by cparser-iso11");
            }
            // `long long` is one integer type; upstream consumes a single `long`.
            while (_sym == TK_LONG) { getTok(); ++longs; }
        } else if (_sym == TK_INLINE || _sym == TK___ATTRIBUTE__ || _sym == TK___ASM) {
            parsingError("inline and compiler-extension declarations are not yet supported");
        } else break;
    }
    if (seenSigned && seenUnsigned) parsingError("conflicting signedness specifiers");
    if (longs) {
        if (type == NULL_AST_NODE || type->getKind() != NK_INTEGRAL_TYPE ||
            static_cast<IntegralTypeASTNode *>(type)->getAlignment() != 8)
            parsingError("long is not a valid specifier for this type");
        if (longs > 1) type = new IntegralTypeASTNode(8, true); // `long long`
    }
    if (type == NULL_AST_NODE) {
        if (!seenInt && !seenSigned && !seenUnsigned) parsingError("expected type specifier");
        type = new IntegralTypeASTNode(4, !seenUnsigned);
    } else if (type->getKind() == NK_INTEGRAL_TYPE) {
        auto *integer = static_cast<IntegralTypeASTNode *>(type);
        type = new IntegralTypeASTNode(integer->getAlignment(), !seenUnsigned);
    } else if (seenInt || seenSigned || seenUnsigned) parsingError("invalid type specifier combination");
    else if (type->getKind() == NK_VOID_TYPE) type = new VoidTypeASTNode();
    type->setFlags(flags);
    return type;
