#CFLAGS = -O2 -Wall -g
CFLAGS = -O2 -Wall -g -frounding-math -fsignaling-nans -mfpmath=sse -msse2

.PHONY: clean

fhctrl: nfhc.c ftdilcd.c fhctrl.c
	$(CC) $(CFLAGS) -g -o $@ $^ -ljack -lconfig -lcdk -lcurses -lftdi

clean:
	rm -f fhctrl nfhc
