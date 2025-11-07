parser grammar SqlParser;

options {
	language = Cpp;
	tokenVocab = SqlLexer;
}

// Top-level entry point
statements: statement (SEMICOLON statement)* SEMICOLON? EOF;

statement: select | values | unionClause | createView | createTable;

// WITH clause (CTEs) - can precede SELECT
with: WITH cte (COMMA cte)*;

cte:
	cteName = IDENTIFIER (LPAREN columnList RPAREN)? AS LPAREN select RPAREN;

columnList: IDENTIFIER (COMMA IDENTIFIER)*;

// SELECT statement
select:
	with? SELECT DISTINCT? selectList from? where? groupBy?
	| with? SELECT DISTINCT? selectList;

selectList: selectItem (COMMA selectItem)*;

selectItem:
	term (AS alias = IDENTIFIER)?	# selectTerm
	| wildcard						# selectWildcard;

wildcard:
	ASTERISK								# simpleWildcard
	| tableAlias = IDENTIFIER DOT ASTERISK	# qualifiedWildcard;

// FROM clause
from: FROM source (COMMA source)*;

source:
	tableName = IDENTIFIER (AS alias = IDENTIFIER)?	# tableSource
	| LPAREN select RPAREN sourceAlias?	# subquerySource
	| LPAREN values RPAREN sourceAlias?	# valuesSource;

sourceAlias: AS alias = IDENTIFIER (LPAREN columnList RPAREN)?;

// WHERE clause
where: WHERE condition;

// GROUP BY clause
groupBy: GROUP BY groupByItem (COMMA groupByItem)*;

groupByItem: term | wildcard;

// UNION statements
unionClause:
	select UNION ALL select (UNION ALL select)*	# unionAll
	| select UNION select ( UNION select)*		# unionSimple;

// VALUES clause
values: VALUES valueRow (COMMA valueRow)*;

valueRow: LPAREN constant (COMMA constant)* RPAREN;

// Conditions (WHERE clause expressions)
condition:
	NOT condition					# notCondition
	| condition AND condition		# andCondition
	| condition OR condition		# orCondition
	| term comparisonOp term		# comparisonCondition
	| EXISTS LPAREN select RPAREN	# existsCondition
	| inclusion						# inclusionCondition
	| LPAREN condition RPAREN		# parenCondition;

inclusion: (term | LPAREN columnRef (COMMA columnRef)+ RPAREN) NOT? IN LPAREN select RPAREN;

comparisonOp: EQ | NEQ | LT | GT | LTE | GTE;

// Terms (expressions that can be used in SELECT, WHERE, etc.)
term:
	constant						# constantTerm
	| columnRef						# columnTerm
	| aggregateFunction				# functionTerm
	| caseWhenExpr					# caseTerm
	| term (ASTERISK | DIV) term	# multDivTerm
	| term (PLUS | MINUS) term		# addSubTerm
	| LPAREN term RPAREN			# parenTerm
	| LPAREN select RPAREN			# subqueryTerm; // scalar subquery

columnRef:
	columnName = IDENTIFIER									# simpleColumn
	| tableAlias = IDENTIFIER DOT columnName = IDENTIFIER	# qualifiedColumn;

aggregateFunction:
	COUNT LPAREN (term | ASTERISK) RPAREN	# countFunction
	| SUM LPAREN term RPAREN				# sumFunction
	| AVG LPAREN term RPAREN				# avgFunction
	| MIN LPAREN term RPAREN				# minFunction
	| MAX LPAREN term RPAREN				# maxFunction;

caseWhenExpr: CASE whenClause+ END;

whenClause: WHEN condition THEN term;

// CREATE statements
createView: CREATE OR REPLACE VIEW viewName = IDENTIFIER (LPAREN columnList RPAREN)? AS LPAREN select RPAREN;

createTable: CREATE TABLE tableName = IDENTIFIER (LPAREN columnList RPAREN)? AS LPAREN select RPAREN;

// Constants
constant:
	INTEGER_LITERAL		# intConstant
	| FLOAT_LITERAL		# floatConstant
	| STRING_LITERAL	# stringConstant
	| TRUE				# trueConstant
	| FALSE				# falseConstant;
