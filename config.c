#include <stdbool.h>
#include <libconfig.h>
#include <string.h>

#include "fhctrl.h"

extern struct FSTPlug* fst_get(uint8_t uuid);
extern struct Song* song_new();

bool dump_state(char const* config_file, struct Song **song_first, struct FSTPlug **fst) {
	short i, j, sn = 0;
	int ret;
	char name[24];
	struct Song* s;
	struct FSTState* fs;
	config_t cfg;
	config_setting_t* group;
	config_setting_t* song_name;
	config_setting_t* list;

	config_init(&cfg);

	// Save plugs
	group = config_setting_add(cfg.root, "global", CONFIG_TYPE_GROUP);
	for (i = j = 0; i < 128; i++) {
		if (fst[i] == NULL) continue;

		snprintf(name, sizeof name, "plugin%d", j++);
		list = config_setting_add(group, name, CONFIG_TYPE_LIST);
		config_setting_set_int_elem(list, -1, fst[i]->id);
		config_setting_set_int_elem(list, -1, fst[i]->type);
		config_setting_set_string_elem(list, -1, fst[i]->name);
	}

	// Save songs
	for(s = *song_first; s; s = s->next) {
		snprintf(name, sizeof name, "song%d", sn++);
		group = config_setting_add(cfg.root, name, CONFIG_TYPE_GROUP);

		song_name = config_setting_add(group, "name", CONFIG_TYPE_STRING);
		config_setting_set_string( song_name, s->name );
		for (i = j = 0; i < 128; i++) {
			if (fst[i] == NULL) continue;

			fs = s->fst_state[i];

			snprintf(name, sizeof name, "plugin%d", j++);
			list = config_setting_add(group, name, CONFIG_TYPE_LIST);
			config_setting_set_int_elem(list, -1, fs->state);
			config_setting_set_int_elem(list, -1, fs->program);
			config_setting_set_int_elem(list, -1, fs->channel);
			config_setting_set_int_elem(list, -1, fs->volume);
			config_setting_set_string_elem(list, -1, fs->program_name);
		}
	}

	ret = config_write_file(&cfg, config_file);
	config_destroy(&cfg);

	if (ret == CONFIG_TRUE) {
		LOG("Save to %s OK", config_file);
		return true;
	} else {
		LOG("Save to %s Fail", config_file);
		return false;
	}
}

bool load_state(const char* config_file, struct Song **song_first, struct FSTPlug **fst) {
	struct FSTPlug* f;
	struct FSTState* fs;
	struct Song* song;
	config_t cfg;
	config_setting_t* global;
	config_setting_t* list;
	char name[24];
	const char* sparam;
	const char* plugName;
	unsigned short id, i, s;

	config_init(&cfg);
	if (!config_read_file(&cfg, config_file)) {
		LOG("%s:%d - %s",
			config_file,
			config_error_line(&cfg),
			config_error_text(&cfg)
		);
		config_destroy(&cfg);
		return false;
	}

	// Global section
	global = config_lookup(&cfg, "global");
	for(i=0; i < config_setting_length(global); i++) {
		list = config_setting_get_elem(global, i);
		plugName = config_setting_name(list);

		id = config_setting_get_int_elem(list, 0);
		f = fst_get(id);
		f->type = config_setting_get_int_elem(list, 1);

		sparam = config_setting_get_string_elem(list, 2);
		strcpy(f->name, sparam);

		// Songs iteration
		s = 0;
		song = *song_first;

again:
		snprintf(name, sizeof name, "song%d", s);
		if (config_lookup(&cfg, name) == NULL) continue;

		// Create new song if needed
		if (! song) {
			song = song_new();
			snprintf(name, sizeof name, "song%d.name", s);
			const char* value;
			if ( config_lookup_string ( &cfg, name, &value ) ) {
				strncpy( song->name, value, 23 );
			}
		}

		snprintf(name, sizeof name, "song%d.%s", s++, plugName);
		list = config_lookup(&cfg, name);
		if (list != NULL) {
			fs = song->fst_state[id];
			fs->state = config_setting_get_int_elem(list, 0);
			fs->program = config_setting_get_int_elem(list, 1);
			fs->channel = config_setting_get_int_elem(list, 2);
			fs->volume = config_setting_get_int_elem(list, 3);
			sparam = config_setting_get_string_elem(list, 4);
			if (sparam) strcpy(fs->program_name, sparam);
		}
		song = song->next;
		goto again;
	}

	config_destroy(&cfg);

	return true;
}
