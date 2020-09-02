make: package.c compute.c
	gcc package.c -Wall -std=c99 -lpthread -o package
	gcc compute.c -Wall -std=c99 -lpthread -o compute
clean:
	rm package
	rm compute
debug:
	gcc package.c -Wall -std=c99 -lpthread -g -o package
	gcc compute.c -Wall -std=c99 -lpthread -g -o compute
