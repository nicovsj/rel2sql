grammar rel;

// Parser rules

prog:   stat+ ;

stat:   expr NEWLINE                # printExpr
    |   ID '=' expr NEWLINE         # assign
    |   NEWLINE                     # blank
    ;

expr:   expr op=('*'|'/') expr      # MulDiv
    |   expr op=('+'|'-') expr      # AddSub
    |   INT                         # int
    |   ID                          # id
    |   '(' expr ')'                # parens
    ;

// Lexer rules

ID:     [a-zA-Z]+ ;                // match identifiers
INT:    [0-9]+ ;                   // match integers

NEWLINE: [\r\n]+ ;                 // match newlines
WS:     [ \t]+ -> skip ;           // skip spaces and tabs
