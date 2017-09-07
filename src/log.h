#ifndef __log_h__
#define __log_h__

typedef void (*LOGCALLBACK)(char *msg, void *user_data);

void log_init();
void log_close ();
void clear_log();
const char* get_logpath ();
void LOG(char *fmt, ...);
void set_logcallback( LOGCALLBACK lcb, void *user_data );

#endif /* __log_h__ */
