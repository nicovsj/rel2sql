parser grammar PrunedCoreRelParser;

options {
	language = Cpp;
	tokenVocab = CoreRelLexer;
}

program: relDef* EOF;

relDef: s = 'def' name = T_ID relAbs;

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
	| T_QUOTE (interpolationPart | ERR_INVALID_STR_LIT)+ T_QUOTE				# interpol_
	| T_TRIPLE_QUOTE (interpolationPart | ERR_INVALID_STR_LIT)+ T_TRIPLE_QUOTE	# interpol_;

interpolationPart:
	T_STATIC_STR_PART
	| T_STATIC_MSTR_PART T_MIDDLE_QUOTES?
	| T_MIDDLE_QUOTES? T_STATIC_MSTR_PART
	| T_INTERPOL_ID
	| T_INTERPOL_START expr ')';

bindingInner: bindings=binding (',' bindings=binding)*;

binding: literal | id=T_ID ('in' id_domain = T_ID)?;

// ========================================================================================

applBase: T_ID | relAbs;

applParam: underscore = '_' | expr;

applParams: params = applParam (',' params = applParam)*;

productInner: (exprs = expr (',' exprs = expr)*)?;

relAbs: '{' (exprs = expr (';' exprs = expr)*)? '}';

// The order of the rules below matters for precedence.
expr:
	literal							# litExpr
	| T_ID							# IDExpr
	| '(' productInner ')'			# productExpr
	| lhs = expr 'where' rhs = expr	# conditionExpr
	| relAbs						# relAbsExpr
	| formula						# formulaExpr
	| '[' bindingInner ']' ':' expr		# bindingsExpr
	| '(' bindingInner ')' ':' formula	# bindingsFormula
	| applBase '[' applParams ']'	# partialAppl;

formula:
	'{' ('(' ')')? '}'										# formulaBool
	| applBase '(' applParams? ')'							# FullAppl
	| lhs = formula op = 'and' rhs = formula				# binOp
	| lhs = formula op = 'or' rhs = formula					# binOp
	| op = 'not' formula									# unOp
	| op = 'exists' '(' '(' bindingInner ')' '|' formula ')'	# quantification
	| op = 'forall' '(' '(' bindingInner ')' '|' formula ')'	# quantification
	| '(' formula ')'										# paren;
