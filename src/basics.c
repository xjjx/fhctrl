/*
   FSTHost Control by Xj 

   This is part of FSTHost sources

   Based on jack-midi-dump by Carl Hetherington
*/

#include <stdio.h>
#include <string.h>

#include "basics.h"
#include "log.h"

/****************** STATE **************************************/
UnitState* state_new() {
	UnitState* fs = calloc(1,sizeof(UnitState));
	// Default state is Inactive
	fs->state = UNIT_NA;
	// Default program name is .. like below ;-)
	strcpy(fs->program_name, "<<<- UNKNOWN PRESET- >>>");
	// Rest is initialized to 0 ( by calloc )
	return fs;
}

/****************** FST ****************************************/
void unit_new ( Unit** unit, Song** songs, uint8_t uuid ) {
	Unit* f = malloc(sizeof(Unit));
	snprintf(f->name, sizeof f->name, "Device%d", uuid);
	f->id = uuid;
	f->type = UNIT_TYPE_PLUGIN; // default type
	f->state = state_new();
	f->change = true;

	// Add states to songs
	Song* s;
	for(s = song_first(songs); s; s = s->next)
		s->unit_state[uuid] = state_new();

	// Fill our global array
	unit[uuid] = f;
}

uint8_t unit_uniqe_id ( Unit** unit, uint8_t start ) {
	uint8_t i = start;
	for( ; i < MAX_UNITS; i++) if (!unit[i]) return i;
	return 0; // 0 mean error
}

Unit* unit_get ( Unit** unit, Song** songs, uint8_t uuid ) {
	if (unit[uuid] == NULL) unit_new( unit, songs, uuid);
	return unit[uuid];
}

Unit* unit_next ( Unit** unit, Unit* prev ) {
	Unit* fp;
	uint8_t i = (prev == NULL) ? 0 : prev->id + 1;
	while ( i < MAX_UNITS ) {
		fp = unit[i];
		if (fp) return fp;
		i++;
	}
	return NULL;
}

void unit_set_sysex ( Unit* fp, SysExDumpV1* sysex ) {
	UnitState* fs = fp->state;

	sysex->uuid = fp->id;
	sysex->program = fs->program;
	sysex->channel = fs->channel;
	sysex->volume = fs->volume;
	sysex->state = fs->state;
                
	/* NOTE: FSTHost ignore incoming strings anyway */
	memcpy(sysex->program_name, fs->program_name, sizeof(sysex->program_name));
	memcpy(sysex->plugin_name, fp->name, sizeof(sysex->plugin_name));
}

Unit* unit_get_from_sysex ( Unit** unit, Song** songs, SysExDumpV1* sysex ) {
	Unit* fp = unit_get( unit, songs, sysex->uuid );
	UnitState* fs = fp->state;

	fp->type = UNIT_TYPE_PLUGIN; // We just know this ;-)
	strcpy ( fp->name, (char*) sysex->plugin_name );

	fs->state	= sysex->state;
	fs->program	= sysex->program;
	fs->channel	= sysex->channel;
	fs->volume	= sysex->volume;
	strcpy ( fs->program_name, (char*) sysex->program_name );

	fp->change = true; // Update display

	return fp;
}

void unit_reset_to_na ( Unit** unit ) {
	uint8_t i;
	for ( i=0; i < MAX_UNITS; i++ ) {
		Unit* fp = unit[i];
		if ( ! fp ) continue;

		// NOTE: non-sysex devices doesn't handle IdentRequest
		if ( fp->type != UNIT_TYPE_DEVICE )
			fp->state->state = UNIT_NA;

		// No longer wait
		fp->wait4done = false;
	}
}

bool unit_is_any_na ( Unit** unit ) {
	uint8_t i;
	for (i=0; i < MAX_UNITS; i++) {
		Unit* fp = unit[i];
		if ( ! fp ) continue;

		if ( fp->type != UNIT_TYPE_DEVICE && 
		     fp->state->state == UNIT_NA
		) return true;
	}
	return false;
}

/****************** SONG ****************************************/
Song* song_new ( Song** songs, Unit** unit) {
	uint8_t i;
	Song* s = calloc(1, sizeof(Song));

	//LOG("Creating new song");
	// Add state for already known plugins
	for(i=0; i < MAX_UNITS; i++) {
		if (unit[i] == NULL) continue;

		s->unit_state[i] = state_new();
		*s->unit_state[i] = *unit[i]->state;
	}

	snprintf(s->name, sizeof s->name, "Song %d", song_count(songs) );

	// Bind to song list
	Song** sptr = songs;
	while (*sptr) sptr = &( (*sptr)->next );
	*sptr = s;

	return s;
}

Song* song_get ( Song** songs, short SongNumber) {
	if (SongNumber >= song_count(songs)) return NULL;

	Song* song;
	int s = 0;
	for(song=*songs; song && s < SongNumber; song = song->next, s++);

	return song;
}

short song_count ( Song** songs ) {
	Song* s;
	int count = 0;
	for(s = song_first(songs); s; s = s->next, count++);
	return count;
}

void song_update ( Song* song, Unit** unit ) {
	if ( ! song ) {
		LOG("SongUpdate: no such song");
		return;
	}

	uint8_t i;
	for(i=0; i < MAX_UNITS; i++) {
		// Do not update state for inactive Units
		if (unit[i] && unit[i]->state->state != UNIT_NA)
			*song->unit_state[i] = *unit[i]->state;
	}
}
