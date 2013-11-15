APP = fhctrl

#PKG_CONFIG_MODULES := jack ncurses libftdi libconfig
PKG_CONFIG_MODULES := jack libftdi libconfig
CFLAGS = -g -O2 -Wall -mfpmath=sse -msse2
CFLAGS_APP = $(CFLAGS)
CFLAGS_APP += $(shell pkg-config --cflags $(PKG_CONFIG_MODULES))
CFLAGS_APP += -I/usr/include/cdk

LIBRARIES := $(shell pkg-config --libs $(PKG_CONFIG_MODULES))
LIBRARIES += -lcdk -lpthread -lncurses

DESTDIR =
BINDIR = usr/bin

.PHONY: all,clean

all: fhctrl lsp connect nxjsm save

$(APP): src/nfhc.c src/config.c src/ftdilcd.c src/basics.c src/fjack.c src/fhctrl.c src/log.c src/lcd.c
	$(CC) $(CFLAGS_APP) -o $@ $^ $(LIBRARIES)
	
lsp: tools/lsp.c
	$(CC) $(CFLAGS) -Wno-deprecated-declarations -o $(APP)_$@ $^ -ljack

connect: tools/connect.c
	$(CC) $(CFLAGS) -o $(APP)_$@ $^ -ljack

colors: unused/colors.c
	$(CC) $(CFLAGS) -o $@ $^ -lcdk -lcurses -I/usr/include/cdk

test: unused/test.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

nxjsm: src/nxjsm/nxjsm.c
	$(CC) $(CFLAGS_APP) -o $@ $^ $(LIBRARIES)

save: src/nxjsm/save.c
	$(CC) $(CFLAGS) -o nxjsm_$@ $^ -ljack -lconfig

transport: transport.c
	$(CC) $(CFLAGS) -o $(APP)_$@ $^ -ljack -lreadline

inprocess: inprocess.c
	$(CC) $(CFLAGS) -o $@ $^ -fPIC -shared -ljack

clean:
	rm -f $(APP) $(APP)_lsp $(APP)_connect $(APP)_transport colors test inprocess nxjsm nxjsm_save

install: all
	install -Dm755 $(APP) $(DESTDIR)/$(BINDIR)/$(APP)
	install -Dm755 tools/xjsm $(DESTDIR)/$(BINDIR)/xjsm
	install -Dm755 nxjsm $(DESTDIR)/$(BINDIR)/nxjsm
	install -Dm755 nxjsm_save $(DESTDIR)/$(BINDIR)/nxjsm_save
	install -Dm755 $(APP)_lsp $(DESTDIR)/$(BINDIR)/$(APP)_lsp
	install -Dm755 $(APP)_connect $(DESTDIR)/$(BINDIR)/$(APP)_connect
#	install -Dm755 $(APP)_transport $(DESTDIR)/$(BINDIR)/$(APP)_transport

