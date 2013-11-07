/*
   FSTHost Control by Xj 

   This is part of FSTHost sources

   Based on jack-midi-dump by Carl Hetherington
*/

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/session.h>
#include <jack/ringbuffer.h>

#include "basics.h"
#include "fjack.h"
#include "sysex.h"
#include "fhctrl.h"
#include "log.h"

#include "ftdilcd.h"

#define CTRL_CHANNEL 15
#define APP_NAME "FHControl"

// Config file support
bool dump_state( FHCTRL* fhctrl );
bool load_state( FHCTRL* fhctrl );

/* Functions */
void send_ident_request( FHCTRL* fhctrl ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	fst_reset_to_na ( fhctrl->fst );

	LOG("Send ident request");
	SysExIdentRqst sysex_ident_request = SYSEX_IDENT_REQUEST;
	queue_midi_out(
		fjack->buffer_midi_out,
		(jack_midi_data_t*) &sysex_ident_request,
		sizeof(SysExIdentRqst),
		"SendIdentRequest",
		-1
	);
}

void send_dump_request( FHCTRL* fhctrl , short id ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	SysExDumpRequestV1 sysex_dump_request = SYSEX_DUMP_REQUEST;
	sysex_dump_request.uuid = id;
	LOG("Sent dump request for ID:%d", id);
	queue_midi_out(
		fjack->buffer_midi_out,
		(jack_midi_data_t*) &sysex_dump_request,
		sizeof(SysExDumpRequestV1),
		"SendDumpRequest",
		id
	);
}

static void inline
send_offer ( FHCTRL* fhctrl, SysExIdentReply* r ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	SysExIdOffer sysex_offer = SYSEX_OFFER;

	/* No more free id ? :-( */
	if ( (sysex_offer.uuid = fst_uniqe_id(fhctrl->fst, fhctrl->offered_last + 1)) == 0 ) return;
	fhctrl->offered_last_choke = 10;

	memcpy(sysex_offer.rnid, r->version, sizeof(sysex_offer.rnid));
	fhctrl->offered_last = sysex_offer.uuid;

	collect_rt_logs( fjack, "Send Offer %d for %X %X %X %X", sysex_offer.uuid,
		sysex_offer.rnid[0], sysex_offer.rnid[1], sysex_offer.rnid[2], sysex_offer.rnid[3]
	);

	queue_midi_out(
		fjack->buffer_midi_out,
		(jack_midi_data_t*) &sysex_offer,
		sizeof(SysExIdOffer),
		"SendOffer",
		sysex_offer.uuid
	);
}

void fhctrl_fst_send ( FHCTRL* fhctrl, FSTPlug* fp, const char* logFuncName ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	jack_midi_data_t pc[2];
	SysExDumpV1 sysex = SYSEX_DUMP;
	jack_midi_data_t* data = NULL;
	size_t data_size = 0;

	switch ( fp->type ) {
	case FST_TYPE_DEVICE:
		// For devices just send ProgramChange
		pc[0] = ( fp->state->channel - 1 ) & 0x0F;
		pc[0] |= 0xC0;
		pc[1] = fp->state->program & 0x0F;
		data = (jack_midi_data_t*) &pc;
		data_size = sizeof pc;
		break;
	case FST_TYPE_PLUGIN:
		// If unit is NA then keep it state and skip sending
		fst_set_sysex ( fp, &sysex );
		data = (jack_midi_data_t*) &sysex;
		data_size = sizeof(SysExDumpV1);
		break;
	}
	queue_midi_out ( fjack->buffer_midi_out, data, data_size, logFuncName, fp->id );
}

void fhctrl_song_send (FHCTRL* fhctrl, short SongNumber) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	Song* song = song_get (fhctrl->songs, SongNumber);
	if (!song) return;
	
	collect_rt_logs( fjack, "SendSong \"%s\"", song->name);

	// Dump states via SysEx - for all FST
	short i;
	for (i=0; i < 128; i++) {
		FSTPlug* fp = fhctrl->fst[i];
		if ( ! fp ) continue;

		/* Save current state */
		enum State curState = fp->state->state;

		/* Copy state from song */
		*fp->state = *song->fst_state[i];
		
		// If unit is NA in Song then preserve it's current state and skip sending
		if ( fp->state->state == FST_NA ) {
			fp->state->state = curState;
			continue;
		}

		// Send state to unit
		if (curState == FST_NA) {
			// If unit is NA then keep it state and skip sending
			// NOTE: this is valid only for PLUGIN type units
			fp->state->state = FST_NA;
		} else {
			fhctrl_fst_send ( fhctrl, fp, "SongSend" );
		}

		fp->change = true; // Update display
	}
}

static void init_lcd( struct LCDScreen* lcd_screen ) {
	lcd_screen->available = lcd_init();
	if (! lcd_screen->available) return;

	char lcdline[16];
	snprintf(lcdline, 16, "%s", APP_NAME);
	lcd_text(0,0,lcdline);
	snprintf(lcdline, 16, "says HELLO");
	lcd_text(0,1,lcdline);
	lcd_screen->fst = NULL;
}

static void update_lcd( struct LCDScreen* lcd_screen ) {
	if (! lcd_screen->available) return;

	FSTPlug* fp = lcd_screen->fst;
	if(!fp) return;

	char line[24];
	snprintf(line, 24, "D%02d P%02d C%02d V%02d",
		fp->id,
		fp->state->program,
		fp->state->channel,
		fp->state->volume
	);
	lcd_text(0,0,line);			// Line 1
	lcd_text(0,1,fp->state->program_name);	// Line 2
	lcd_text(0,2,fp->name);			// Line 3
}

static inline void
lcd_set_current_fst ( struct LCDScreen* lcd_screen, FSTPlug* fp ) {
	if ( ! lcd_screen->available ) return;
	lcd_screen->fst = fp;
}

static void fjack_session_reply ( FJACK* fjack ) {
	LOG("session callback");

	FHCTRL* fhctrl = (FHCTRL*) fjack->user;

	char *restore_cmd = malloc(256);
	char filename[FILENAME_MAX];
	jack_session_event_t* sev = fjack->session_event;

	// Save state, set error if fail
	snprintf(filename, sizeof(filename), "%sstate.cfg", sev->session_dir);
	if ( ! dump_state( fhctrl ) )
		sev->flags |= JackSessionSaveError;

	sev->flags |= JackSessionNeedTerminal;

	snprintf(restore_cmd, 256, "fhctrl \"${SESSION_DIR}state.cfg\" %s", sev->client_uuid);
	sev->command_line = restore_cmd;

	jack_session_reply( fjack->client, sev );

//	if (event->type == JackSessionSaveAndQuit)

	jack_session_event_free(sev);
}

int graph_order_callback_handler ( void *arg ) {
	FJACK* fjack =(FJACK*) arg;
	FHCTRL* fhctrl = (FHCTRL*) fjack->user;
	// If our outport isn't connected to anyware - it's no sense to try send anything
	if ( jack_port_connected(fjack->out) ) fhctrl->graph_order_changed = 10;

	return 0;
}

void registration_handler(jack_port_id_t port_id, int reg, void *arg) {
	FJACK* j =(FJACK*) arg;
	FHCTRL* fhctrl = (FHCTRL*) j->user;
	if (reg != 0) fhctrl->try_connect_to_physical = true;
}

int cpu_load( FHCTRL* fhctrl ) {
	FJACK* fjack = (FJACK*) fhctrl->user;
	return (int) jack_cpu_load(fjack->client);
}

// Midi control channel handling - true if handled
static inline bool
ctrl_channel_handling ( FHCTRL* fhctrl, jack_midi_data_t data[] ) {
	if ( (data[0] & 0x0F) != CTRL_CHANNEL ) return false;

	// Don't shine for realitime messages
	if ( data[0] < 0xF0 ) fhctrl->gui.ctrl_midi_in = true;

	if ( (data[0] & 0xF0) == 0xC0 ) fhctrl_song_send( fhctrl, data[1] );
	return true;
}

static inline void
fjack_out_port_handling ( FJACK* j, jack_nframes_t frames ) {
	void* outbuf = jack_port_get_buffer(j->out, frames);
	assert (outbuf);
	jack_midi_clear_buffer(outbuf);

	while ( jack_ringbuffer_read_space(j->buffer_midi_out) ) {
		size_t size;
		jack_ringbuffer_peek(j->buffer_midi_out, (char*) &size, sizeof size);

		if ( size > jack_midi_max_event_size(outbuf) ) {
			collect_rt_logs(j, "No more space in midi buffer");
			break;
		}

		jack_ringbuffer_read_advance(j->buffer_midi_out, sizeof size);
		
		jack_midi_data_t tmpbuf[size];
		jack_ringbuffer_read(j->buffer_midi_out, (char*) &tmpbuf, size);

		if (jack_midi_event_write(outbuf, j->buffer_size - 1, (const jack_midi_data_t *) &tmpbuf, size) )
			collect_rt_logs(j, "SendOur - Write dropped");
	}
}

static inline void
fjack_forward_port_handling ( FJACK* j, jack_nframes_t frames ) {
	FHCTRL* fhctrl = (FHCTRL*) j->user;

	void* inbuf = jack_port_get_buffer (j->fin, frames);
	assert (inbuf);

	void* outbuf = jack_port_get_buffer(j->fout, frames);
	assert (outbuf);
	jack_midi_clear_buffer(outbuf);

	jack_nframes_t i, count;
	count = jack_midi_get_event_count (inbuf);
	for (i = 0; i < count; ++i) {
		jack_midi_event_t event;
		if (jack_midi_event_get (&event, inbuf, i) != 0) break;

		if ( ctrl_channel_handling ( fhctrl, event.buffer ) ) continue;

		/* Filer out unneded messages - mostly active sensing */
		if ( event.buffer[0] > 0xEF || event.buffer[0] < 0x80 ) continue;

		fhctrl->gui.midi_in = true;
		if ( jack_midi_event_write(outbuf, event.time, event.buffer, event.size) ) {
			collect_rt_logs(j, "Forward - Write dropped (buffer size: %d)", jack_midi_max_event_size(outbuf));
			break;
		}
	}
}

static inline void
fhctrl_handle_ident_reply ( FHCTRL* fhctrl, SysExIdentReply* r ) {
	// If this is FSTPlug then try to deal with him ;-)
	if ( r->id == SYSEX_MYID ) {
		if ( r->model[0] == 0 ) {
			send_offer ( fhctrl, r );
		} else {
			// Note: we refresh GUI when dump back to us
			FSTPlug* fp = fst_get ( fhctrl->fst, fhctrl->songs, r->model[0] );
			fp->type = FST_TYPE_PLUGIN; // We just know this ;-)
			send_dump_request (fhctrl, fp->id);
		}
	} /* else { Regular device - not supported yet } */
}

static inline void
fhctrl_handle_sysex_dump ( FHCTRL* fhctrl, SysExDumpV1* sysex ) {
	/* Dump from address zero mean unknown device - try fix this sending ident req */
	if (sysex->uuid == 0) {
		send_ident_request( fhctrl );
		return;
	}

	/* This also create new FSTPlug if needed */
	/* FIXME: this use fst_new which use calloc */
	FSTPlug* fp = fst_get_from_sysex ( fhctrl->fst, fhctrl->songs, sysex );

	lcd_set_current_fst ( &fhctrl->lcd_screen, fp );
}

static inline void
fjack_in_port_handling ( FJACK* j, jack_nframes_t frames ) {
	FHCTRL* fhctrl = (FHCTRL*) j->user;

	void* inbuf = jack_port_get_buffer (j->in, frames);
	assert (inbuf);

	jack_nframes_t i, count;
	count = jack_midi_get_event_count (inbuf);
	for (i = 0; i < count; ++i) {
		jack_midi_event_t event;
		if (jack_midi_event_get (&event, inbuf, i) != 0) break;

//		collect_rt_logs(j, "MIDI: %X", event.buffer[0]);

		if ( ctrl_channel_handling ( fhctrl, event.buffer ) ) continue;

		/* Is this sysex message ? */
		if ( event.size < 5 || event.buffer[0] != SYSEX_BEGIN ) continue;
		fhctrl->gui.sysex_midi_in = true;

		switch (event.buffer[1]) {
		case SYSEX_NON_REALTIME:
			// event.buffer[2] is target_id - in our case always 7F
			if ( event.buffer[3] == SYSEX_GENERAL_INFORMATION &&
			     event.buffer[4] == SYSEX_IDENTITY_REPLY
			) {
				SysExIdentReply* r = (SysExIdentReply*) event.buffer;
				collect_rt_logs(j, "Got SysEx ID Reply ID %X : %X", r->id, r->model[0]);
				fhctrl_handle_ident_reply ( fhctrl, r );
			}
			break;
		case SYSEX_MYID:
			if (event.size < sizeof(SysExDumpV1)) continue;

			SysExDumpV1* sysex = (SysExDumpV1*) event.buffer;
			if ( sysex->type == SYSEX_TYPE_DUMP ) {
				collect_rt_logs(j, "Got SysEx Dump %X : %s : %s", sysex->uuid, sysex->plugin_name, sysex->program_name);
				fhctrl_handle_sysex_dump ( fhctrl, sysex );
			}
			break;
		}
	}
}

int process (jack_nframes_t frames, void* arg) {
	FJACK* fjack = (FJACK*) arg;

	/* Read input */
	fjack_out_port_handling ( fjack, frames );

	/* Send our queued messages */
	fjack_out_port_handling ( fjack, frames );

	/* Forward port handling */
	fjack_forward_port_handling ( fjack, frames );

	return 0;
}

void update_config (FHCTRL* fhctrl) {
	if (fhctrl->config_file) dump_state ( fhctrl );
}

// Will be called from nfhc
void fhctrl_idle ( FHCTRL* fhctrl ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	/* Update LCD */
	if (fhctrl->gui.lcd_need_update) {
		fhctrl->gui.lcd_need_update = false;
		update_lcd( &fhctrl->lcd_screen );
	}

	/* If Jack need our answer */
	if (fjack->need_ses_reply) {
		fjack->need_ses_reply = false;
		fjack_session_reply( fjack );
	}

	/* Try connect to physical ports */
	if (fhctrl->try_connect_to_physical) {
		fhctrl->try_connect_to_physical = false;
		connect_to_physical( fjack );
	}

	/* discollect RT logs */
	get_rt_logs( fjack );

	/* Clear last offered and detect N/A (with some choke) */
	if (fhctrl->offered_last_choke == 0) {
		// Send SysEx Ident request if found some N/A units
		// NOTE: don't trigger for non-sysex units
		if ( fhctrl->graph_order_changed > 0 &&
		     --(fhctrl->graph_order_changed) == 0 &&
		     fst_is_any_na ( fhctrl->fst )
		) send_ident_request ( fhctrl );

		if (fhctrl->offered_last > 0) fhctrl->offered_last = 0;
	} else fhctrl->offered_last_choke--;
}

void fhctrl_init( FHCTRL* fhctrl, void* user_ptr ) {
	fhctrl->user = user_ptr;
	fhctrl->songs = &fhctrl->song_first;

	/* try on start */
	fhctrl->try_connect_to_physical = true;

	/* Try detect on start */
	fhctrl->graph_order_changed = 1;
}

int main (int argc, char* argv[]) {
	FHCTRL fhctrl = (FHCTRL) {0};
	FJACK fjack = (FJACK) {0};

	if (argc > 1) fhctrl.config_file = argv[1];
	if (argc > 2) fjack.session_uuid = argv[2];

	fhctrl_init ( &fhctrl, &fjack );
	fjack_init ( &fjack, APP_NAME, &fhctrl );

	clear_log();

	/* Try change terminal size */
	printf("\033[8;43;132t\n");
//	sleep(1); // Time for resize terminal

	// Init LCD
	init_lcd( &fhctrl.lcd_screen );

	// Try read file
	if (fhctrl.config_file) load_state( &fhctrl );

	// ncurses GUI loop
	nfhc(&fhctrl);

	jack_deactivate ( fjack.client );
	jack_client_close ( fjack.client );

	// Close LCD
	if (fhctrl.lcd_screen.available) lcd_close();

	return 0;
}
