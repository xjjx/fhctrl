/*
   FSTHost Control by XJ / Pawel Piatek /

   This is part of FSTHost sources
*/

#include <stdint.h>

enum Type {
	FST_TYPE_PLUGIN,
	FST_TYPE_DEVICE
};

enum State {
	FST_STATE_BYPASS = 0,
	FST_STATE_ACTIVE = 1,
	FST_NA = 2 // NOT AVAILABLE
};

typedef struct _FSTState {
        enum State state;
        uint8_t program; /* 0 - 127 */
        uint8_t channel; /* 0 - 17 */
        uint8_t volume; /* 0 - 127 */
        char program_name[24];
} FSTState;

typedef struct _FSTPlug {
        uint8_t id; /* 0 - 127 */
        char name[24];
	enum Type type;
	bool change;
       	FSTState* state;
} FSTPlug;

typedef struct _Song {
        char name[24];
        struct _FSTState* fst_state[127];
        struct _Song* next;
} Song;

struct LCDScreen {
	bool available; /* Are we have LCD ? */
	FSTPlug* fst;
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
	void (*idle_cb)(void* arg);

	/* Public variables */
	FSTPlug*		fst[128];
	Song**			songs;
	struct CDKGUI		gui;
	struct LCDScreen	lcd_screen;
} FHCTRL;

/* nfhc.c */
void nfhc(FHCTRL* fhctrl);

/* Exported functions */
Song* song_first ( Song** songs );
short song_count( Song** songs );
void send_ident_request( FHCTRL* fhctrl );
void song_send( FHCTRL* fhctrl, short SongNumber);
Song* song_new(Song** songs, FSTPlug** fst);
Song* song_get(Song** songs, short SongNumber);
void song_update(FSTPlug** fst, Song* song);
void update_config( FHCTRL* fhctrl );
int cpu_load( FHCTRL* fhctrl );
FSTState* state_new();
FSTPlug* fst_get ( FSTPlug** fst, Song** songs, uint8_t uuid );
FSTPlug* fst_next(FSTPlug** fst, FSTPlug* prev);
void fst_send(FHCTRL* fhctrl, FSTPlug* fp);
void send_dump_request(FHCTRL* fhctrl, short id);

