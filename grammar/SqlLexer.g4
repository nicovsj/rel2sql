lexer grammar SqlLexer;

options {
    language = Cpp;
}

// Whitespace and comments
WS: [ \t\r\n]+ -> skip;
LINE_COMMENT: '--' ~[\r\n]* -> skip;
BLOCK_COMMENT: '/*' .*? '*/' -> skip;

// SQL Keywords (must be before IDENTIFIER)
SELECT: 'SELECT' | 'select';
DISTINCT: 'DISTINCT' | 'distinct';
FROM: 'FROM' | 'from';
WHERE: 'WHERE' | 'where';
GROUP: 'GROUP' | 'group';
BY: 'BY' | 'by';
WITH: 'WITH' | 'with';
AS: 'AS' | 'as';
UNION: 'UNION' | 'union';
ALL: 'ALL' | 'all';
VALUES: 'VALUES' | 'values';
CASE: 'CASE' | 'case';
WHEN: 'WHEN' | 'when';
THEN: 'THEN' | 'then';
END: 'END' | 'end';
EXISTS: 'EXISTS' | 'exists';
IN: 'IN' | 'in';
NOT: 'NOT' | 'not';
AND: 'AND' | 'and';
OR: 'OR' | 'or';
TRUE: 'TRUE' | 'true';
FALSE: 'FALSE' | 'false';
CREATE: 'CREATE' | 'create';
REPLACE: 'REPLACE' | 'replace';
VIEW: 'VIEW' | 'view';
TABLE: 'TABLE' | 'table';

// Aggregate functions
COUNT: 'COUNT' | 'count';
SUM: 'SUM' | 'sum';
AVG: 'AVG' | 'avg';
MIN: 'MIN' | 'min';
MAX: 'MAX' | 'max';

// Identifiers
IDENTIFIER: [a-zA-Z_][a-zA-Z0-9_]*;

// String literals (single quotes with escape sequences)
fragment ESCAPE_SEQ: '\\' ([btnfr\\'] | 'u' [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F] [0-9a-fA-F]);
STRING_LITERAL: '\'' (ESCAPE_SEQ | ~['\\])* '\'';

// Numeric literals
fragment DIGIT: [0-9];
INTEGER_LITERAL: DIGIT+;
FLOAT_LITERAL: DIGIT+ '.' DIGIT* | '.' DIGIT+ | DIGIT+ [eE] [+-]? DIGIT+ | DIGIT+ '.' DIGIT* [eE] [+-]? DIGIT+;

// Operators
EQ: '=';
NEQ: '!=';
LT: '<';
GT: '>';
LTE: '<=';
GTE: '>=';
PLUS: '+';
MINUS: '-';
DIV: '/';

// Punctuation
LPAREN: '(';
RPAREN: ')';
COMMA: ',';
SEMICOLON: ';';
DOT: '.';
// ASTERISK is used for both wildcard (*) and multiplication
// The parser context will determine which one
ASTERISK: '*';

// Error token
ERROR: .;
