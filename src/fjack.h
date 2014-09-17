#ifndef __fjack_h__
#define __fjack_h__

#include <stdbool.h>
#include <stdint.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/session.h>
#include <jack/ringbuffer.h>

#include "sysex.h"

typedef struct _FJACK {
	jack_client_t*		client;
	jack_port_t*		in;
	jack_port_t*		out;
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

bool fjack_send ( FJACK* fjack, void* data, size_t size, const char* What, int8_t id );

void connect_to_physical ( FJACK* fjack );

void get_rt_logs ( FJACK* fjack );

void collect_rt_logs ( FJACK* fjack, char *fmt, ... );

void fjack_init ( FJACK* fjack, const char* client_name, void* user_ptr );

void fjack_send_ident_request ( FJACK* fjack );

void fjack_send_offer ( FJACK* fjack, SysExIdentReply* reply, uint8_t uuid );

void fjack_send_dump_request ( FJACK* fjack , short id );

void fjack_send_reload ( FJACK* fjack, uint8_t uuid );

#endif /* __fjack_h__ */
