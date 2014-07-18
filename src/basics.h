#ifndef __basics_h__
#define __basics_h__

/*
   FSTHost Control by Xj 

   This is part of FSTHost sources
*/

#include <stdint.h>
#include <stdbool.h>

#include "sysex.h"

/* NOTE: Currently ID is one 7-bit MIDI packet,
         so 128 is limit for addressing UNITs ( PLUGIN TYPE ),
         while DEVICES are already limited to 16 MIDI channels
*/
#define MAX_UNITS 128

#define FOREACH_UNIT(U,UNITS) for(U=unit_next(UNITS,NULL);U;U=unit_next(UNITS,U))

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
	uint8_t id; /* 0 - (MAX_UNITS - 1) */
	char name[24];
	enum Type type;
	bool change;
	UnitState* state;
} Unit;

typedef struct _Song {
	char name[24];
	struct _UnitState* unit_state[MAX_UNITS];
	struct _Song* next;
} Song;

/****************** STATE **************************************/
UnitState* state_new();
/****************** UNIT ***************************************/
void unit_new ( Unit** unit, Song** songs, uint8_t uuid );
uint8_t unit_uniqe_id ( Unit** unit, uint8_t start );
Unit* unit_get ( Unit** unit, Song** songs, uint8_t uuid );
Unit* unit_next ( Unit** unit, Unit* prev );
void unit_set_sysex ( Unit* fp, SysExDumpV1* sysex );
Unit* unit_get_from_sysex ( Unit** unit, Song** songs, SysExDumpV1* sysex );
bool unit_is_any_na ( Unit** unit );
void unit_reset_to_na ( Unit** unit );
/****************** SONG ****************************************/
Song* song_new(Song** songs, Unit** unit);
Song* song_get(Song** songs, short SongNumber);
static inline Song* song_first ( Song** songs ) { return *songs; }
short song_count( Song** songs );
void song_update(Song* song, Unit** unit);

#endif /* __basics_h__ */
