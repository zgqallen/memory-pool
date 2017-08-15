CC = gcc
LDFLAGS = -lpthread
INCLUDES = -I./

OBJS = mmpool.o mm_unittest.o

mm_test: $(OBJS)
	$(CC) -o $@ $^ -Wall $(LDFLAGS)

mm_test_glibc:
	$(CC) mm_unittest.c -DGLIBC -o $@ $(INCLUDES) $(LDFLAGS)

mm_test_debug:
	$(CC) mmpool.c mm_unittest.c -DDEBUG -g -o $@ $(INCLUDES) $(LDFLAGS)

%.o: %.c 
	$(CC) -c -o $@ $< $(INCLUDES)

all: mm_test mm_test_glibc mm_test_debug

clean:
	rm *.o mm_test mm_test_glibc mm_test_debug
