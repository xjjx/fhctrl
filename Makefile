APP = fhctrl

#PKG_CONFIG_MODULES := jack ncurses libftdi libconfig
#PKG_CONFIG_MODULES := jack libftdi libconfig
PKG_CONFIG_MODULES := jack libconfig
CFLAGS = -g -O2 -Wall -mfpmath=sse -msse2
CFLAGS_APP = $(CFLAGS)
CFLAGS_APP += $(shell pkg-config --cflags $(PKG_CONFIG_MODULES))
CFLAGS_APP += -I/usr/include/cdk

LIBRARIES := $(shell pkg-config --libs $(PKG_CONFIG_MODULES)) -lpthread

DESTDIR =
BINDIR = usr/bin

.PHONY: all,clean

TARGETS := $(APP) n$(APP)
all: $(TARGETS)

# src/config.c src/ftdilcd.c src/basics.c src/fjack.c src/fhctrl.c src/log.c src/lcd.c
$(APP): src/config.c src/basics.c src/fjack.c src/fhctrl.c src/log.c
	$(CC) $(CFLAGS_APP) -o $@ $^ $(LIBRARIES)

# src/nfhc.c src/config.c src/ftdilcd.c src/basics.c src/fjack.c src/fhctrl.c src/log.c src/lcd.c
n$(APP): src/nfhc.c src/config.c src/basics.c src/fjack.c src/fhctrl.c src/log.c
	$(CC) $(CFLAGS_APP) -DGUI=1 -o $@ $^ $(LIBRARIES) -lcdk -lncurses
	
test: unused/test.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

inprocess: inprocess.c
	$(CC) $(CFLAGS) -o $@ $^ -fPIC -shared -ljack

clean:
	rm -f $(TARGETS) test inprocess 

install: all
	install -Dm755 $(APP) $(DESTDIR)/$(BINDIR)/$(APP)
	install -Dm755 n$(APP) $(DESTDIR)/$(BINDIR)/n$(APP)
