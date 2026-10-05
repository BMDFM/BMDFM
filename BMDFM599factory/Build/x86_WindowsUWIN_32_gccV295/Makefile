# Makefile

#==============================================================================
# Win32-UWIN, GNU C
#------------------------------------------------------------------------------
CC = gcc
OPTIONS = -O3
#==============================================================================

MATH = -lm
PSM = -lpthread

all: cflp_udf.o fastlisp BMDFMldr BMDFMsrv CPUPROC

cflp_udf.o: cflp_udf.c cflp_udf.h
	$(CC) $(OPTIONS) -c cflp_udf.c

fastlisp: fastlisp.o cflp_udf.o
	$(CC) -o fastlisp.exe fastlisp.o cflp_udf.o $(MATH) $(PSM)

BMDFMldr: BMDFMldr.o cflp_udf.o
	$(CC) -o BMDFMldr.exe BMDFMldr.o cflp_udf.o $(MATH) $(PSM)

BMDFMsrv: BMDFMsrv.o cflp_udf.o
	$(CC) -o BMDFMsrv.exe BMDFMsrv.o cflp_udf.o $(MATH) $(PSM)

CPUPROC: CPUPROC.o cflp_udf.o
	$(CC) -o CPUPROC.exe CPUPROC.o cflp_udf.o $(MATH) $(PSM)

clean:
	rm -f cflp_udf.o fastlisp.exe BMDFMldr.exe BMDFMsrv.exe CPUPROC.exe \
        2>/dev/null

