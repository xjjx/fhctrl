#CFLAGS = -O2 -Wall -g
CFLAGS = -O2 -Wall -Wno-deprecated-declarations -g -frounding-math -fsignaling-nans -mfpmath=sse -msse2
DESTDIR =

.PHONY: all,clean

all: fhctrl fhctrl_sn fhctrl_lsp fhctrl_connect

fhctrl: nfhc.c config.c ftdilcd.c fhctrl.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack -lconfig -lcdk -lcurses -lftdi
	
fhctrl_sn: session_notify.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

fhctrl_lsp: lsp.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

fhctrl_connect: connect.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

clean:
	rm -f fhctrl fhctrl_sn fhctrl_lsp fhctrl_connect

install: all
	install -Dm755 fhctrl $(DESTDIR)/usr/bin/fhctrl
	install -Dm755 fhctrl_sn $(DESTDIR)/usr/bin/fhctrl_sn
	install -Dm755 fhctrl_lsp $(DESTDIR)/usr/bin/fhctrl_lsp
	install -Dm755 fhctrl_connect $(DESTDIR)/usr/bin/fhctrl_connect
