parser grammar PrunedCoreRelParser;

options {
	language = Cpp;
	tokenVocab = CoreRelLexer;
}

program: relDef* EOF;

relDef: s = 'def' name = T_ID relAbs;

// ========================================================================================

literal:
	T_INT_LIT																	# int
	| T_NEG_INT_LIT																# negInt
	| T_META_INT_LIT															# metaInt
	| T_FLOAT_LIT																# float
	| T_NEG_FLOAT_LIT															# negFloat
	| T_RELNAME_LIT																# relName
	| T_RELNAME_STR_LIT															# relNameStr
	| T_RELNAME_MSTR_LIT														# relNameMstr
	| T_CHAR_LIT																# Char
	| T_STATIC_STR_LIT															# Str
	| T_STATIC_MSTR_LIT															# Mstr
	| T_RAWSTRING_LIT															# Rawstr
	| T_DATE_LIT																# Date
	| T_DATETIME_LIT															# Datetime
	| meh = ('true' | 'false')													# bool
	| T_QUOTE (interpolationPart | ERR_INVALID_STR_LIT)+ T_QUOTE				# interpol
	| T_TRIPLE_QUOTE (interpolationPart | ERR_INVALID_STR_LIT)+ T_TRIPLE_QUOTE	# interpol;

interpolationPart:
	T_STATIC_STR_PART
	| T_STATIC_MSTR_PART T_MIDDLE_QUOTES?
	| T_MIDDLE_QUOTES? T_STATIC_MSTR_PART
	| T_INTERPOL_ID
	| T_INTERPOL_START expr ')';

bindingInner: bindings = binding (',' bindings = binding)*;

binding: literal | id = T_ID ('in' id_domain = T_ID)?;

// ========================================================================================

applBase: T_ID | relAbs;

applParam: underscore = '_' | expr;

applParams: params = applParam (',' params = applParam)*;

productInner: (exprs = expr (',' exprs = expr)*)?;

relAbs: '{' (exprs = expr (';' exprs = expr)*)? '}';

operator: T_OP_PLUS | T_OP_MINUS | T_OP_MULT | T_OP_DIV;

comparator: T_OP_COMP | T_OP_NEQ | T_OP_EQ;

numericalConstant:
	T_INT_LIT  # numInt
	| T_NEG_INT_LIT # numNegInt
	| T_FLOAT_LIT # numFloat
	| T_NEG_FLOAT_LIT # numNegFloat;

term:
	T_ID								              # IDTerm
	| numericalConstant					      # numTerm
	| lhs = term operator rhs = term	# opTerm;

// The order of the rules below matters for precedence.
expr:
	literal								                   # litExpr
	| T_ID								                   # IDExpr
	| '(' productInner ')'                   # productExpr
	| lhs = expr 'where' rhs = formula	     # conditionExpr
	| relAbs							                   # relAbsExpr
	| formula							                   # formulaExpr
	| '[' bindingInner ']' ':' expr		       # bindingsExpr
	| '(' bindingInner ')' ':' formula	     # bindingsFormula
	| applBase '[' applParams ']'		         # partialAppl;

formula:
	'{' ('(' ')')? '}'									              # formulaBool
	| applBase '(' applParams? ')'						        # FullAppl
	| lhs = formula op = 'and' rhs = formula			    # binOp
	| lhs = formula op = 'or' rhs = formula				    # binOp
	| op = 'not' formula								              # unOp
	| op = 'exists' '(' '(' bindingInner ')' '|' formula ')'	# quantification
	| op = 'forall' '(' '(' bindingInner ')' '|' formula ')'	# quantification
	| '(' formula ')'									                # paren
	| lhs = term comparator rhs = term					      # comparison;
