
LIB = libpgchess

SOURCES = pgchess.c
OBJECTS = $(SOURCES:.c=.o)

CHESSLIB_DIR = ../chesslib/src
BUILD_DIR =  ../build

CCFLAGS = -Wall -fpic -I /usr/include/postgresql/server -I $(CHESSLIB_DIR)
LDFLAGS = -L  $(BUILD_DIR)/lib -lchess -Wl,-rpath=`pwd`/$(BUILD_DIR)/lib

CC = gcc

all: pgchess.sql

pgchess.sql: $(LIB).so
	psql chess -f $@ && true

$(LIB).so: $(OBJECTS)
	$(CC) $(CCFLAGS) $(LDFLAGS) $(OBJECTS) -shared -o $(LIB).so 
	cp $(LIB).so $(BUILD_DIR)/lib

.c.o:
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(LIB).so $(OBJECTS)
	
