-- Wide TPC-H relations over the benchmark database's tpch_* tables. The translation names the
-- columns of a relation A1, A2, ..., so each view renames the table's columns, in order.

CREATE OR REPLACE VIEW w_lineitem AS SELECT l_orderkey AS A1, l_partkey AS A2, l_suppkey AS A3, l_linenumber AS A4, l_quantity AS A5, l_extendedprice AS A6, l_discount AS A7, l_tax AS A8, l_returnflag AS A9, l_linestatus AS A10, l_shipdate AS A11, l_commitdate AS A12, l_receiptdate AS A13, l_shipinstruct AS A14, l_shipmode AS A15, l_comment AS A16 FROM tpch_lineitem;
CREATE OR REPLACE VIEW w_orders AS SELECT o_orderkey AS A1, o_custkey AS A2, o_orderstatus AS A3, o_totalprice AS A4, o_orderdate AS A5, o_orderpriority AS A6, o_clerk AS A7, o_shippriority AS A8, o_comment AS A9 FROM tpch_orders;
CREATE OR REPLACE VIEW w_customer AS SELECT c_custkey AS A1, c_name AS A2, c_address AS A3, c_nationkey AS A4, c_phone AS A5, c_acctbal AS A6, c_mktsegment AS A7, c_comment AS A8 FROM tpch_customer;
CREATE OR REPLACE VIEW w_part AS SELECT p_partkey AS A1, p_name AS A2, p_mfgr AS A3, p_brand AS A4, p_type AS A5, p_size AS A6, p_container AS A7, p_retailprice AS A8, p_comment AS A9 FROM tpch_part;
CREATE OR REPLACE VIEW w_partsupp AS SELECT ps_partkey AS A1, ps_suppkey AS A2, ps_availqty AS A3, ps_supplycost AS A4, ps_comment AS A5 FROM tpch_partsupp;
CREATE OR REPLACE VIEW w_supplier AS SELECT s_suppkey AS A1, s_name AS A2, s_address AS A3, s_nationkey AS A4, s_phone AS A5, s_acctbal AS A6, s_comment AS A7 FROM tpch_supplier;
CREATE OR REPLACE VIEW w_nation AS SELECT n_nationkey AS A1, n_name AS A2, n_regionkey AS A3, n_comment AS A4 FROM tpch_nation;
CREATE OR REPLACE VIEW w_region AS SELECT r_regionkey AS A1, r_name AS A2, r_comment AS A3 FROM tpch_region;
