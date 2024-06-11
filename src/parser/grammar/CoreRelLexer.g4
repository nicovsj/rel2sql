lexer grammar CoreRelLexer;

options {
    language = Cpp;
}

// Naming conventions:
// * K_*: Keyword
// * T_*: Token
// * F_*: Fragments (i.e., parts of other tokens)
// * M_*: Mode
// * ERR*: Errors
// * X_*: Skipped or "internal"
// * STR_*: Single line string
// * MSTR_*: Multiple line string

// Extra tokens that are used indirectly.
tokens {
    T_INTERPOL_ID, T_INTERPOL_START,
    ERR_INVALID_STR_LIT, ERR_NONTERM_STR_LIT,
    T_DOCSTRING_LIT, T_RAWSTRING_LIT
}

@header {
    #include <stack>
}

@members {
    // Keep track of whether we are at the first character in a multiline string interpolation.
    // This is to disallow quotes at the beginning of the string.
    bool first_char;
    // Keep track of whether quotes have been encountered in the middle of an interpolation.
    // This is to disallow more than two continuous quotes in the middle.
    bool middle_quotes;
    // Count the number of opened parentheses,
    // in each interpolation level of an interpolated string.
    std::stack<int> open_parens;
    // Count the delimiters at the beginning and the end of a raw string literal.
    int raw_start_count;
    int raw_end_count;
    // Docstring and Rawstring are handled the same. Need to keep track of the prefix.
    std::string str_prefix;
    // Helper methods.
    void rawlike_str_start(std::string prefix) {
        raw_start_count = 1;
        raw_end_count = 0;
        str_prefix = prefix;
    }
    void rawlike_str_type() {
        setType(str_prefix == "doc" ? T_DOCSTRING_LIT : T_RAWSTRING_LIT);
    }
}

X_WS: [ \t\r\n]+ -> skip;
X_LINE_COMMENT: '//' ~[\r\n]* -> skip;
X_BLOCK_COMMENT: '/*' ( ~('*') | '*'+ ~('/' | '*') )* '*'+ '/' -> skip;

// NOTE: must be defined before T_ID so they are not matched as such.
K_and: 'and';
K_as: 'as';
K_declare: 'declare';
K_def: 'def';
K_end: 'end';
K_exists: 'exists';
K_false: 'false';
K_forall: 'forall';
K_from: 'from';
K_ic: 'ic';
K_iff: 'iff';
K_implies: 'implies';
K_import: 'import';
K_in: 'in';
K_module: 'module';
K_namespace: 'namespace';
K_not: 'not';
K_or: 'or';
K_requires: 'requires';
K_true: 'true';
K_use: 'use';
K_where: 'where';
K_with: 'with';
K_xor: 'xor';
// Soft keywords can be used as identifiers too.
K_SOFT_VALUE: 'value';
K_SOFT_TYPE: 'type';

fragment F_OPERATOR
    : '.' | '^' | '%' | '*' | '/' | '÷' | '-' | '+' | '×' | '∩' | '∪' | T_OP_SUB_SUP
    | T_OP_EQ | T_OP_NEQ | T_OP_COMP | '≡' | '≢' | '<:' | ':>' | '<++' | '++>' | '++'
    | T_LOOSE_EQ | T_LOOSE_NEQ | T_OP_MATH | T_OP_CUSTOM
    ;

fragment F_ID: [a-zA-Zα-ωΑ-Ω] | [a-zA-Zα-ωΑ-Ω_] [a-zA-Zα-ωΑ-Ω_0-9]+;
fragment F_VT_ID: '^' F_ID;
fragment F_PAREN_OP: '(' F_OPERATOR ')';
fragment F_ID_EXT: F_ID | F_VT_ID;
fragment F_ID_EXTMORE: F_ID_EXT | F_PAREN_OP;
T_ID: F_ID_EXT;
T_AT_ID: '@' F_ID;
T_NS_ID
    : '::'? (F_ID_EXT '::')* F_ID_EXT
    // To allow a parenthesised operator (as an ID), some :: needs to be present.
    | '::' (F_ID_EXTMORE '::')* F_ID_EXTMORE
    | '::'? (F_ID_EXTMORE '::')+ F_ID_EXTMORE
    | '::'? (F_ID_EXTMORE '::')* F_ID_EXTMORE
    ;

T_VARARGS_ID: F_ID ('...' | '…');

// Mirroring the Python PEP for integer/float literals with optional separators.
// https://peps.python.org/pep-0515/
// Extend, to allow for leading zeros.
fragment DEC: [0-9];
fragment OCT: [0-7];
fragment HEX: [0-9a-fA-F];
fragment DEC_INT: '0'* [1-9] ('_'? DEC)* | '0' ('_'? '0') *;
fragment OCT_INT: '0o' ('_'? OCT)+;
fragment HEX_INT: '0x' ('_'? HEX)+;
T_META_INT_LIT: '#' (DEC_INT | OCT_INT | HEX_INT);
T_INT_LIT: DEC_INT | OCT_INT | HEX_INT;
T_NEG_INT_LIT: '-' DEC_INT;
// Have to specifically handle this as a token, otherwise they will parse as "-0" and "x..."
ERR_INVALID_NEG_OCT_HEX: '-' (OCT_INT | HEX_INT);

fragment EXPONENT: [eE] [+-]? DEC_INT;
fragment POINT_FLOAT: DEC_INT? '.' DEC ('_'? DEC)* | DEC_INT '.';
fragment EXPONENT_FLOAT: (DEC_INT | POINT_FLOAT) EXPONENT;
T_FLOAT_LIT: POINT_FLOAT | EXPONENT_FLOAT;
T_NEG_FLOAT_LIT: '-' (POINT_FLOAT | EXPONENT_FLOAT);

T_DATE_LIT: DEC DEC DEC DEC '-' DEC DEC '-' DEC DEC;
fragment F_TIME_DIGITS: DEC DEC ':' DEC DEC ':' DEC DEC ('.' DEC DEC DEC)?;
fragment F_TIMEZONE: 'Z' | [+-] DEC DEC ':' DEC DEC;
T_DATETIME_LIT: T_DATE_LIT 'T' F_TIME_DIGITS F_TIMEZONE;

// Identify some expression patterns that might seem like date literals,
// but are not, in order to report a warning.
fragment F_INVALID_MONTH_OR_DAY
    : DEC '-' DEC DEC
    | DEC DEC '-' DEC
    | DEC '-' DEC
    ;
T_INVALID_DATE_LIT_EXPR
    : DEC DEC DEC DEC '-' F_INVALID_MONTH_OR_DAY
    | F_INVALID_MONTH_OR_DAY '-' DEC DEC DEC DEC
    | DEC DEC '-' DEC DEC '-' DEC DEC DEC DEC
    ;

T_RELNAME_LIT: ':' '^'? (F_ID | '_') | ':[]';

fragment F_ESCAPABLE
    : ['"\\abefnrtv0-7%]
    | OCT OCT
    | [0123] OCT OCT
    | 'x' HEX HEX
    | 'u' HEX HEX HEX HEX
    | 'U' HEX HEX HEX HEX HEX HEX HEX HEX
    ;
fragment F_ESC: '\\' F_ESCAPABLE;

fragment F_CHAR_SEQ: F_ESC | ~[\\'\r\n];
fragment F_STR_SEQ:  F_ESC | ~[\\%"\r\n];
fragment F_MSTR_SEQ: F_ESC | ~[\\%"];

T_CHAR_LIT: '\'' F_CHAR_SEQ '\'';
// Any escape sequence (or other character in general) not matched above.
ERR_INVALID_CHAR_LIT: '\'' (~[\\'\r\n] | '\\' .)* '\'';
ERR_NONTERM_CHAR_LIT: '\'' F_CHAR_SEQ* ([\r\n] | EOF);

T_STATIC_STR_LIT: '"' F_STR_SEQ* '"';
// Any escape sequence (or other character in general) not matched above.
X_ERR_1: '"' (~[\\%"\r\n] | '\\' .)* '"' -> type(ERR_INVALID_STR_LIT);
X_ERR_2: '"' F_STR_SEQ* ([\r\n] | EOF) -> type(ERR_NONTERM_STR_LIT);

// A multiline string has either no internal quotes,
// or if it does they have to be surrounded by some amount of non-quotes.
T_STATIC_MSTR_LIT
    : '"""' F_MSTR_SEQ* '"""'
    | '"""' F_MSTR_SEQ+ (('"' | '""') F_MSTR_SEQ+)+ '"""'
    ;
X_ERR_3: '"""' ('"' | '""')? ~[%"] (~[%"])*? '"""' -> type(ERR_INVALID_STR_LIT);
X_ERR_4: '"""' ('"'? '"'? (~["\\] | F_ESC))* EOF -> type(ERR_NONTERM_STR_LIT);

T_DOCSTRING_START: 'doc"' { rawlike_str_start("doc"); } -> more, pushMode(M_RAW_PRELUDE);

T_RAW_START: 'raw"' { rawlike_str_start("raw"); } -> more, pushMode(M_RAW_PRELUDE);

T_RELNAME_STR_LIT: ':' T_STATIC_STR_LIT;
T_RELNAME_MSTR_LIT: ':' T_STATIC_MSTR_LIT;

ERR_INVALID_INTERPOL_RELNAME
    : ':"' (~'\\'? '%' | ~[%"])* '"'
    | ':"""' ( '"'? '"'? (~["\\] | F_ESC) )* '"""'
    ;

// ========================================================================================

T_UNDERSCORE_VARARGS: '_...' | '_…';
T_UNDERSCORE: '_';
T_OP_DOT: '.';
T_OP_POWER: '^';
T_OP_MOD: '%';
T_OP_MULT: '*';
T_OP_DIV: '/';
T_OP_TRUNC_DIV: '÷'; // U+00F7 - Division sign
T_OP_MINUS: '-';
T_OP_PLUS: '+';
T_OP_EQ: '=';
// U+2260 - Not equal
T_OP_NEQ: '!=' | '≠' ;
T_OP_COMP: '<' | '<=' | '≤' | '>' | '>=' | '≥' ;
T_OP_CART: '×';      // U+00DF - Multiplication sign
T_OP_INTERSECT: '∩'; // U+2229 - Intersection
T_OP_UNION: '∪';     // U+222a - Union
// U+2286 - Subset or eq, U+2287 - Superset or eq
T_OP_SUB_SUP: '⊂' | '⊃' | '⊆' | '⊇' ;
T_TRIPLE_EQ: '≡';
T_TRIPLE_NEQ: '≢';
T_RIGHT_RESTRICT: ':>';
T_LEFT_RESTRICT: '<:';
T_RIGHT_OVERRIDE: '++>';
T_LEFT_OVERRIDE: '<++';
T_OP_CONCAT: '++';
T_LOOSE_EQ: '~=';
T_LOOSE_NEQ: '!~=';
T_PIPE: '|';
T_LBRACKET: '[';
T_RBRACKET: ']';
T_LBRACE: '{';
T_RBRACE: '}';
T_DOTS: '...' | '…';
T_COMMA: ',';
T_SEMICOLON: ';';
T_COLON: ':';
T_DOUBLE_COLON: '::';
T_HASH: '#';
// Should be defined after the explicit symbols, in case any other symbol above falls into this range.
T_OP_MATH: [\u2200-\u22FF];
T_OP_CUSTOM: '¬' | '→' | '←' | '⇒' | '⇐' | '⇔' | '⇎' ;

// ========================================================================================

T_TRIPLE_QUOTE: '"""' {
    first_char = true;
    middle_quotes = false;
} -> pushMode(M_MULTI_INTERPOLATION);
T_QUOTE: '"' -> pushMode(M_INTERPOLATION);

T_LPAREN: '(' {
    if (!open_parens.empty())
        open_parens.top()++;
};

T_RPAREN: ')' {
    if (!open_parens.empty()) {
        if (open_parens.top() == 1) {
            popMode();
            open_parens.pop();
        } else {
            open_parens.top()--;
        }
    }
};

// Errors caught here - must be the final definition.
ERROR: .;

// ========================================================================================

mode M_INTERPOLATION;

X_QUOTE_10: '"' -> type(T_QUOTE), popMode;

T_STATIC_STR_PART: F_STR_SEQ+;

X_INTERPOL_ID_10: '%' F_ID -> type(T_INTERPOL_ID);
X_INTERPOL_START_10: '%(' {
    open_parens.push(1);
} -> type(T_INTERPOL_START), pushMode(DEFAULT_MODE);

X_ERR_10: ([\r\n] | EOF) -> type(ERR_NONTERM_STR_LIT), popMode;
X_ERR_15: (('\\' .) | .) -> type(ERR_INVALID_STR_LIT);

// ========================================================================================

mode M_MULTI_INTERPOLATION;

X_QUOTE_20: '"""' -> type(T_TRIPLE_QUOTE), popMode;

T_MIDDLE_QUOTES: { !first_char && !middle_quotes; }? '"' '"'? { middle_quotes = true; };
T_STATIC_MSTR_PART: (~[%"\\] | F_ESC)+ { first_char = middle_quotes = false; };

X_INTERPOL_ID_20: '%' F_ID { first_char = middle_quotes = false; } -> type(T_INTERPOL_ID);
X_INTERPOL_START_20: '%(' {
    open_parens.push(1);
    first_char = middle_quotes = false;
} -> type(T_INTERPOL_START), pushMode(DEFAULT_MODE);

X_ERR_20: EOF -> type(ERR_NONTERM_STR_LIT), popMode;
X_ERR_25: (('\\' .) | .) -> type(ERR_INVALID_STR_LIT);

// ========================================================================================

mode M_RAW_PRELUDE;

// Only odd number of delimiters are allowed.
X_QUOTE_30: '""' { raw_start_count += 2; } -> more;
X_QUOTE_35: '"' {
    // Allow for `raw""` (when `count` == 1).
    if (raw_start_count == 1) rawlike_str_type();
    else setType(ERROR);
} -> popMode;
X_OTHER_30: . -> more, popMode, pushMode(M_RAW_CONTENT);

// ========================================================================================

mode M_RAW_CONTENT;

X_QUOTE_40: '"' {
    raw_end_count = 1;
    if (raw_start_count == 1) {
        popMode(); // M_RAW_CONTENT
        rawlike_str_type();
    } else {
        more();
        pushMode(M_RAW_MAY_END);
    }
};
X_OTHER_40: . -> more;

// ========================================================================================

mode M_RAW_MAY_END;

X_QUOTE_50: '"' {
    raw_end_count++;
    if (raw_end_count == raw_start_count) {
        popMode(); // M_RAW_MAY_END
        popMode(); // M_RAW_CONTENT
        rawlike_str_type();
    } else
        more();
};
X_OTHER_50: . { raw_end_count = 0; } -> more, popMode;
