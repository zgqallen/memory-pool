CC = gcc
LDFLAGS = -lpthread
INCLUDES = -I./

OBJS = mmpool.o mm_unittest.o

mm_test: $(OBJS)
	$(CC) -o $@ $^ -Wall $(LDFLAGS)

%.o: %.c 
	$(CC) -c -o $@ $< $(INCLUDES)

clean:
	rm *.o mm_test
