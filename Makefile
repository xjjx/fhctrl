#CFLAGS = -O2 -Wall -g
CFLAGS = -O2 -Wall -Wno-deprecated-declarations -g -frounding-math -fsignaling-nans -mfpmath=sse -msse2
DESTDIR =

.PHONY: all,clean

all: fhctrl fhctrl_sn fhctrl_lsp fhctrl_connect test

fhctrl: nfhc.c config.c ftdilcd.c fhctrl.c log.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack -lconfig -lcdk -lcurses -lftdi -I/usr/include/cdk
	
fhctrl_sn: session_notify.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

fhctrl_lsp: lsp.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

fhctrl_connect: connect.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

colors: colors.c
	$(CC) $(CFLAGS) -o $@ $^ -lcdk -lcurses -I/usr/include/cdk

test: test.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

clean:
	rm -f fhctrl fhctrl_sn fhctrl_lsp fhctrl_connect colors test

install: all
	install -Dm755 fhctrl $(DESTDIR)/usr/bin/fhctrl
	install -Dm755 xjsm $(DESTDIR)/usr/bin/xjsm
	install -Dm755 fhctrl_sn $(DESTDIR)/usr/bin/fhctrl_sn
	install -Dm755 fhctrl_lsp $(DESTDIR)/usr/bin/fhctrl_lsp
	install -Dm755 fhctrl_connect $(DESTDIR)/usr/bin/fhctrl_connect
