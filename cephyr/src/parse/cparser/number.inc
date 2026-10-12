    std::string spelling;
    while (isalnum(_ch) || _ch == '.' || _ch == '_') {
        spelling.push_back(static_cast<char>(_ch));
        nextCh();
    }
    auto digits = std::string_view(spelling);
    int base = 10;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        base = 16; digits.remove_prefix(2);
    } else if (digits.size() > 1 && digits[0] == '0') base = 8;
    std::size_t suffix = digits.size();
    while (suffix && (digits[suffix - 1] == 'u' || digits[suffix - 1] == 'U' ||
                      digits[suffix - 1] == 'l' || digits[suffix - 1] == 'L')) --suffix;
    std::string tail(digits.substr(suffix));
    // A long-long suffix must use the same case for both letters.
    if (tail.find("lL") != std::string::npos || tail.find("Ll") != std::string::npos)
        error(t->line, "invalid integer suffix");
    for (char &c : tail) c = static_cast<char>(tolower(c));
    if (tail != "" && tail != "u" && tail != "l" && tail != "ul" && tail != "lu" &&
        tail != "ll" && tail != "ull" && tail != "llu") error(t->line, "invalid integer suffix");
    bool is_unsigned = tail.find('u') != std::string::npos;
    unsigned width = tail.find('l') != std::string::npos ? 64 : 32;
    const auto number = digits.substr(0, suffix);
    unsigned long long value = 0;
    auto result = std::from_chars(number.data(), number.data() + number.size(), value, base);
    if (number.empty() || result.ec != std::errc{} || result.ptr != number.data() + number.size())
        error(t->line, "unsupported or out-of-range integer literal");
    if (width == 32 && value > (is_unsigned ? UINT_MAX : INT_MAX)) {
        if (base != 10 && !is_unsigned && value <= UINT_MAX) is_unsigned = true;
        else width = 64;
    }
    if (width == 64 && !is_unsigned && value > LLONG_MAX) {
        if (base == 10) error(t->line, "out-of-range signed integer literal");
        is_unsigned = true;
    }
    t->kind = TK_INT_LIT;
    t->sval = spelling;
    t->info.ival = static_cast<long long>(value);
    t->integer_unsigned = is_unsigned;
    t->integer_width = width;
