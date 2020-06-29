CC = gcc
CFLAGS = -Wall -Wpedantic -Wextra
PREFIX = /usr/bin

src = $(wildcard *.c)
obj = $(src:.c=.o)
bin = ash-euses

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

$(bin): $(obj)
	$(CC) -o $(bin) $^ $(CFLAGS)

.PHONY: install
install: $(bin)
	mkdir -p $(DESTDIR)$(PREFIX)
	cp $< $(DESTDIR)$(PREFIX)/$(bin)

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/$(bin)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)

