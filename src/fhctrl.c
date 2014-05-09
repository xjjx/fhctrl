/*
   FSTHost Control by Xj 

   This is part of FSTHost sources

   Based on jack-midi-dump by Carl Hetherington
*/

#include <stdio.h>
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
#include "fhctrl.h"
#include "log.h"
#include "sysex.h"

#define WANT_SONG_NO 255
#define CTRL_CHANNEL 15
#define APP_NAME "FHControl"

/* From lcd.c */
extern void init_lcd( struct LCDScreen* lcd_screen );
extern void update_lcd( struct LCDScreen* lcd_screen );
extern void lcd_set_current_unit ( struct LCDScreen* lcd_screen, Unit* fp );

/* ftdi.h */
extern void lcd_close();

// Config file support
bool dump_state( FHCTRL* fhctrl, const char* config_file );
bool load_state( FHCTRL* fhctrl, const char* config_file );

/* Functions */
static bool choke_check ( uint8_t* choke ) {
	if ( *choke > 0 ) {
		(*choke)--;
		return true;
	}
	return false;
}

void send_ident_request ( FHCTRL* fhctrl ) {
	unit_reset_to_na ( fhctrl->unit );

	FJACK* fjack = (FJACK*) fhctrl->user;
	fjack_send_ident_request ( fjack );
}

void send_dump_request( FHCTRL* fhctrl , short id ) {
	FJACK* fjack = (FJACK*) fhctrl->user;
	fjack_send_dump_request ( fjack , id );
}

static void inline
send_offer ( FHCTRL* fhctrl, SysExIdentReply* reply ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	uint8_t uuid = unit_uniqe_id (fhctrl->unit, fhctrl->offered_last + 1);
	if ( uuid == 0 ) return; /* No more free id ? :-( */

	fjack_send_offer ( fjack, reply, uuid );

	fhctrl->offered_last_choke = 10;
	fhctrl->offered_last = uuid;
}

void fhctrl_unit_send ( FHCTRL* fhctrl, Unit* fp, const char* logFuncName ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	switch ( fp->type ) {
	case UNIT_TYPE_DEVICE: ;
		// For devices just send ProgramChange
		jack_midi_data_t pc[2];
		pc[0] = ( fp->state->channel - 1 ) & 0x0F;
		pc[0] |= 0xC0;
		pc[1] = fp->state->program & 0x0F;
		fjack_send ( fjack, &pc, sizeof pc, logFuncName, fp->id );
		break;
	case UNIT_TYPE_PLUGIN: ;
		// If unit is NA then keep it state and skip sending
		SysExDumpV1 sysex = SYSEX_DUMP;
		unit_set_sysex ( fp, &sysex );
		fjack_send ( fjack, &sysex, sizeof sysex, logFuncName, fp->id );
		break;
	}
}

void fhctrl_song_send (FHCTRL* fhctrl, short SongNumber) {
	fhctrl->want_song = WANT_SONG_NO;

	Song* song = song_get (fhctrl->songs, SongNumber);
	if (!song) return;

	LOG ( "SendSong \"%s\"", song->name );

	// Dump states via SysEx - for all FST
	short i;
	for (i=0; i < MAX_UNITS; i++) {
		Unit* fp = fhctrl->unit[i];
		if ( ! fp ) continue;

		/* Save current state */
		enum State curState = fp->state->state;

		// If unit state in song is NA do not copy plugin state
		UnitState* songFS = song->unit_state[i];
		if ( songFS->state == UNIT_NA ) {
			switch ( fp->type ) {
			case UNIT_TYPE_PLUGIN:
				// For plugin type this mean set it to bypass
				// if plugin is already in BYPASS state then skip it
				if ( fp->state->state == UNIT_STATE_BYPASS ) {
					continue;
				} else {
					fp->state->state = UNIT_STATE_BYPASS;
				}
				break;
			default: // For rest .. mean do nothing
				continue;
			}
		} else {
			/* Copy state from song */
			*fp->state = *songFS;
		}

		// Send state to unit
		if (curState == UNIT_NA && fp->type != UNIT_TYPE_DEVICE) {
			// If unit was NA then keep it state and skip sending
			// NOTE: non-sysex devices does not support NA state
			//       cause they can't be discovered
			fp->state->state = UNIT_NA;
		} else {
			fhctrl_unit_send ( fhctrl, fp, "SongSend" );
		}
		fp->change = true; // Update display
	}
}

static void fjack_session_reply ( FJACK* fjack ) {
	LOG("session callback");

	jack_session_event_t* sev = fjack->session_event;
	sev->flags |= JackSessionNeedTerminal;

	char *restore_cmd = malloc(256);
	snprintf ( restore_cmd, 256, "fhctrl \"${SESSION_DIR}state.cfg\" %s", sev->client_uuid );
	sev->command_line = restore_cmd;

	FHCTRL* fhctrl = (FHCTRL*) fjack->user;
	// Save state, set error if fail
	char filename[FILENAME_MAX];
	snprintf (filename, sizeof filename, "%sstate.cfg", sev->session_dir);

	//FIXME: update config file ?
	//fhctrl->config_file = filename;

	if ( ! dump_state ( fhctrl, filename ) )
		sev->flags |= JackSessionSaveError;

	jack_session_reply ( fjack->client, sev );

	// TODO: quit on jack request
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
	// Don't process realitime messages
	if ( data[0] > 0xEF ) return false;
	// Don't process not our channel
	if ( (data[0] & 0x0F) != CTRL_CHANNEL ) return false;
	// Shine
	fhctrl->gui.ctrl_midi_in = true;
	// Change song
	if ( (data[0] & 0xF0) == 0xC0 ) {
		fhctrl->want_song_choke = 10;
		fhctrl->want_song = data[1];
	}
	return true;
}

static inline void
fhctrl_handle_ident_reply ( FHCTRL* fhctrl, SysExIdentReply* r ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	// If this is Unit then try to deal with him ;-)
	if ( r->id == SYSEX_MYID ) {
		uint8_t UnitID = r->model[0];
		if ( UnitID == 0 ) {
			send_offer ( fhctrl, r );
		} else {
			// Note: we refresh GUI when dump back to us
			Unit* fp = unit_get ( fhctrl->unit, fhctrl->songs, UnitID );
			fp->type = UNIT_TYPE_PLUGIN; // We just know this ;-)

			if ( fp->state->state == UNIT_NA ) {
				// Unit is new or NA after ident request
				send_dump_request (fhctrl, fp->id);
			} else {
				// Unit is avaiable while it sent ident reply ? try recover
				collect_rt_logs(fjack, "IdentReply: recovering unit %X", fp->id);
				fhctrl_unit_send ( fhctrl, fp, "IdentReply" );
			}
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

	/* This also create new Unit if needed */
	/* FIXME: this use unit_new which can use calloc for new units */
	Unit* fp = unit_get_from_sysex ( fhctrl->unit, fhctrl->songs, sysex );

	lcd_set_current_unit ( &fhctrl->lcd_screen, fp );
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

		int ret = jack_midi_event_write (
			outbuf,
			0, // First frame
			(const jack_midi_data_t *) &tmpbuf,
			size
		);
		if ( ret != 0 ) collect_rt_logs(j, "SendOur - Write dropped");
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

	jack_nframes_t count = jack_midi_get_event_count (inbuf);
	jack_nframes_t i;
	for (i = 0; i < count; ++i) {
		jack_midi_event_t event;
		if (jack_midi_event_get (&event, inbuf, i) != 0) break;

		/* Filter out active sensing */
		if ( event.buffer[0] == 0xFE ) continue;

		/* Process and filter out our messages */
		if ( ctrl_channel_handling ( fhctrl, event.buffer ) ) continue;

		fhctrl->gui.midi_in = true;
		if ( jack_midi_event_write(outbuf, event.time, event.buffer, event.size) ) {
			collect_rt_logs(j, "Forward - Write dropped (buffer size: %d)", jack_midi_max_event_size(outbuf));
			break;
		}
	}
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
	fjack_in_port_handling ( fjack, frames );

	/* Send our queued messages */
	fjack_out_port_handling ( fjack, frames );

	/* Forward port handling */
	fjack_forward_port_handling ( fjack, frames );

	return 0;
}

void update_config (FHCTRL* fhctrl) {
	if (fhctrl->config_file)
		dump_state ( fhctrl, fhctrl->config_file );
}

// Will be called from nfhc
void fhctrl_idle ( FHCTRL* fhctrl ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	/* Change song */
	if ( fhctrl->want_song != WANT_SONG_NO
	  && choke_check ( &(fhctrl->want_song_choke) )
	) fhctrl_song_send ( fhctrl, fhctrl->want_song );

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
	if ( choke_check ( &(fhctrl->offered_last_choke) ) ) {
		// Send SysEx Ident request if found some N/A units
		// NOTE: don't trigger for non-sysex units
		if ( fhctrl->graph_order_changed > 0 &&
		     --(fhctrl->graph_order_changed) == 0 &&
		     unit_is_any_na ( fhctrl->unit )
		) {
			send_ident_request ( fhctrl );
		}

		if (fhctrl->offered_last > 0) fhctrl->offered_last = 0;
	};
}

void fhctrl_init( FHCTRL* fhctrl, void* user_ptr ) {
	fhctrl->user = user_ptr;
	fhctrl->songs = &fhctrl->song_first;
	fhctrl->want_song = WANT_SONG_NO;

	/* try on start */
	fhctrl->try_connect_to_physical = true;

	/* Try detect on start */
	fhctrl->graph_order_changed = 1;
}

#ifndef GUI
volatile bool quit;
static void signal_handler (int signum) {
	switch(signum) {
	case SIGINT:
		puts("Caught signal to terminate (SIGINT)");
		quit = true;
		break;
	}
}

void justLOG ( char *msg, void *user_data ) {
	puts ( msg );
}
#endif

int main (int argc, char* argv[]) {
	log_init ();

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
	fhctrl.lcd_screen.app_name = APP_NAME;
	init_lcd( &fhctrl.lcd_screen );

	// Try read file
	if (fhctrl.config_file) load_state( &fhctrl, fhctrl.config_file );

#ifdef GUI
	// ncurses GUI loop
	nfhc(&fhctrl);
#else
	// Handling SIGINT for clean quit
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = &signal_handler;
	sigaction(SIGINT, &sa, NULL);

	set_logcallback ( justLOG, NULL );

	// loop
	quit = false;
	while ( ! quit ) {
		fhctrl_idle ( &fhctrl );
		sleep ( 1 );
	}
#endif

	jack_deactivate ( fjack.client );
	jack_client_close ( fjack.client );

	// Close LCD
	if (fhctrl.lcd_screen.available) lcd_close();

	log_close ();

	return 0;
}
