APP = fhctrl

PKG_CONFIG_MODULES := jack ncurses libftdi libconfig
CFLAGS = -g -O2 -Wall -mfpmath=sse -msse2
CFLAGS_APP = $(CFLAGS)
CFLAGS_APP += $(shell pkg-config --cflags $(PKG_CONFIG_MODULES))
CFLAGS_APP += -I/usr/include/cdk

LIBRARIES := $(shell pkg-config --libs $(PKG_CONFIG_MODULES))
LIBRARIES += -lcdk

DESTDIR =
BINDIR = usr/bin


.PHONY: all,clean

all: fhctrl sn lsp connect

$(APP): src/nfhc.c src/config.c src/ftdilcd.c src/basics.c src/fjack.c src/fhctrl.c src/log.c
	$(CC) $(CFLAGS_APP) -o $@ $^ $(LIBRARIES)
	
sn: tools/session_notify.c
	$(CC) $(CFLAGS) -o $(APP)_$@ $^ -ljack

lsp: tools/lsp.c
	$(CC) $(CFLAGS) -Wno-deprecated-declarations -o $(APP)_$@ $^ -ljack

connect: tools/connect.c
	$(CC) $(CFLAGS) -o $(APP)_$@ $^ -ljack

colors: unused/colors.c
	$(CC) $(CFLAGS) -o $@ $^ -lcdk -lcurses -I/usr/include/cdk

test: unused/test.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack

transport: transport.c
	$(CC) $(CFLAGS) -o $(APP)_$@ $^ -ljack -lreadline

inprocess: inprocess.c
	$(CC) $(CFLAGS) -o $@ $^ -fPIC -shared -ljack

clean:
	rm -f $(APP) $(APP)_sn $(APP)_lsp $(APP)_connect $(APP)_transport colors test inprocess

install: all
	install -Dm755 $(APP) $(DESTDIR)/$(BINDIR)/$(APP)
	install -Dm755 xjsm $(DESTDIR)/$(BINDIR)/xjsm
	install -Dm755 $(APP)_sn $(DESTDIR)/$(BINDIR)/$(APP)_sn
	install -Dm755 $(APP)_lsp $(DESTDIR)/$(BINDIR)/$(APP)_lsp
	install -Dm755 $(APP)_connect $(DESTDIR)/$(BINDIR)/$(APP)_connect
#	install -Dm755 $(APP)_transport $(DESTDIR)/$(BINDIR)/$(APP)_transport

