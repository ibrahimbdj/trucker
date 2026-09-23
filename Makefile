trucker: delivery.c main.c
	gcc *.c -o trucker

kill:
	for i in `cat pidlist | tr '\' ' '`; do kill -9 $$i; done 2> /dev/null && echo -n "" > pidlist