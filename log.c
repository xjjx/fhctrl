// log.c

#include <stdio.h>
#include <stdarg.h>

#include "log.h"

static const char* logfile = "/tmp/fhctrl.log";
static LOGCALLBACK logcallback = NULL;
static void* logcallback_user_data = NULL;

void clear_log() {
	remove ( logfile );
}

void set_logcallback( LOGCALLBACK lcb, void *user_data ) {
	logcallback = lcb;
	logcallback_user_data = user_data;
}

const char* get_logpath () {
	return logfile;
}

void LOG(char *fmt, ...) {
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

	if (logcallback) {
		char msg[256];
		va_start ( args, fmt );
		vsnprintf ( msg, sizeof msg, fmt, args );
		va_end ( args );
		logcallback( msg, logcallback_user_data );
	}
}
