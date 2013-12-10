#ifndef __basics_h__
#define __basics_h__

/*
   FSTHost Control by Xj 

   This is part of FSTHost sources
*/

#include <stdint.h>
#include <stdbool.h>

#include "sysex.h"

enum State {
	UNIT_STATE_BYPASS = 0,
	UNIT_STATE_ACTIVE = 1,
	UNIT_NA = 2 // NOT AVAILABLE
};

typedef struct _UnitState {
        enum State state;
        uint8_t program; /* 0 - 127 */
        uint8_t channel; /* 0 - 17 */
        uint8_t volume; /* 0 - 127 */
        char program_name[24];
} UnitState;

enum Type { UNIT_TYPE_PLUGIN, UNIT_TYPE_DEVICE };
typedef struct _Unit {
        uint8_t id; /* 0 - 127 */
        char name[24];
	enum Type type;
	bool change;
       	UnitState* state;
} Unit;

typedef struct _Song {
        char name[24];
        struct _UnitState* fst_state[127];
        struct _Song* next;
} Song;

/****************** STATE **************************************/
UnitState* state_new();
/****************** FST ****************************************/
void fst_new ( Unit** fst, Song** songs, uint8_t uuid );
uint8_t fst_uniqe_id ( Unit** fst, uint8_t start );
Unit* fst_get ( Unit** fst, Song** songs, uint8_t uuid );
Unit* fst_next ( Unit** fst, Unit* prev );
void fst_set_sysex ( Unit* fp, SysExDumpV1* sysex );
Unit* fst_get_from_sysex ( Unit** fst, Song** songs, SysExDumpV1* sysex );
bool fst_is_any_na ( Unit** fst );
void fst_reset_to_na ( Unit** fst );
/****************** SONG ****************************************/
Song* song_new(Song** songs, Unit** fst);
Song* song_get(Song** songs, short SongNumber);
static inline Song* song_first ( Song** songs ) { return *songs; }
short song_count( Song** songs );
void song_update(Song* song, Unit** fst);

#endif /* __basics_h__ */
