
LIB = libpgchess.so
DB = chess


CHESSLIB_DIR = ../chesslib/src
OBJS = `ls $(CHESSLIB_DIR)/*.o`
LIB_DIR = ../build/lib/

SOURCES = pgchess.c
OBJECTS = $(SOURCES:.c=.o)
SQL = pgchess.sql

CCFLAGS = -Wall -fpic -I /usr/include/postgresql/server -I $(CHESSLIB_DIR)
LDFLAGS = -L $(CHESSLIB_DIR) -lchess

CC = gcc

all: runsql

runsql: $(SQL) $(LIB)
	psql $(DB) -f $(SQL) > /dev/null
	touch $@

%.sql: %.source
	rm -f $@ && sed -e "s:_OBJWD_:`pwd`/:g" < $< > $@

$(LIB): $(OBJECTS) 
	$(CC) $(CCFLAGS) $(LDFLAGS) $(OBJS) $(OBJECTS) -shared -o $(LIB)

clean:
	rm -f $(LIB).so $(OBJECTS) $(SQL) runsql

build:
	install $(LIB) $(LIB_DIR) 


.PHONY: clean build
