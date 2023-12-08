CFLAGS := -g -Wall -Werror -D_GNU_SOURCE

TARGETS := test_basic

# Make sure that 'all' is the first target
all: depend $(TARGETS)

clean:
	rm -rf core *.o $(TARGETS)

realclean: clean
	rm -rf *~ *.bak .depend *.log *.out

tags:
	etags *.c *.h

OBJS := test_thread.o thread.o 

$(TARGETS): $(OBJS)

depend:
	$(CC) -MM *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif
