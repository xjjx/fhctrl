/*
   FSTHost Control by XJ / Pawel Piatek /

   This is part of FSTHost sources
*/

#include "basics.h"

struct LCDScreen {
	bool available; /* Are we have LCD ? */
	const char* app_name;
	Unit* fst;
};

struct CDKGUI {
	bool midi_in;
	bool ctrl_midi_in;
	bool sysex_midi_in;
	bool lcd_need_update;
};

typedef struct _FHCTRL {
	/* Our private variables */
	const char*		config_file;
	bool			try_connect_to_physical;
	uint8_t			offered_last;
	uint8_t			offered_last_choke;
	uint8_t			graph_order_changed;
	Song*			song_first;
	void*			user;

	/* Public variables */
	Unit*		fst[128];
	Song**			songs;
	struct CDKGUI		gui;
	struct LCDScreen	lcd_screen;
} FHCTRL;

/* nfhc.c */
void nfhc ( FHCTRL* fhctrl );

/* Exported functions */
void send_ident_request ( FHCTRL* fhctrl );
void fhctrl_song_send ( FHCTRL* fhctrl, short SongNumber);
void fhctrl_fst_send ( FHCTRL* fhctrl, Unit* fp, const char* logFuncName );
void update_config ( FHCTRL* fhctrl );
int cpu_load ( FHCTRL* fhctrl );
void send_dump_request ( FHCTRL* fhctrl, short id );
void fhctrl_idle ( FHCTRL* fhctrl );
