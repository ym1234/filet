TARGET  = filet
MANPAGE = filet.1
PREFIX ?= /usr/local

CFLAGS   += -std=c11 -Wall -Wextra -pedantic
CPPFLAGS += -D_POSIX_C_SOURCE=200809L

.PHONY: all install clean

all: $(TARGET)

install: all
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -Dm644 $(MANPAGE) $(DESTDIR)$(PREFIX)/share/man/man1/$(MANPAGE)

clean:
	$(RM) $(TARGET)
