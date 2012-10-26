#CFLAGS = -O2 -Wall -g
CFLAGS = -O2 -Wall -g -frounding-math -fsignaling-nans -mfpmath=sse -msse2

.PHONY: clean

fhctrl: ../../trunk/sysex.c nfhc.c ftdilcd.c fhctrl.c
	$(CC) $(CFLAGS) -g -o $@ $^ -ljack -lconfig -lcdk -lcurses -pthread -lftdi

clean:
	rm -f fhctrl nfhc
