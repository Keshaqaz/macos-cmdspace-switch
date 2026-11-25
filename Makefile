CC = clang
CFLAGS = -O2 -Wall -Wextra -Werror
LDFLAGS = -framework Carbon -framework CoreFoundation

all: cmdspace-switch

cmdspace-switch: cmdspace-switch.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

install: cmdspace-switch
	./cmdspace-switch-ctl.sh install

uninstall:
	./cmdspace-switch-ctl.sh uninstall

status:
	./cmdspace-switch-ctl.sh status

clean:
	rm -f cmdspace-switch

.PHONY: all install uninstall status clean
