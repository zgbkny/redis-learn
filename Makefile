# Makefile for wbox
# Copyright (C) 2007 Salvatore Sanfilippo <antirez@invece.org>
# All Rights Reserved
# Under the GPL license version 2

DEBUG?= -g
CFLAGS?= -O2 -Wall -W -DSDS_ABORT_ON_OOM
CCOPT= $(CFLAGS)

OBJ = adlist.o ae.o anet.o dict.o redis.o sds.o picol.o
PRGNAME = redis-server

all: redis-server

# Deps (use make dep to generate this)
src/picol.o: src/picol.c src/picol.h
src/adlist.o: src/adlist.c src/adlist.h
src/ae.o: src/ae.c src/ae.h
src/anet.o: src/anet.c src/anet.h
src/dict.o: src/dict.c src/dict.h
src/redis.o: src/redis.c src/ae.h src/sds.h src/anet.h src/dict.h src/adlist.h
src/sds.o: src/sds.c src/sds.h

redis-server: $(OBJ)
	$(CC) -o $(PRGNAME) $(CCOPT) $(DEBUG) $(OBJ)
	@echo ""
	@echo "Hint: To run the test-redis.tcl script is a good idea."
	@echo "Launch the redis server with ./redis-server, then in another"
	@echo "terminal window enter this directory and run 'make test'."
	@echo ""

c.o:
	$(CC) -c $(CCOPT) $(DEBUG) $(COMPILE_TIME) $<

clean:
	rm -rf $(PRGNAME) *.o

dep:
	$(CC) -MM *.c

test:
	tclsh test-redis.tcl
