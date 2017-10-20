CXX?=/sw/gcc/7.1.0/bin/gcc

CFLAGS=-O3 -g -Wall
LDFLAGS=-O3 -g

HFILES=System.h AppClass.h App.h SchedEvent.h Schedule.h Simulation.h Task.h Trace.h
OFILES=$(HFILES:.h=.o)

all: celio prospective

celio: celio.o $(OFILES)
	$(CXX) $(LDFLAGS) -o $@ $? -lpng -lm

prospective: prospective.o $(OFILES)
	$(CXX) $(LDFLAGS) -o $@ $? -lpng -lm

%.o: %.C $(HFILES)
	$(CXX) $(CFLAGS) -o $@ -c $<

clean:
	@rm -f celio celio.o prospective prospective.o $(OFILES)
