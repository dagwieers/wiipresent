name = wiipresent

CC = cc
CFLAGS = -Wall -O2 -I /usr/include/libcwiimote -D_ENABLE_TILT -D_ENABLE_FORCE -D_DISABLE_BLOCKING_UPDATE
LDFLAGS= -lm -lX11 -lXtst -lcwiimote -lbluetooth

all: wiipresent

wiipresent: wiipresent.c
	$(CC) $(CFLAGS) $(LDFLAGS) wiipresent.c -o wiipresent

clean:
	rm -f wiipresent
