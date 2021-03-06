#ifndef __sysex_h__
#define __sysex_h__

/*
  SysEx parser - part of FST host
*/

#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define SYSEX_MAX_SIZE sizeof(SysExDumpV1)
#define SYSEX_BEGIN 0xF0
#define SYSEX_END 0xF7
#define SYSEX_NON_REALTIME 0x7E
#define SYSEX_GENERAL_INFORMATION 0x06
#define SYSEX_IDENTITY_REQUEST 0x01
#define SYSEX_IDENTITY_REPLY 0x02
#define SYSEX_MYID 0x5B
#define SYSEX_VERSION 1
#define SYSEX_AUTO_ID 0

typedef enum {
	SYSEX_TYPE_NONE, // Stopper ;-)
	SYSEX_TYPE_DUMP,
	SYSEX_TYPE_RQST,
	SYSEX_TYPE_REPLY,
	SYSEX_TYPE_OFFER,
	SYSEX_TYPE_DONE,
	SYSEX_TYPE_RELOAD
} SysExType;

/* ----------------- Universal SysEx ---------------------------- */
#define SYSEX_IDENT_REQUEST {SYSEX_BEGIN,SYSEX_NON_REALTIME,0x7F,SYSEX_GENERAL_INFORMATION,SYSEX_IDENTITY_REQUEST,SYSEX_END}
typedef struct {
	const uint8_t begin;
	const uint8_t type;
       	const uint8_t target_id;
	const uint8_t gi;
	const uint8_t ir;
	const uint8_t end;
} SysExIdentRqst;

#define SYSEX_IDENT_REPLY {SYSEX_BEGIN,SYSEX_NON_REALTIME,0x7F,SYSEX_GENERAL_INFORMATION,SYSEX_IDENTITY_REPLY,\
   SYSEX_MYID,{0},{SYSEX_AUTO_ID,0},{SYSEX_VERSION,0,0,0},SYSEX_END}
typedef struct {
	const uint8_t begin;
	const uint8_t type;
       	const uint8_t target_id;
	const uint8_t gi;
	const uint8_t ir;
	const uint8_t id;
	const uint8_t family[2];
	uint8_t model[2]; // Here we set sysex_uuid as [0]
	uint8_t version[4]; // Here we set sysex_random_id for all elements
	const uint8_t end;
} SysExIdentReply;

/* ------------------- FSTHost own SysEx ------------------------ */
typedef struct {
	const uint8_t begin;
	const uint8_t id;
	const uint8_t version;
	const uint8_t type;
	uint8_t uuid;
} SysExHeader;

#define SYSEX_OFFER {SYSEX_BEGIN,SYSEX_MYID,SYSEX_VERSION,SYSEX_TYPE_OFFER,0,{0},SYSEX_END}
typedef struct {
	const uint8_t begin;
	const uint8_t id;
	const uint8_t version;
	const uint8_t type;
	uint8_t uuid; // Offered ID
	/* --- HEADER --- */

	uint8_t rnid[4]; // Copy of SysExIdentReply.version
	const uint8_t end;
} SysExIdOffer;

#define SYSEX_DUMP_REQUEST {SYSEX_BEGIN,SYSEX_MYID,SYSEX_VERSION,SYSEX_TYPE_RQST,0,SYSEX_END}
typedef struct {
	const uint8_t begin;
	const uint8_t id;
	const uint8_t version;
	const uint8_t type;
	uint8_t uuid;
	/* --- HEADER --- */

	const uint8_t end;
} SysExDumpRequestV1;

enum SysExState {
	SYSEX_STATE_NOACTIVE = 0,
	SYSEX_STATE_ACTIVE   = 1
};

#define SYSEX_DUMP {SYSEX_BEGIN,SYSEX_MYID,SYSEX_VERSION,SYSEX_TYPE_DUMP,0,0,0,0,0,{0},{0},SYSEX_END}
typedef struct {
	const uint8_t begin;
	const uint8_t id;
	const uint8_t version;
	const uint8_t type;
	uint8_t uuid;
	/* --- HEADER --- */

	uint8_t state;
	uint8_t program;
	uint8_t channel;
	uint8_t volume;
	uint8_t program_name[24]; /* Last is always 0 */
	uint8_t plugin_name[24]; /* Last is always 0 */
	const uint8_t end;
} SysExDumpV1;

#define SYSEX_DONE {SYSEX_BEGIN,SYSEX_MYID,SYSEX_VERSION,SYSEX_TYPE_DONE,0,SYSEX_END}
typedef struct {
	const uint8_t begin;
	const uint8_t id;
	const uint8_t version;
	const uint8_t type;
	uint8_t uuid;
	/* --- HEADER --- */

	const uint8_t end;
} SysExDone;

#define SYSEX_RELOAD {SYSEX_BEGIN,SYSEX_MYID,SYSEX_VERSION,SYSEX_TYPE_RELOAD,0,SYSEX_END}
typedef struct {
	const uint8_t begin;
	const uint8_t id;
	const uint8_t version;
	const uint8_t type;
	uint8_t uuid;
	/* --- HEADER --- */

	const uint8_t end;
} SysExReload;

static inline const char*
SysExType2str ( SysExType type ) {
	switch ( type ) {
	case SYSEX_TYPE_NONE:	return "UNKNOWN";
	case SYSEX_TYPE_DUMP:	return "DUMP";
	case SYSEX_TYPE_RQST:	return "REQUEST";
	case SYSEX_TYPE_REPLY:	return "REPLY";
	case SYSEX_TYPE_OFFER:	return "OFFER";
	case SYSEX_TYPE_DONE:	return "DONE";
	case SYSEX_TYPE_RELOAD:	return "RELOAD";
	}
	return "UNKNOWN";
}

#endif /* __sysex_h__ */

