// log.c

#include <stdio.h>
#include <stdarg.h>
#include <semaphore.h>

#include "log.h"

static const char* logfile = "/tmp/fhctrl.log";
static volatile LOGCALLBACK logcallback = NULL;
static volatile void* logcallback_user_data = NULL;
sem_t logsema;

void clear_log() {
	remove ( logfile );
}

void set_logcallback( LOGCALLBACK lcb, void *user_data ) {
	sem_wait ( &logsema );
	logcallback = lcb;
	logcallback_user_data = user_data;
	sem_post ( &logsema );
}

const char* get_logpath () {
	return logfile;
}

void log_init () {
	sem_init ( &logsema, 0 , 1 );
}

void LOG ( char *fmt, ... ) {
	FILE *f = fopen( logfile , "a" );
	if (!f) {
		fprintf(stderr, "Can't open logfile: %s\n", logfile);
		return;
	}

	va_list args;
	va_start ( args, fmt );
	vfprintf ( f, fmt, args );
	va_end ( args );

	fputc ( '\n', f );
	fclose( f );

	/* Send message to defined user callback ( e.g. nLOG ) */
	sem_wait ( &logsema );
	if ( logcallback != NULL && logcallback_user_data != NULL ) {
		char msg[256];
		va_start ( args, fmt );
		vsnprintf ( msg, sizeof msg, fmt, args );
		va_end ( args );
		void* ud = (void*) logcallback_user_data;
		logcallback( msg, ud );
	}
	sem_post ( &logsema );
}
