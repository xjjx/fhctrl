#ifndef __fjack_h__
#define __fjack_h__

#include <stdbool.h>
#include <stdint.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/session.h>
#include <jack/ringbuffer.h>

typedef struct _FJACK {
	jack_client_t*		client;
	jack_port_t*		in;
	jack_port_t*		out;
	jack_port_t*		fin;
	jack_port_t*		fout;
	jack_nframes_t		buffer_size;
	jack_nframes_t		sample_rate;
	jack_session_event_t*	session_event;
	const char*		session_uuid;
	bool			need_ses_reply;
	jack_ringbuffer_t*	log_collector;
	jack_ringbuffer_t*	buffer_midi_out;
	void*			user;
} FJACK;

void collect_rt_logs ( FJACK* fjack, char *fmt, ... );

bool queue_midi_out (jack_ringbuffer_t* rbuf, jack_midi_data_t* data, size_t size, const char* What, int8_t id);

void connect_to_physical ( FJACK* fjack );

void get_rt_logs ( FJACK* fjack );

void collect_rt_logs ( FJACK* fjack, char *fmt, ... );

void fjack_init ( FJACK* fjack, const char* client_name, void* user_ptr );

#endif /* __fjack_h__ */

