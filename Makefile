trucker.real: src/delivery.c src/main.c
	gcc src/*.c -Wall -Wextra -o trucker.real

kill:
	for i in `cat pidlist`; do kill -9 $$i; done 2> /dev/null && echo -n "" > pidlist

killp:
	for i in $$(ps -ef |  grep pasta | tr -s " " |  cut -d" " -f2);do kill -9 $$i; done 2> /dev/null
