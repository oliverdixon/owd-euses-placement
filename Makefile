CC = gcc
CFLAGS = -O3 -Wall -Wpedantic -Wextra
PREFIX = /usr

src = $(wildcard *.c)
obj = $(src:.c=.o)
bin = ash-euses

$(bin): $(obj)
	$(CC) -o bin/$@ $^ $(CFLAGS)

.PHONY: install
install: $(bin)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $< $(DESTDIR)$(PREFIX)/bin/$(bin)

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(bin)

.PHONY: clean
clean:
	rm -f $(obj) bin/$(bin)

