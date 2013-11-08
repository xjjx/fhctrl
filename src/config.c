#include <stdbool.h>
#include <libconfig.h>
#include <string.h>

#include "fhctrl.h"
#include "log.h"

bool dump_state( FHCTRL* fhctrl, const char* config_file ) {
	char name[24];

	config_t cfg;
	config_init(&cfg);

	// Save plugs
	config_setting_t* group = config_setting_add(cfg.root, "global", CONFIG_TYPE_GROUP);
	short i, j;
	for (i = j = 0; i < 128; i++) {
		if (fhctrl->fst[i] == NULL) continue;

		snprintf(name, sizeof name, "plugin%d", j++);

		config_setting_t* list = config_setting_add(group, name, CONFIG_TYPE_LIST);
		config_setting_set_int_elem(list, -1, fhctrl->fst[i]->id);
		config_setting_set_int_elem(list, -1, fhctrl->fst[i]->type);
		config_setting_set_string_elem(list, -1, fhctrl->fst[i]->name);
	}

	// Save songs
	Song* s;
	short sn = 0;
	for(s = song_first(fhctrl->songs); s; s = s->next) {
		snprintf(name, sizeof name, "song%d", sn++);
		group = config_setting_add(cfg.root, name, CONFIG_TYPE_GROUP);

		config_setting_t* song_name = config_setting_add(group, "name", CONFIG_TYPE_STRING);
		config_setting_set_string( song_name, s->name );
		short i, j;
		for (i = j = 0; i < 128; i++) {
			if (fhctrl->fst[i] == NULL) continue;

			FSTState* fs = s->fst_state[i];

			snprintf(name, sizeof name, "plugin%d", j++);

			config_setting_t* list = config_setting_add(group, name, CONFIG_TYPE_LIST);
			config_setting_set_int_elem(list, -1, fs->state);
			config_setting_set_int_elem(list, -1, fs->program);
			config_setting_set_int_elem(list, -1, fs->channel);
			config_setting_set_int_elem(list, -1, fs->volume);
			config_setting_set_string_elem(list, -1, fs->program_name);
		}
	}

	int ret = config_write_file(&cfg, config_file);
	config_destroy(&cfg);

	if (ret == CONFIG_TRUE) {
		LOG("Save to %s OK", config_file);
		return true;
	} else {
		LOG("Save to %s Fail", config_file);
		return false;
	}
}

bool load_state( FHCTRL* fhctrl, const char* config_file ) {
	config_t cfg;
	config_init(&cfg);
	if (!config_read_file(&cfg, config_file)) {
		LOG("%s:%d - %s",
			fhctrl->config_file,
			config_error_line(&cfg),
			config_error_text(&cfg)
		);
		config_destroy(&cfg);
		return false;
	}

	// Get root
	config_setting_t* root = config_root_setting ( &cfg );
	if ( ! root ) return false; // WTF ?

	// Global section
	config_setting_t* global = config_setting_get_member ( root, "global" );
	unsigned short i;
	for (i=0; i < config_setting_length(global); i++) {
		config_setting_t* list = config_setting_get_elem ( global, i );

		const char* plugName = config_setting_name (list);
		if ( ! plugName ) continue; // this is error

		// Is this a proper plugin entry ?
		if ( strstr ( plugName, "plugin" ) != plugName )
			continue;

		// Unit ID
		unsigned short id = config_setting_get_int_elem(list, 0);

		// Create/Get unit for speific id
		FSTPlug* fp = fst_get ( fhctrl->fst, fhctrl->songs, id );

		// Unit type
		fp->type = config_setting_get_int_elem ( list, 1 );

		// Unit name
		const char* sparam = config_setting_get_string_elem ( list, 2 );
		strncpy ( fp->name, sparam, 23 );

		// Read unit states in songs
		Song* song = song_first(fhctrl->songs);
		unsigned short j;
		for (j=0; j < config_setting_length(root); j++ ) {
			config_setting_t* song_group = config_setting_get_elem ( root, j );

			const char* elem_name = config_setting_name ( song_group );
			if ( ! elem_name ) continue; // this is error

			// If elem_name does not start with song ...
			if ( strstr ( elem_name, "song" ) != elem_name )
				continue;

			// Song number
			unsigned short song_number = strtol ( elem_name + 4, NULL, 10 );

			// Create new song if needed
			if (! song) {
				song = song_new (fhctrl->songs, fhctrl->fst );

				// Song name
				const char* song_name;
				int ret = config_setting_lookup_string ( song_group, "name", &song_name );
				if ( ret == CONFIG_TRUE ) {
					strncpy ( song->name, song_name, 23 );
				} else { // Set default name
					snprintf( song->name, 24, "Song %d", song_number);
				}
			}

			// Unit state in this song
			config_setting_t* plug_list = config_setting_get_member ( song_group, plugName );

			// Maybe this song doesn't have state for this unit ?
			if ( plug_list ) {
				FSTState* fs = song->fst_state[id];
				fs->state	= config_setting_get_int_elem		( plug_list, 0 );
				fs->program	= config_setting_get_int_elem		( plug_list, 1 );
				fs->channel	= config_setting_get_int_elem		( plug_list, 2 );
				fs->volume	= config_setting_get_int_elem		( plug_list, 3 );
				const char* pname = config_setting_get_string_elem	( plug_list, 4 );
				if (pname) strncpy ( fs->program_name, pname, 23 );
			}
			song = song->next;
		}
	}

	config_destroy(&cfg);
	return true;
}
