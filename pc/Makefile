CC ?= gcc
CFLAGS ?= -O2 -g -Wall -std=gnu11
SRC = sim.c ../source/game.c ../source/level.c ../source/physics.c

ucgsim: $(SRC) ../source/game.h ../source/ucg.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

clean:
	rm -f ucgsim
