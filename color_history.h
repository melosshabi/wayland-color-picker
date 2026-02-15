#ifndef COLOR_HISTORY_H
#define COLOR_HISTORY_H

#include <json-glib/json-glib.h>

#define MAX_HISTORY 50
#define HISTORY_FILE "color_history.json"

typedef struct {
    gchar *hex_color;
    gint64 timestamp;
}ColorEntry;

void save_color_to_history(const char *hex_color);
GList *load_color_history();
void free_color_history(GList *history);

#endif
