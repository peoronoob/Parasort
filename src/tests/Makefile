MPICC = mpicc
MPICPP = mpic++
CFLAGS = -Wall -Wextra -Wno-unused -Werror -g -ftrapv

OBJECTS = ../debug.o
TESTS = sendrecv probe tsetup fwrite

DALOBJECTS = $(OBJECTS) ../dal.o
DALTESTS = daltest dalsendrecv dalalltoall dalscatter dalgather dalalltoallv dalscatterv dalgatherv

all: tests

tests: $(TESTS) $(DALTESTS)

$(TESTS): $(OBJECTS)
	@echo "Compiling $@..."
	$(MPICC) $(CFLAGS) $@.c $(OBJECTS) -o $@

$(DALTESTS): $(OBJECTS) $(DALOBJECTS)
	@echo "Compiling $@..."
	$(MPICC) $(CFLAGS) $@.c $(DALOBJECTS) -o $@

clean:
	rm -f *~ *.o $(TESTS) $(DALTESTS)
