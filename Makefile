trucker.real: delivery.c main.c
	gcc *.c -Wall -Wextra -o trucker.real

kill:
	for i in `cat pidlist`; do kill -9 $$i; done 2> /dev/null && echo -n "" > pidlist