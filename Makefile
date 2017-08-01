CC=clang
CFLAGS=-O2 -DNDEBUG -Wall -fPIC -shared 

libmyalloc.so: allocator.c
	$(CC) $(CFLAGS) -o libmyalloc.so allocator.c -lm
