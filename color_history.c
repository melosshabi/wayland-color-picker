#include "color_history.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

static gchar *get_history_file_path(){
    const gchar *config_dir = g_get_user_config_dir();
    gchar *app_dir = g_build_filename(config_dir, "waypicker", NULL);
    g_mkdir_with_parents(app_dir, 0755);
    gchar *file_path = g_build_filename(app_dir, HISTORY_FILE, NULL);
    g_free(app_dir);

    return file_path;
}

GList *load_color_history(){
    gchar *file_path = get_history_file_path();
    GList *history = NULL;

    if(!g_file_test(file_path, G_FILE_TEST_EXISTS)){
        g_free(file_path);
        return NULL;
    }

    gchar *contents = NULL;
    GError *error = NULL;
    if(!g_file_get_contents(file_path, &contents, NULL, &error)){
        g_printerr("Failed to read history: %s\n", error->message);
        g_clear_error(&error);
        g_free(file_path);
        return NULL;
    }

    JsonParser *parser = json_parser_new();
    if(!json_parser_load_from_data(parser, contents, -1, &error)){
        g_printerr("Failed to parse history JSON: %s\n", error->message);
        g_clear_error(&error);
        g_free(contents);
        g_free(file_path);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *root_obj = json_node_get_object(root);
    JsonArray *colors = json_object_get_array_member(root_obj, "colors");

    guint length = json_array_get_length(colors);
    for(guint i = 0; i < length; i++){
        JsonObject *color_obj = json_array_get_object_element(colors, i);
        ColorEntry *entry = g_new0(ColorEntry, 1);
        entry->hex_color = g_strdup(json_object_get_string_member(color_obj, "hex"));
        entry->timestamp = json_object_get_int_member(color_obj, "timestamp");

        history = g_list_append(history, entry);
    }
    g_free(contents);
    g_free(file_path);
    g_object_unref(parser);
    return history;
}

void save_color_to_history(const char *hex_color){
    gchar *file_path = get_history_file_path();
    GList *history = load_color_history();

    // Check for duplicates
    for(GList *l = history; l != NULL; l = l->next){
        ColorEntry *entry = l->data;
        // Compare strings
        // g_strcmp0 returns 0 when strings are equal
        if(g_strcmp0(entry->hex_color, hex_color) == 0){
            // Move to fron by removing and re-adding
            history = g_list_remove(history, entry);
            g_free(entry->hex_color);
            g_free(entry);
            break;
        }
    }

    ColorEntry *new_entry = g_new0(ColorEntry, 1);
    new_entry->hex_color = g_strdup(hex_color);
    new_entry->timestamp = g_get_real_time();

    history = g_list_prepend(history, new_entry);

    if(g_list_length(history) > MAX_HISTORY){
        GList *last = g_list_last(history);
        ColorEntry *old_entry = last->data;
        g_free(old_entry->hex_color);
        g_free(old_entry);
        history = g_list_delete_link(history,last);
    }

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "colors");
    json_builder_begin_array(builder);

    for(GList *l = history; l != NULL; l = l->next){
        ColorEntry *entry = l->data;
       json_builder_begin_object(builder);
       json_builder_set_member_name(builder, "hex");
       json_builder_add_string_value(builder, entry->hex_color);
       json_builder_set_member_name(builder, "timestamp");
       json_builder_add_int_value(builder, entry->timestamp);
       json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
    json_builder_end_object(builder);

    // Write the changes to file
    JsonNode *root = json_builder_get_root(builder);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, root);
    json_generator_set_pretty(gen, TRUE);

    gchar *json_str = json_generator_to_data(gen, NULL);
    GError *error = NULL;
    g_file_set_contents(file_path, json_str, -1, &error);

    if(error){
        g_printerr("Failed to save history: %s\n", error->message);
        g_clear_error(&error);
    }

    g_free(json_str);
    g_free(file_path);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
    free_color_history(history);
}

void free_color_history(GList *history){
    for(GList *l = history; l != NULL; l = l->next){
        ColorEntry *entry = l->data;
        g_free(entry->hex_color);
        g_free(entry);
    }
    g_list_free(history);
}
