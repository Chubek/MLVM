{ static int identifier_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
} }
file: kernel declaration* rule*;
kernel: kernel_kw identifier inheritance? ';';
kernel_kw: 'Kernel' [ if (identifier_char(*$n.end)) ${reject}; ];
inherits_kw: 'inherits' [ if (identifier_char(*$n.end)) ${reject}; ];
concerns_kw: 'concerns' [ if (identifier_char(*$n.end)) ${reject}; ];
requires_kw: 'requires' [ if (identifier_char(*$n.end)) ${reject}; ];
where_kw: 'where' [ if (identifier_char(*$n.end)) ${reject}; ];
inheritance: '(' inherits_kw qualified (',' qualified)* ')';
declaration: concern | require;
concern: concerns_kw qualified ';';
require: requires_kw qualified group? ';';
group: '::' '{' identifier (',' identifier)* '}';
qualified: identifier ('::' identifier)*;
rule: identifier '::' pattern '->' identifier where_kw guard '{' match '~>' build ';' '}';
pattern: 'Binary' '(' pattern ',' pattern ')' | 'Term' '!' refinement;
refinement: identifier | identifier '(' integer ')' | identifier '(' capture ')';
guard: guard_atom ('&&' guard_atom)*;
guard_atom: identifier arguments? | '(' guard ')';
arguments: '(' capture (',' capture)* ')';
match: match_atom (operator match_atom)*;
match_atom: capture | '(' match ')';
build: build_atom (operator build_atom)*;
build_atom: capture | integer | '(' build ')';
operator: '`' identifier '`';
capture: '$' digits;
integer: '-'? digits;
identifier: "[A-Za-z_][A-Za-z0-9_]*";
digits: "[0-9]+";
