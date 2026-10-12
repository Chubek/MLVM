    t->kind = TK_STRING_LIT;
    t->sval.clear();
    do {
        nextCh();
        while (_ch != '"') {
            if (_ch == EOF || _ch == '\n') error(t->line, "unterminated string literal");
            if (_ch == '\\') {
                nextCh();
                unsigned value = 0;
                if (_ch >= '0' && _ch <= '7') {
                    for (unsigned i = 0; i < 3 && _ch >= '0' && _ch <= '7'; ++i) {
                        value = value * 8 + static_cast<unsigned>(_ch - '0'); nextCh();
                    }
                } else if (_ch == 'x') {
                    nextCh();
                    if (!isxdigit(_ch)) error(t->line, "hex escape requires digits");
                    while (isxdigit(_ch)) {
                        value = value * 16 + static_cast<unsigned>(isdigit(_ch) ? _ch - '0' : tolower(_ch) - 'a' + 10);
                        if (value > 255) error(t->line, "escape exceeds one byte");
                        nextCh();
                    }
                } else {
                    switch (_ch) {
                    case 'a': value = 7; break;
                    case 'b': value = 8; break;
                    case 't': value = 9; break;
                    case 'n': value = 10; break;
                    case 'v': value = 11; break;
                    case 'f': value = 12; break;
                    case 'r': value = 13; break;
                    case '\\': case '\'': case '"': case '?': value = static_cast<unsigned>(_ch); break;
                    default: error(t->line, "unsupported string escape");
                    }
                    nextCh();
                }
                if (value > 255) error(t->line, "escape exceeds one byte");
                t->sval.push_back(static_cast<char>(value));
                continue;
            }
            t->sval.push_back(static_cast<char>(_ch));
            nextCh();
        }
        nextCh();
        while (isspace(_ch)) nextCh();
    } while (_ch == '"');
