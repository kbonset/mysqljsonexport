noinst_test=test-init.cnf cretab1.cnf cretab2.cnf cretab3.cnf cretab4.cnf cretab5.cnf cretab6.cnf cretab7.cnf \
  test2.cnf test3.cnf test4.cnf test4.ref test4_3.ref test5.cnf test5.ref test6.cnf test7.cnf test8.cnf test8.ref1 test8.ref2 \
  test9.cnf test9_1.ref test9_2.ref test9_3.ref test9_4.ref test10.cnf test10_1.ref test11.cnf test11_1.ref test11_2.ref \
  test11_4.ref test11_7.ref test12.cnf test12_1.ref test12_2.ref test12_3.ref test13.cnf test13.ref test14.cnf test14.ref test15.cnf \
  test15_1.ref test16.cnf test16_1.ref test16_2.ref test17.cnf test17_1.ref test18_1.ref test18_2.ref test19_1.ref test19_2.ref test19_3.ref \
  test20.ref
EXTRA_DIST=$(noinst_test)
TESTPROG1=../mysqljsonexport
DATABASE=jsontest
TESTDIR=jsontest2
TEST_INIT=-rm -rf $(DATABASE) $(TESTDIR) *.out

check: $(TESTPROG1) test1 test2 test3 test4_1 test4_2 test4_3 test5 test6 test7 test8 test9_1 test9_2 test9_3 test9_4 \
  test10_1 test10_2 test11_1 test11_2 test11_3 test11_4 test11_5 test11_6 test11_7 test11_8 test12_1 test12_2 test12_3 test13 \
  test14_1 test14_2 test15_1 test16_1 test16_2 test17_1 test18_1 test18_2 test19_1 test19_2 test19_3 test20

# Test that we get an error when required options aren't specified.
test1: $(TESTPROG)
	@$(TEST_INIT)
	test `$(TESTPROG1) > /dev/null 2>&1 ; echo $$?` -eq 22

test2: $(TESTPROG) test2.cnf test-init.cnf
	@echo 'Testing a simple config file'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test2.cnf > /dev/null 2>&1 ; echo $$?` -eq 0

test3: $(TESTPROG) test3.cnf test-init.cnf cretab1.cnf
	@echo 'Testing export of a non-existing table'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test3.cnf > /dev/null 2>&1 ; echo $$?` -eq 122

test4_1: $(TESTPROG) test4.cnf test4.ref test-init.cnf cretab1.cnf
	@echo 'Testing basic export of a table'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test4.cnf > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab1.json test4.ref > /dev/null

test4_2: $(TESTPROG) test4.cnf test4.ref test-init.cnf cretab1.cnf
	@echo 'Testing basic export of a table using use_result'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test4.cnf --use-result > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab1.json test4.ref > /dev/null

test4_3: $(TESTPROG) test4.cnf test4_3.ref test-init.cnf cretab1.cnf
	@echo 'Testing basic export of a table with different json column names'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test4.cnf --col-json-name=jsoncol1=col1 --col-json-name=jsoncol2=col2 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab1.json test4_3.ref > /dev/null

test5: $(TESTPROG) test5.cnf test5.ref test-init.cnf cretab1.cnf
	@echo 'Testing export skipping one column'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test5.cnf > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab1.json test5.ref > /dev/null

test6: $(TESTPROG) test6.cnf test-init.cnf cretab1.cnf
	@echo 'Testing export without autobatching an no primary key'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test6.cnf > /dev/null 2>&1 ; echo $$?` -eq 255

test7: $(TESTPROG) test7.cnf test-init.cnf cretab1.cnf
	@echo 'Testing export without auto-batch and a non exiting batch column specified.'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test7.cnf > /dev/null 2>&1 ; echo $$?` -eq 255

test8: $(TESTPROG) test8.cnf test8.ref1 test8.ref2 test-init.cnf cretab2.cnf
	@echo 'Testing export without auto batching'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test8.cnf > test8.out 2>&1 ; echo $$?` -eq 0
	$(DIFF) test8.out test8.ref1 > /dev/null
	$(DIFF) $(DATABASE)/jsontab2.json test8.ref2 > /dev/null

test9_1: $(TESTPROG) test9.cnf test-init.cnf cretab2.cnf test9_1.ref
	@echo 'Testing export with a fixed value column'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test9.cnf > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab2.json test9_1.ref > /dev/null

test9_2: $(TESTPROG) test9.cnf test-init.cnf cretab2.cnf test9_2.ref
	@echo 'Testing export including one quoted, unknown column'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test9.cnf --col-quoted=coldummy1 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab2.json test9_2.ref > /dev/null

test9_3: $(TESTPROG) test9.cnf test-init.cnf cretab2.cnf 
	@echo 'Testing export and skipping one non-existing column'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test9.cnf --skip-col=coldummy1 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab2.json test9_3.ref > /dev/null

test9_4: $(TESTPROG) test9.cnf test-init.cnf cretab2.cnf test9_4.ref
	@echo 'Testing export and skipping one known and one unknown column'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test9.cnf --skip-col=coldummy1 --skip-col=jsoncol1 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab2.json test9_4.ref > /dev/null

test10_1: $(TESTPROG) test10.cnf test-init.cnf cretab1.cnf test10_1.ref
	@echo 'Testing export of a single table into a specific file'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test10.cnf --file=test10.out jsontab1 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) test10.out test10_1.ref > /dev/null

test10_2: $(TESTPROG) test10.cnf test-init.cnf cretab1.cnf
	@echo 'Testing export with two tables and a single file'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test10.cnf --file=test10.out jsontab1 jsontab1 > /dev/null 2>&1 ; echo $$?` -eq 255

test11_1: $(TESTPROG) test11.cnf test-init.cnf cretab3.cnf test11_1.ref
	@echo 'Testing export with 2 skipped columns'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test11.cnf > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab3.json test11_1.ref > /dev/null

test11_2: $(TESTPROG) test11.cnf test-init.cnf cretab3.cnf test11_2.ref
	@echo 'Testing export with limit'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test11.cnf --limit=13 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab3.json test11_2.ref > /dev/null

test11_3: $(TESTPROG) test11.cnf test-init.cnf cretab3.cnf test11_2.ref
	@echo 'testing export with limit not modulo batch size'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test11.cnf --batch-size=3 --limit=13 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab3.json test11_2.ref > /dev/null

test11_4: $(TESTPROG) test11.cnf test-init.cnf cretab3.cnf test11_4.ref
	@echo 'Test of export with limit larger than batch size'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test11.cnf --batch-size=5 --limit=10 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab3.json test11_4.ref > /dev/null

test11_5: $(TESTPROG) test11.cnf test-init.cnf cretab3.cnf test11_4.ref
	@echo 'Testing export with batch and limit equal'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test11.cnf --batch-size=10 --limit=10 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab3.json test11_4.ref > /dev/null

test11_6: $(TESTPROG) test11.cnf test-init.cnf cretab3.cnf test11_4.ref
	@echo 'Testing batching with limit less than batch'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test11.cnf --batch-size=100 --limit=10 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab3.json test11_4.ref > /dev/null

test11_7: $(TESTPROG) test11.cnf test-init.cnf cretab3.cnf test11_7.ref
	@echo 'Testing batching with limit less than batch and unquoted strings'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test11.cnf --batch-size=100 --limit=10 --col-unquoted=jsoncol3 > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab3.json test11_7.ref > /dev/null

test11_8: $(TESTPROG) test11.cnf test-init.cnf cretab3.cnf test11_4.ref
	@echo 'Testing batching with limit less than batch into a specific directory'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test11.cnf --batch-size=100 --limit=10 --directory=$(TESTDIR) > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(TESTDIR)/jsontab3.json test11_4.ref > /dev/null

test12_1: $(TESTPROG) test12.cnf test-init.cnf cretab4.cnf test12_1.ref
	@echo 'Testing datatype export'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test12.cnf > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab4.json test12_1.ref > /dev/null

test12_2: $(TESTPROG) test12.cnf test-init.cnf cretab4.cnf test12_2.ref
	@echo 'Testing export of bool'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test12.cnf --tiny1-as-bool > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab4.json test12_2.ref > /dev/null

test12_3: $(TESTPROG) test12.cnf test-init.cnf cretab4.cnf test12_3.ref
	@echo 'Testing skipping null columns'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test12.cnf --skip-null > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab4.json test12_3.ref > /dev/null

test13: $(TESTPROG) test13.cnf test-init.cnf cretab5.cnf test13.ref
	@echo 'Testing skipping one column'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test13.cnf --skip-col=col_bigint > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab5.json test13.ref > /dev/null

test14_1: $(TESTPROG) test14.cnf test-init.cnf cretab3.cnf test14.ref
	@echo 'Testing SQL WHERE suffix'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test14.cnf > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab3.json test14.ref > /dev/null

test14_2: $(TESTPROG) test14.cnf test-init.cnf cretab3.cnf test14.ref
	@echo 'Testing SQL WHERE suffix with batching'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --batch-size=4 --defaults-file=test14.cnf > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab3.json test14.ref > /dev/null

test15_1: $(TESTPROG) test15.cnf test-init.cnf cretab6.cnf test15_1.ref
	@echo 'Testing SQL statement export'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test15.cnf --sql="SELECT id, value, 'A string' FROM jsontab6_1" \
  --file=test15_1.out > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) test15_1.out test15_1.ref > /dev/null

test16_1: $(TESTPROG) test16.cnf test-init.cnf cretab6.cnf test16_1.ref
	@echo 'Testing SQL statement with JOIN export'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test16.cnf --sql="SELECT t1.id AS id1, t2.id AS id2 FROM jsontab6_1 AS t1 \
  JOIN jsontab6_2 AS t2 ON t1.id = t2.parent ORDER BY t1.id, t2.id" --file=test16_1.out > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) test16_1.out test16_1.ref > /dev/null

test16_2: $(TESTPROG) test16.cnf test-init.cnf cretab6.cnf test16_2.ref
	@echo 'Testing SQL statement with JOIN and WHERE export'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test16.cnf --sql="SELECT t1.id AS id1, t2.id AS id2 FROM jsontab6_1 AS t1 \
  JOIN jsontab6_2 AS t2 ON t1.id = t2.parent WHERE t1.id > 3 ORDER BY t1.id, t2.id" --file=test16_2.out > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) test16_2.out test16_2.ref > /dev/null

test17_1: $(TESTPROG) test17.cnf test-init.cnf cretab6.cnf test17_1.ref
	@echo 'Testing SQL statement with JOIN and WHERE export'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --batch-size=3 --defaults-file=test17.cnf --sql="SELECT * FROM (SELECT t1.id * 100 + t2.id AS id, t1.id AS id1, t2.id AS id2 FROM jsontab6_1 AS t1 \
  JOIN jsontab6_2 AS t2 ON t1.id = t2.parent) AS t1 %W id1 > 3 %O" --batch-col=id --skip-auto-batch --file=test17_1.out > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) test17_1.out test17_1.ref > /dev/null

test18_1: $(TESTPROG) test-init.cnf cretab6.cnf test18_1.ref test18_2.ref
	@echo 'Testing multi table export'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test-init.cnf --include=cretab6.cnf > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab6_1.json test18_1.ref > /dev/null
	$(DIFF) $(DATABASE)/jsontab6_2.json test18_2.ref > /dev/null

test18_2: $(TESTPROG) test-init.cnf cretab6.cnf test18_1.ref test18_2.ref
	@echo 'Testing multi table export with extension'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test-init.cnf --include=cretab6.cnf --extension=.dmp > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab6_1.dmp test18_1.ref > /dev/null
	$(DIFF) $(DATABASE)/jsontab6_2.dmp test18_2.ref > /dev/null

test19_1: $(TESTPROG) test-init.cnf cretab7.cnf test19_1.ref
	@echo 'Testing export skipping empty string columns'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test-init.cnf --include=cretab7.cnf --skip-empty > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab7.json test19_1.ref > /dev/null

test19_2: $(TESTPROG) test-init.cnf cretab7.cnf test19_2.ref
	@echo 'Testing export skipping empty and null string columns'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test-init.cnf --include=cretab7.cnf --skip-empty --skip-null > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab7.json test19_2.ref > /dev/null

test19_3: $(TESTPROG) test-init.cnf cretab7.cnf test19_1.ref test19_3.ref
	@echo 'Testing sql-finish option'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test-init.cnf --include=cretab7.cnf --sql-finish="DELETE FROM jsontab7 WHERE id = 5" --skip-empty > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab7.json test19_1.ref > /dev/null
	test `$(TESTPROG1) -d $(DATABASE) -u root > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab7.json test19_3.ref > /dev/null

test20: $(TESTPROG) test-init.cnf cretab7.cnf test20.ref
	@echo 'Testing array-file option'
	@$(TEST_INIT)
	test `$(TESTPROG1) -d $(DATABASE) -u root --defaults-file=test-init.cnf --include=cretab7.cnf --array-file > /dev/null 2>&1 ; echo $$?` -eq 0
	$(DIFF) $(DATABASE)/jsontab7.json test20.ref > /dev/null
