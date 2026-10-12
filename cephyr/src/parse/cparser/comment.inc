    for (;;) {
        if (_ch == EOF) error(_line, "unterminated block comment");
        if (_ch == '*') {
            nextCh();
            if (_ch == '/') { nextCh(); return; }
        } else nextCh();
    }
