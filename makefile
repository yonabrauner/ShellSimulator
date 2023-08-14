myshell: myshell.o LineParser.o
	gcc -g -m32 -Wall myshell.o LineParser.o -o myshell

LineParser.o: LineParser.c LineParser.h
	gcc -g -m32 -Wall -c LineParser.c

myshell.o: myshell.c LineParser.h
	gcc -g -m32 -Wall -c myshell.c

mypipeline: mypipeline.o
	gcc -g -m32 -Wall mypipeline.o -o mypipeline

mypipeline.o: mypipeline.c
	gcc -g -m32 -Wall -c mypipeline.c

clean:
	rm -f *.o myshell mypipeline