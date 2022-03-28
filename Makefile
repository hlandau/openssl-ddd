TESTS=ddd-01-conn-blocking ddd-02-conn-nonblocking ddd-03-fd-blocking ddd-04-fd-nonblocking ddd-05-mem-nonblocking

all: $(TESTS)

test: all
	for x in $(TESTS); do echo "$$x"; ./$$x | grep -q '</html>' || { echo >&2 'Error'; exit 1; }; done

ddd-%: ddd-%.c
	gcc -O3 -g -o "$@" "$<" -lcrypto -lssl

