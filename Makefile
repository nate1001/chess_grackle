
LIB = libpgchess.so
DB = chess

CHESSLIB_DIR = ../chesslib/src
BUILD_DIR = ../build

SOURCES = pgchess.c
OBJECTS = $(SOURCES:.c=.o)
SQL = pgchess.sql

CCFLAGS = -Wall -fpic -I /usr/include/postgresql/server -I $(CHESSLIB_DIR)
LDFLAGS = -L $(CHESSLIB_DIR) -lchess

CC = gcc

all: $(LIB) $(SQL) 
	psql $(DB) -f $(SQL)

%.sql: %.source
	echo sed -e "s:_OBJWD_:`pwd`/:g" < $< > $@
	rm -f $@ && sed -e "s:_OBJWD_:`pwd`/:g" < $< > $@

$(LIB): $(OBJECTS) 
	$(CC) $(CCFLAGS) $(LDFLAGS) $(OBJECTS) -shared -o $(LIB)

.c.o:
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(LIB).so $(OBJECTS) $(SQL)
	
