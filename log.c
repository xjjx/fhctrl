// log.c

#include <stdio.h>
#include <stdarg.h>

const char* logfile = "/tmp/fhctrl.log";

void clear_log() {
	remove ( logfile );
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
}
