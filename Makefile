CFLAGS ?= -std=c99 -O2 -march=native
OBJS = MKVParser.o example.o

example: $(OBJS)
clean:
	$(RM) $(OBJS) example
