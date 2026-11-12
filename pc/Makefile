CC ?= gcc
CFLAGS ?= -O2 -g -Wall -std=gnu11
GAME = ../source/game.c ../source/level.c ../source/physics.c
SRC = sim.c $(GAME)

all: ucgsim gimtest

ucgsim: $(SRC) ../source/game.h ../source/ucg.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

gimtest: gimtest.c $(GAME) ../source/game.h ../source/ucg.h
	$(CC) $(CFLAGS) -o $@ gimtest.c $(GAME) -lm

clean:
	rm -f ucgsim gimtest
