PORT=57359
CFLAGS = -DPORT=\$(PORT) -g -Wall -Werror -fsanitize=address

all: combatserver

combatserver: combatserver.o
	gcc ${CFLAGS} -o $@ $^

clean:
	rm *.o combatserver