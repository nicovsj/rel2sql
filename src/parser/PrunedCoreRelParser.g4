parser grammar PrunedCoreRelParser;

options {
	language = Cpp;
	tokenVocab = CoreRelLexer;
}

program: rel_def* EOF;

rel_def: s = 'def' name = T_ID rel_abs;

// ========================================================================================

literal:
	T_INT_LIT																	# int_
	| T_NEG_INT_LIT																# neg_int_
	| T_META_INT_LIT															# meta_int_
	| T_FLOAT_LIT																# float_
	| T_NEG_FLOAT_LIT															# neg_float_
	| T_RELNAME_LIT																# relname_
	| T_RELNAME_STR_LIT															# relname_str_
	| T_RELNAME_MSTR_LIT														# relname_mstr_
	| T_CHAR_LIT																# char_
	| T_STATIC_STR_LIT															# str_
	| T_STATIC_MSTR_LIT															# mstr_
	| T_RAWSTRING_LIT															# rawstr_
	| T_DATE_LIT																# date_
	| T_DATETIME_LIT															# datetime_
	| meh = ('true' | 'false')													# bool_
	| T_QUOTE (interpolation_part | ERR_INVALID_STR_LIT)+ T_QUOTE				# interpol_
	| T_TRIPLE_QUOTE (interpolation_part | ERR_INVALID_STR_LIT)+ T_TRIPLE_QUOTE	# interpol_;

interpolation_part:
	T_STATIC_STR_PART
	| T_STATIC_MSTR_PART T_MIDDLE_QUOTES?
	| T_MIDDLE_QUOTES? T_STATIC_MSTR_PART
	| T_INTERPOL_ID
	| T_INTERPOL_START expr ')';

bindings: binding | bindings ',' binding;

binding: literal | T_ID ('in' id_domain = T_ID)?;

// ========================================================================================

appl_base: T_ID | rel_abs;

appl_param: underscore = '_' | expr;

appl_params: appl_param | appl_params ',' appl_param;

product_inner: expr | product_inner ',' expr;

union_inner: expr | union_inner ';' expr;

rel_abs: '{' union_inner ';'? '}' # union;

// The order of the rules below matters for precedence.
expr:
	literal							# lit_expr
	| T_ID							# id_expr
	| '(' product_inner ','? ')'	# product
	| expr 'where' expr				# condition
	| rel_abs						# rel_abs_expr_rec
	| formula						# formula_expr
	| '[' bindings ']' ':' expr		# bindings_expr
	| '(' bindings ')' ':' formula	# bindings_formula
	| appl_base '[' appl_params ']'	# partial_appl;

formula:
	'{' ('(' ')')? '}'										# rel_abs_true_false
	| appl_base '(' appl_params? ')'						# full_appl
	| formula op = 'and' formula							# binop
	| formula op = 'or' formula								# binop
	| op = 'not' formula									# unop
	| op = 'exists' '(' '(' bindings ')' '|' formula ')'	# quantification
	| op = 'forall' '(' '(' bindings ')' '|' formula ')'	# quantification
	| '(' formula ')'										# paren;
