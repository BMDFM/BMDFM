# Makefile

#==============================================================================
# i386 Cygnus Win32, GNU C
#------------------------------------------------------------------------------
CC = gcc -m32
OPTIONS = -O3
#==============================================================================

MATH = -lm

all: cflp_udf.o fastlisp

cflp_udf.o: cflp_udf.c
	$(CC) $(OPTIONS) -c cflp_udf.c

fastlisp: fastlisp.o cflp_udf.o
	$(CC) -o fastlisp.exe fastlisp.o cflp_udf.o $(MATH)

clean:
	rm -f cflp_udf.o fastlisp.exe 2>/dev/null

