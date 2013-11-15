APP = nxjsm

#PKG_CONFIG_MODULES := jack ncurses libconfig
PKG_CONFIG_MODULES := jack libconfig
CFLAGS = -g -O2 -Wall -mfpmath=sse -msse2
CFLAGS_APP = $(CFLAGS)
CFLAGS_APP += $(shell pkg-config --cflags $(PKG_CONFIG_MODULES))
CFLAGS_APP += -I/usr/include/cdk

LIBRARIES := $(shell pkg-config --libs $(PKG_CONFIG_MODULES))
LIBRARIES += -lcdk -lncurses

DESTDIR =
BINDIR = usr/bin

.PHONY: all,clean

all: $(APP)_load $(APP)_save

$(APP)_load: load.c
	$(CC) $(CFLAGS_APP) -o $@ $^ $(LIBRARIES)

$(APP)_save: save.c
	$(CC) $(CFLAGS) -o $@ $^ -ljack -lconfig

clean:
	rm -f $(APP)_load $(APP)_save

install: all
	install -Dm755 $(APP) $(DESTDIR)/$(BINDIR)/$(APP)
	install -Dm755 $(APP)_load $(DESTDIR)/$(BINDIR)/$(APP)_load
	install -Dm755 $(APP)_save $(DESTDIR)/$(BINDIR)/$(APP)_save
