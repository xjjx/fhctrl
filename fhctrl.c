/*
   FSTHost Control by XJ / Pawel Piatek /

   This is part of FSTHost sources

   Based on jack-midi-dump by Carl Hetherington
*/

#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <jack/jack.h>
#include <jack/midiport.h>

#include "sysex.h"
#include "fhctrl.h"

// Ncurses interface
extern void nfhc(struct Song *song_first, struct FSTPlug **fst);

// LCD via FTDI support
extern bool lcd_init();
extern void lcd_text(short x, short y, char* txt);
extern void lcd_close();

// Config file support
bool dump_state(char const* config_file, struct Song *song_first, struct FSTPlug **fst, short CtrlCh);
bool load_state(const char* config_file, struct Song *song_first, struct FSTPlug **fst, short CtrlCh);

char const* client_name = "FHControl";
static jack_port_t* inport;
static jack_port_t* outport;
static jack_nframes_t jack_buffer_size;
static jack_nframes_t jack_sample_rate;
short CtrlCh = 15; /* Our default MIDI control channel */
short SongCount = 0;
short SongSend = -1;
bool ident_request = false;

struct FSTPlug* fst[128] = {NULL};
struct Song* song_first = NULL;

struct LCDScreen lcd_screen;

const SysExIdentRqst sysex_ident_request = SYSEX_IDENT_REQUEST;
SysExDumpRequestV1 sysex_dump_request = SYSEX_DUMP_REQUEST;
SysExDumpV1 sysex_dump = SYSEX_DUMP;

struct FSTState* state_new() {
	struct FSTState* fs = calloc(1,sizeof(struct FSTState));
	fs->state = FST_NA; // Initial state is Inactive
	return fs;
}

void fst_new(uint8_t uuid) {
	struct Song* s;
	struct FSTPlug* f = malloc(sizeof(struct FSTPlug));
	sprintf(f->name, "Device%d", uuid);
	f->id = uuid;
	f->state = state_new();
	f->change = true;

	// Add to states to songs
	for(s = song_first; s; s = s->next) {
		s->fst_state[uuid] = state_new();
	}

	// Fill our global array
	fst[uuid] = f;
}

struct FSTPlug* fst_get(uint8_t uuid) {
	if (fst[uuid] == NULL)
		fst_new(uuid);

	return fst[uuid];	
}

struct Song* song_new() {
	short i;
	struct Song** snptr;
	struct Song* s = calloc(1, sizeof(struct Song));

	//printf("Creating new song\n");
	// Add state for already known plugins
	for(i=0; i < 128; i++) {
		if (fst[i] == NULL) continue;

		s->fst_state[i] = state_new();
	}

	// Bind to song list
	if (song_first) {
		snptr = &song_first;
		while(*snptr) { snptr = &(*snptr)->next; }
		*snptr = s;
	} else {
		song_first = s;
	}

	sprintf(s->name, "Song %d", SongCount);
	SongCount++;

	return s;
}

struct Song* song_get(short SongNumber) {
	if (SongNumber >= SongCount)
		return NULL;

	struct Song* song;
	int s = 0;
	for(song=song_first; s < SongNumber; s++) {
		song = song->next;
		if (song == NULL) return NULL;
	}

	return song;
}

void song_send(short SongNumber) {
	SongSend = SongNumber;
}

void song_update(short SongNumber) {
	struct Song* song = song_get(SongNumber);
	if(song == NULL) {
		nLOG("SongUpdate: no such song");
		return;
	}

	short i;
	for(i=0; i < 128; i++) {
		if (fst[i] == NULL) continue;
		// Do not update state for inactive FSTPlugs
		if (fst[i]->state->state == FST_NA) continue;
		*song->fst_state[i] = *fst[i]->state;
	}
}

void send_ident_request() {
	short i;

	// Reset states to non-active
	for(i=0; i < 128; i++) {
		if (fst[i] == NULL) continue;
		fst[i]->state->state = FST_NA;
	}

	ident_request = true;
}

void update_lcd() {
	if (! lcd_screen.available)
		return;

	struct FSTPlug* fp = lcd_screen.fst;
	if(!fp) return;
	char line[24];

	// Line 1
	snprintf(line, 24, "D%02d P%02d C%02d V%02d",
		fp->id,
		fp->state->program,
		fp->state->channel,
		fp->state->volume
	);
	lcd_text(0,0,line);

	// Line 2
	lcd_text(0,1,fp->state->program_name);

	// Line 3
	lcd_text(0,2,fp->name);
}

int process (jack_nframes_t frames, void* arg) {
	void* inbuf;
	void* outbuf;
	jack_nframes_t count;
	jack_nframes_t i;
	unsigned short s;
	jack_midi_event_t event;
	struct FSTPlug* fp;

	inbuf = jack_port_get_buffer (inport, frames);
	assert (inbuf);

	outbuf = jack_port_get_buffer(outport, frames);
	assert (outbuf);
	jack_midi_clear_buffer(outbuf);
	
	count = jack_midi_get_event_count (inbuf);
	for (i = 0; i < count; ++i) {
		if (jack_midi_event_get (&event, inbuf, i) != 0)
			break;

		// My Midi control channel handling
		if ( (event.buffer[0] & 0xF0) == 0xC0 &&
		     (event.buffer[0] & 0x0F) == CtrlCh
		) {
			SongSend = event.buffer[1];
			continue;
		}

		// Ident Reply
		if ( event.size < 5 || event.buffer[0] != SYSEX_BEGIN )
			goto further;

		switch (event.buffer[1]) {
		case SYSEX_NON_REALTIME:
			// event.buffer[2] is target_id - in our case always 7F
			if ( event.buffer[3] != SYSEX_GENERAL_INFORMATION ||
				event.buffer[4] != SYSEX_IDENTITY_REPLY
			) goto further;

			SysExIdentReply* r = (SysExIdentReply*) event.buffer;
			nLOG("Got SysEx Identity Reply from ID %X : %X", r->id, r->model[1]);
			fp = fst_get(r->model[1]);

			// If this is FSTPlug then dump it state
			if (r->id == SYSEX_MYID) {
				// Note: we refresh GUI when Dump back to us
				fp->dump_request = true;
			} else {
				fp->change = true;
			}
			// don't forward this message
			continue;
		case SYSEX_MYID:
			if (event.size < sizeof(SysExDumpV1))
				continue;

			SysExDumpV1* d = (SysExDumpV1*) event.buffer;
			if (d->type != SYSEX_TYPE_DUMP)
				goto further;

			nLOG("Got SysEx Dump %X : %s : %s", d->uuid, d->plugin_name, d->program_name);
			fp = fst_get(d->uuid);
			fp->state->state = d->state;
			fp->state->program = d->program;
			fp->state->channel = d->channel;
			fp->state->volume = d->volume;
			strcpy(fp->state->program_name, (char*) d->program_name);
			strcpy(fp->name, (char*) d->plugin_name);

			if (lcd_screen.available)
				lcd_screen.fst = fp;

			fp->change = true;

			// don't forward this message
			continue;
		}

further:
		// Forward messages
		jack_midi_event_write(outbuf, event.time, event.buffer, event.size);
	}

	// Send Identity Request
	if (ident_request) {
		ident_request = false;
		jack_midi_event_write(outbuf, jack_buffer_size - 1, 
			(jack_midi_data_t*) &sysex_ident_request, sizeof(SysExIdentRqst));
	}

	// Send Dump Request
	for (s=0; s < 128; s++) {
		fp = fst[s];
		if (!fp) continue;
		if (!fp->dump_request) continue;
		fp->dump_request = false;

		sysex_dump_request.uuid = fp->id;
		jack_midi_event_write(outbuf, jack_buffer_size - 1, (jack_midi_data_t*) &sysex_dump_request, sizeof(SysExDumpRequestV1));
	}

	// Send Song
	if (SongSend < 0) return 0;

	struct Song* song = song_get(SongSend);
	SongSend = -1;
	if (song == NULL) return 0;
	
	enum State curState;
	nLOG("Send Song \"%s\" SysEx", song->name);
	// Dump states via SysEx - for all FST
	for (s=0; s < 128; s++) {
		fp = fst[s];
		if (!fp) continue;

		curState = fp->state->state;
		*fp->state = *song->fst_state[s];
		// If plug is NA then keep it state and skip sending to plug
		if(curState == FST_NA) {
			fp->state->state = FST_NA;
			// Update display
			fp->change = true;
			continue;
		// If plug is NA in Song then preserve it's current state
		} else if(fp->state->state == FST_NA) {
			fp->state->state = curState;
		}
		// Update display
		fp->change = true;

		sysex_dump.program = fp->state->program;
		sysex_dump.channel = fp->state->channel;
		sysex_dump.volume = fp->state->volume;
		sysex_dump.state = fp->state->state;
		strcpy((char*) sysex_dump.program_name, fp->state->program_name);
		strcpy((char*) sysex_dump.plugin_name, fp->name);

		jack_midi_event_write(outbuf, jack_buffer_size - 1, (jack_midi_data_t*) &sysex_dump, sizeof(SysExDumpV1));
	}

	return 0;
}

int main (int argc, char* argv[]) {
	jack_client_t* client;
	char const* config_file = NULL;

	if (argv[1]) config_file = argv[1];

	lcd_screen.available = lcd_init();
	if(lcd_screen.available) lcd_screen.fst = NULL;

	// Try read file
	if (config_file != NULL)
		load_state(config_file, song_first, fst, CtrlCh);

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) {
		fprintf (stderr, "Could not create JACK client.\n");
		exit (EXIT_FAILURE);
	}

	jack_set_process_callback (client, process, 0);

	inport = jack_port_register (client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	outport = jack_port_register (client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	jack_buffer_size = jack_get_buffer_size(client);
	jack_sample_rate = jack_get_sample_rate(client);


	if ( jack_activate (client) != 0 ) {
		fprintf (stderr, "Could not activate client.\n");
		exit (EXIT_FAILURE);
	}

	// ncurses loop
	nfhc(song_first, fst);

	if (lcd_screen.available)
		lcd_close();

	if (config_file != NULL)
		dump_state(config_file, song_first, fst, CtrlCh);

	return 0;
}
