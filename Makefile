SDL_CONFIG := sdl-config
SDL_CFLAGS := `$(SDL_CONFIG) --cflags`
SDL_LIBS   := `$(SDL_CONFIG) --libs`

OBJECTS	:= runtusdl.o tusdl.o tusl.o rand.o sim.o \
	   ants.o casdl.o evo.o orbit.o slime.o termite.o turtles.o wator.o 
LDADD	:= -lm

CC	:= gcc
CFLAGS	:= -g -O2 -Wall


all: runtusl runtusdl

runtusl.o: runtusl.c tusl.h
	$(CC) $(CFLAGS) -c $<

tusl.o: tusl.c tusl.h
	$(CC) $(CFLAGS) -c $<

rand.o: rand.c rand.h standard.h
	$(CC) $(CFLAGS) -c $<


runtusdl.o: runtusdl.c tusdl.h tusl.h sim.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

tusdl.o: tusdl.c tusdl.h tusl.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

sim.o: sim.c tusdl.h tusl.h sim.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

ants.o: ants.c tusdl.h tusl.h sim.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

casdl.o: casdl.c tusdl.h tusl.h 
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

evo.o: evo.c tusdl.h tusl.h sim.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

orbit.o: orbit.c tusdl.h tusl.h sim.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

slime.o: slime.c tusdl.h tusl.h sim.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

termite.o: termite.c tusdl.h tusl.h sim.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

turtles.o: turtles.c tusdl.h tusl.h sim.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<

wator.o: wator.c tusdl.h tusl.h sim.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $<


runtusl: runtusl.o tusl.o 
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ runtusl.o tusl.o $(LDADD)

runtusdl: $(OBJECTS)
	@rm -f $@
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDADD) $(SDL_LIBS)


clean:
	rm -f runtusl runtusdl *.exe *.o stdout.txt stderr.txt
