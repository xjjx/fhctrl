#CFLAGS = -O2 -Wall -g
CFLAGS = -O2 -Wall -g -frounding-math -fsignaling-nans -mfpmath=sse -msse2

.PHONY: clean

fhctrl: nfhc.c config.c ftdilcd.c fhctrl.c
	$(CC) $(CFLAGS) -g -o $@ $^ -ljack -lconfig -lcdk -lcurses -lftdi
	
session_notify: session_notify.c
	$(CC) -g -o $@ $^ -ljack

lsp: lsp.c
	$(CC) -g -o $@ $^ -ljack

clean:
	rm -f fhctrl nfhc session_notify lsp
