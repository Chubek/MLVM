/* Export dparser's tree, never an independently parsed AST. */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "dparse.h"
extern D_ParserTables parser_tables_gram;
static void quoted(const char *s, size_t n) {
  putchar('"');
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') printf("\\%c", c);
    else if (c < 32 || c >= 127) printf("\\u%04x", c);
    else putchar(c);
  }
  putchar('"');
}
static void tree(D_ParseNode *n, char *base) {
  D_Symbol *s = &parser_tables_gram.symbols[n->symbol];
  printf("{\"kind\":"); quoted(s->name, (size_t)s->name_len);
  printf(",\"start\":%td,\"end\":%td,\"children\":[", n->start_loc.s-base, n->end-base);
  int count = d_get_number_of_children(n);
  for (int i = 0; i < count; ++i) {
    if (i) putchar(',');
    tree(d_get_child(n, i), base);
  }
  printf("]}");
}
static void syntax_error(D_Parser *p) {
  fprintf(stderr, "syntax error at line %d, column %d\n", p->loc.line, p->loc.col + 1);
}
static void advance(d_loc_t *loc) {
  if (*loc->s++ == '\n') { ++loc->line; loc->col = 0; }
  else ++loc->col;
}
static void whitespace(D_Parser *p, d_loc_t *loc, void **globals) {
  (void)globals;
  for (;;) {
    if (*loc->s == ' ' || *loc->s == '\t' || *loc->s == '\r' || *loc->s == '\n') {
      advance(loc);
    } else if (loc->s[0] == '/' && loc->s[1] == '/') {
      while (*loc->s && *loc->s != '\n') advance(loc);
    } else if (loc->s[0] == '/' && loc->s[1] == '*') {
      int line = loc->line, col = loc->col + 1;
      advance(loc); advance(loc);
      while (*loc->s && !(loc->s[0] == '*' && loc->s[1] == '/')) advance(loc);
      if (!*loc->s) {
        fprintf(stderr, "unterminated block comment at line %d, column %d\n", line, col);
        ++p->syntax_errors;
        return;
      }
      advance(loc); advance(loc);
    } else return;
  }
}
static int check_lexical_delimiters(D_Parser *p, char *buf) {
  d_loc_t loc = p->loc;
  loc.s = buf;
  int backtick = 0, line = 1, col = 1;
  while (*loc.s) {
    whitespace(p, &loc, NULL);
    if (p->syntax_errors) return 0;
    if (!*loc.s) break;
    if (*loc.s == '`') {
      backtick = !backtick;
      if (backtick) { line = loc.line; col = loc.col + 1; }
    }
    advance(&loc);
  }
  if (backtick) {
    fprintf(stderr, "unterminated backtick operator at line %d, column %d\n", line, col);
    return 0;
  }
  return 1;
}
int main(void) {
  size_t len = 0, cap = 4096;
  char *buf = malloc(cap);
  if (!buf) return 2;
  int c;
  while ((c = getchar()) != EOF) {
    if (len + 1 >= cap) {
      if (cap > INT_MAX / 2) { free(buf); return 2; }
      cap *= 2;
      char *next = realloc(buf, cap);
      if (!next) { free(buf); return 2; }
      buf = next;
    }
    buf[len++] = (char)c;
  }
  buf[len] = 0;
  D_Parser *p = new_D_Parser(&parser_tables_gram, 0);
  p->save_parse_tree = 1;
  p->loc.line = 1;
  p->syntax_error_fn = syntax_error;
  p->initial_white_space_fn = whitespace;
  p->error_recovery = 0;
  if (!check_lexical_delimiters(p, buf)) {
    free_D_Parser(p); free(buf); return 1;
  }
  D_ParseNode *n = dparse(p, buf, (int)len);
  int ok = n && !p->syntax_errors;
  if (!ok && !p->syntax_errors) syntax_error(p);
  if (ok) { tree(n, buf); putchar('\n'); }
  if (n) free_D_ParseNode(p, n);
  free_D_Parser(p);
  free(buf);
  return ok ? 0 : 1;
}
