APP = fhctrl

#PKG_CONFIG_MODULES := jack ncurses libftdi libconfig
PKG_CONFIG_MODULES := jack libftdi libconfig
CFLAGS = -g -O2 -Wall -mfpmath=sse -msse2
CFLAGS_APP = $(CFLAGS)
CFLAGS_APP += $(shell pkg-config --cflags $(PKG_CONFIG_MODULES))
CFLAGS_APP += -I/usr/include/cdk

LIBRARIES := $(shell pkg-config --libs $(PKG_CONFIG_MODULES)) -lpthread

DESTDIR =
BINDIR = usr/bin

.PHONY: all,clean

TARGETS := $(APP) n$(APP) $(APP)_lsp $(APP)_connect
all: $(TARGETS)

$(APP): src/config.c src/ftdilcd.c src/basics.c src/fjack.c src/fhctrl.c src/log.c src/lcd.c
	$(CC) $(CFLAGS_APP) -o $@ $^ $(LIBRARIES)

n$(APP): src/nfhc.c src/config.c src/ftdilcd.c src/basics.c src/fjack.c src/fhctrl.c src/log.c src/lcd.c
	$(CC) $(CFLAGS_APP) -DGUI=1 -o $@ $^ $(LIBRARIES) -lcdk -lncurses
	
colors: unused/colors.c
	$(CC) $(CFLAGS) -o $@ $^ -lcdk -lcurses -I/usr/include/cdk

test: unused/test.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

inprocess: inprocess.c
	$(CC) $(CFLAGS) -o $@ $^ -fPIC -shared -ljack

clean:
	rm -f $(TARGETS) $(APP)_transport colors test inprocess 

install: all
	install -Dm755 $(APP) $(DESTDIR)/$(BINDIR)/$(APP)
	install -Dm755 n$(APP) $(DESTDIR)/$(BINDIR)/n$(APP)
	install -Dm755 $(APP)_lsp $(DESTDIR)/$(BINDIR)/$(APP)_lsp
	install -Dm755 $(APP)_connect $(DESTDIR)/$(BINDIR)/$(APP)_connect
#	install -Dm755 $(APP)_transport $(DESTDIR)/$(BINDIR)/$(APP)_transport

