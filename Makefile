TARGET  = filet
PREFIX ?= /usr/local

.PHONY: all install clean

all: $(TARGET)

install: all
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	$(RM) $(TARGET)
