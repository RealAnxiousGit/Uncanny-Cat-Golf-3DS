CC ?= gcc
CFLAGS ?= -O2 -g -Wall -std=gnu11
GAME = ../source/game.c ../source/level.c ../source/physics.c
SRC = sim.c $(GAME)

all: ucgsim gimtest modtest

ucgsim: $(SRC) ../source/game.h ../source/ucg.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

gimtest: gimtest.c $(GAME) ../source/game.h ../source/ucg.h
	$(CC) $(CFLAGS) -o $@ gimtest.c $(GAME) -lm

modtest: modtest.c $(GAME) ../source/bort.c ../source/endless.c ../source/pthru.c ../source/game.h ../source/ucg.h ../source/bort.h ../source/endless.h ../source/pthru.h
	$(CC) $(CFLAGS) -o $@ modtest.c $(GAME) ../source/bort.c ../source/endless.c ../source/pthru.c -lm

clean:
	rm -f ucgsim gimtest modtest
