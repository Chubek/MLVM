#ifndef BLACKSTONE_PROFILE_LEXER_H
#define BLACKSTONE_PROFILE_LEXER_H
/* The source lexer/parser is grammar/blackstone.g + src/parser.c. Profile IR
 * retains decimal spelling; these helpers validate without integer conversion. */
int bs_identifier_valid(const char *);
int bs_qualified_valid(const char *);
int bs_decimal_valid(const char *);
int bs_decimal_equal(const char *, const char *);
#endif
