parser grammar RelParser;
options { tokenVocab=RelLexer; }

program
    : declaration* EOF ;

declaration
    : rel_def
    | rel_decl
    | value_type
    | constraint
    | module
    | use_decl
    | namespace_
    | import_decl
    ;

rel_def
    : prelude s='def' name=lhs_id rel_abs ;

rel_decl
    : prelude s='declare' name=lhs_id ('(' bindings ')' 'requires' expr)? ;

value_type
    : prelude s='value' 'type' name=id_ext rel_abs? ;

constraint
    : prelude s='ic' name=id_ext? '(' bindings? ')' 'requires' expr ;

module
    : prelude s='module' name=id_ext ('[' bindings ']')* declaration* 'end' ;

use_decl
    : 'with' expr 'use' import_from_module (',' import_from_module)* declaration* 'end' ;

import_decl
    : 'from' id_ext 'import' import_from_ns (',' import_from_ns)* ;

// `namespace` is used by ANTLR/C++
namespace_
    : prelude s='namespace' name=id_ext declaration* 'end' ;

// ========================================================================================

prelude
    : T_DOCSTRING_LIT? attribute* ;

attribute
    : T_AT_ID ('(' (literal (',' literal)*)? ')')? ;

import_from_module
    : name=id_or_op ('as' alias=id_or_op)? ;

import_from_ns
    : name=id_or_op ('as' alias=id_or_op)? | T_DOTS ;

id_or_op
    : id | operator_ ;

id
    : T_ID | 'value' | 'type';

// TODO merge with lhs_id and error in logic
id_ext
    : id | T_NS_ID ;

lhs_id
    : id_ext | '(' operator_ ')' | 'iff' | 'xor' | 'implies' ;

// `operator` is used by ANTLR
operator_
    : '.' | '^' | '%' | '*' | '/' | '÷' | '-' | '+' | '×' | '∩' | '∪' | T_OP_SUB_SUP
    | T_OP_EQ | T_OP_NEQ | T_OP_COMP | '≡' | '≢' | '<:' | ':>' | '<++' | '++>' | '++'
    | T_LOOSE_EQ | T_LOOSE_NEQ | T_OP_MATH | T_OP_CUSTOM
    ;

literal
    : T_INT_LIT                                        # int_
    | T_NEG_INT_LIT                                    # neg_int_
    | T_META_INT_LIT                                   # meta_int_
    | T_FLOAT_LIT                                      # float_
    | T_NEG_FLOAT_LIT                                  # neg_float_
    | T_RELNAME_LIT                                    # relname_
    | T_RELNAME_STR_LIT                                # relname_str_
    | T_RELNAME_MSTR_LIT                               # relname_mstr_
    | T_CHAR_LIT                                       # char_
    | T_STATIC_STR_LIT                                 # str_
    | T_STATIC_MSTR_LIT                                # mstr_
    | T_RAWSTRING_LIT                                  # rawstr_
    | T_DATE_LIT                                       # date_
    | T_DATETIME_LIT                                   # datetime_
    | meh=('true' | 'false')                               # bool_
    | T_QUOTE (interpolation_part | ERR_INVALID_STR_LIT)+ T_QUOTE                # interpol_
    | T_TRIPLE_QUOTE (interpolation_part | ERR_INVALID_STR_LIT)+ T_TRIPLE_QUOTE  # interpol_
    ;

interpolation_part
    : T_STATIC_STR_PART
    | T_STATIC_MSTR_PART T_MIDDLE_QUOTES?
    | T_MIDDLE_QUOTES? T_STATIC_MSTR_PART
    | T_INTERPOL_ID
    | T_INTERPOL_START expr ')'
    ;

bindings
    : var_decl | bindings ',' var_decl ;

var_decl
    : literal
    | '{' higher=id '}'
    | id ('in' id_domain=id_ext)?
    | T_VARARGS_ID
    ;

// ========================================================================================

appl_base
    : id_ext | rel_abs ;

appl_param
    : underscore='_' | underscore=T_UNDERSCORE_VARARGS | expr ;

appl_params
    : appl_param | appl_params ',' appl_param ;

product_inner
    : expr | product_inner ',' expr ;

union_inner
    : expr | union_inner ';' expr ;

rel_abs
    : '[' bindings? ']' ':' expr                       # rel_abs_bindings
    | '(' bindings? ')' ':' expr                       # rel_abs_bindings
    | '{' ('(' ')')? '}'                               # rel_abs_true_false
    | '{' union_inner ';'? '}'                         # union
    ;

// The order of the rules below matters for precedence.
expr
    : literal                                          # lit_expr
    | id_ext                                           # id_expr
    | T_VARARGS_ID                                     # id_expr
    | '{' operator_ '}'                                # op_expr
    | '(' product_inner ','? ')'                       # product

    | appl_base '[' appl_params ']'                    # partial_appl
    | appl_base '(' appl_params? ')'                   # full_appl

    | expr op='.' expr                                 # binop
    | <assoc=right> expr op='^' expr                   # binop
    | expr op='%' expr                                 # binop
    | expr op=('*' | '/' | '÷' | '×') expr             # binop
    | expr op=('-' | '+') expr                         # binop
    // Negative literals bind stronger to '-' and literal. Handle the subtraction case here.
    | expr (T_NEG_INT_LIT | T_NEG_FLOAT_LIT)           # neg_subtraction
    | T_INVALID_DATE_LIT_EXPR                          # invalid_date_lit
    | expr op='++' expr                                # binop
    | expr op=('∩' | '∪') expr                         # binop
    | expr op=T_OP_SUB_SUP expr                        # binop
    | expr op=(T_OP_EQ | T_OP_NEQ | T_OP_COMP) expr    # binop
    | expr op=(T_LOOSE_EQ | T_LOOSE_NEQ) expr          # binop
    | expr op=(T_OP_MATH | T_OP_CUSTOM) expr           # binop
    | op='not' expr                                    # unop
    | expr op='and' expr                               # binop
    | expr op='or' expr                                # binop
    | <assoc=right> expr op='implies' expr             # binop
    | expr op=('iff' | 'xor') expr                     # binop
    | expr op=('<:' | ':>') expr                       # binop
    | expr op=('<++' | '++>') expr                     # binop
    | expr op=('≡' | '≢') expr                         # binop

    | expr 'where' expr                                # condition
    | op='forall' '(' '(' bindings ')' '|' expr ')'    # quantification
    | op='exists' '(' '(' bindings ')' '|' expr ')'    # quantification
    | rel_abs                                          # rel_abs_expr_rec
    ;
