SUBDIRS=. tests
bin_PROGRAMS=mysqljsonexport
AM_CPPFLAGS=-I$(MYSQL_HOME)/include -I$(MYSQL_HOME)/include/mysql -Wall
mysqljsonexport_SOURCES=mysqljsonexport.c optionutil.c optionutil.h
mysqljsonexport_LDADD=$(PTHREADLIB) -L$(MYSQL_HOME)/lib -L$(MYSQL_HOME)/lib/mysql \
  -lmysqlclient
EXTRA_DIST=users.cnf mysqljsonexport_1_2.pdf
