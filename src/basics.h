#ifndef __basics_h__
#define __basics_h__

/*
   FSTHost Control by Xj 

   This is part of FSTHost sources
*/

#include <stdint.h>
#include <stdbool.h>

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

enum Type { FST_TYPE_PLUGIN, FST_TYPE_DEVICE };
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

/****************** STATE **************************************/
FSTState* state_new();
/****************** FST ****************************************/
void fst_new ( FSTPlug** fst, Song** songs, uint8_t uuid );
uint8_t fst_uniqe_id ( FSTPlug** fst, uint8_t start );
FSTPlug* fst_get ( FSTPlug** fst, Song** songs, uint8_t uuid );
FSTPlug* fst_next ( FSTPlug** fst, FSTPlug* prev );
/****************** SONG ****************************************/
Song* song_new(Song** songs, FSTPlug** fst);
Song* song_get(Song** songs, short SongNumber);
static inline Song* song_first ( Song** songs ) { return *songs; }
short song_count( Song** songs );
void song_update(Song* song, FSTPlug** fst);

#endif /* __basics_h__ */
