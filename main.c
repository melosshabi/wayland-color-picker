#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtkcssprovider.h>
#include <inttypes.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <libportal/portal.h>
#include "structs.h"
#include "resources.h"
#include "color_history.h"

static void go_back(GtkButton *button, gpointer user_data);
static void populate_color_history(GtkBox *container);
static void update_picture_scale(GtkWidget *pic, double scale) {
    int orig_w = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(pic), "orig-width"));
    int orig_h = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(pic), "orig-height"));

    gtk_widget_set_size_request(pic, orig_w * scale, orig_h * scale);
}
static gboolean on_scroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer user_data) {

    GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
    if (mods & GDK_SHIFT_MASK) {
        return FALSE;
    }
    if (mods & GDK_CONTROL_MASK) {
        ZoomState *zoom = user_data;

        zoom->scale *= dy < 0 ? 1.1 : 0.9;
        zoom->scale = CLAMP(zoom->scale, 0.2, 5.0);

        GtkWidget *pic = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));

        update_picture_scale(pic, zoom->scale);
        return TRUE;
    }
    return FALSE;
}
static gboolean destroy_toast(gpointer data){
    GtkWidget *toast = data;
    if(!GTK_IS_WIDGET(toast)){
        return G_SOURCE_REMOVE;
    }
    GtkWidget *pic = g_object_get_data(G_OBJECT(toast), "picture");
    if(pic && GTK_IS_WIDGET(pic)){
        GtkWidget *current_toast = g_object_get_data(G_OBJECT(pic), "active-toast");
        if(current_toast == toast){
         g_object_set_data(G_OBJECT(pic), "active-toast", NULL);
        }
    }
    GtkWidget *parent = gtk_widget_get_parent(toast);
    if(parent && GTK_IS_OVERLAY(parent)){
        gtk_overlay_remove_overlay(GTK_OVERLAY(parent), toast);
    }
    return G_SOURCE_REMOVE;
}
static void show_toast(GtkPicture *pic, const char *message) {
    // Remove the old toast if it exists
    GtkWidget *oldToast = g_object_get_data(G_OBJECT(pic), "active-toast");
    if(oldToast && GTK_IS_WIDGET(oldToast)){
        guint timeout_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(oldToast), "timeout-id"));
        if(timeout_id > 0){
            g_source_remove(timeout_id);
        }
        GtkWidget *parent = gtk_widget_get_parent(oldToast);
        if(parent && GTK_IS_OVERLAY(parent)){
            gtk_overlay_remove_overlay(GTK_OVERLAY(parent), oldToast);
        }
        g_object_set_data(G_OBJECT(pic), "active-toast", NULL);
    }
    GtkWidget *toast = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_add_css_class(toast, "toast");
    gtk_widget_set_valign(toast, GTK_ALIGN_END);
    gtk_widget_set_halign(toast, GTK_ALIGN_END);
    gtk_widget_set_margin_end(toast, 20);
    gtk_widget_set_margin_bottom(toast, 20);
    GtkWidget *label = gtk_label_new(message);
    gtk_box_append(GTK_BOX(toast), label);
    // GtkScrolledWindow automatically wraps its child in a GtkViewport
    GtkWidget *viewport = gtk_widget_get_parent(GTK_WIDGET(pic));
    GtkWidget *scrolled = gtk_widget_get_parent(viewport);
    GtkWidget *overlay = gtk_widget_get_parent(scrolled);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), toast);
    g_object_set_data(G_OBJECT(toast), "picture", pic);
    g_object_set_data(G_OBJECT(pic), "active-toast", NULL);
    g_object_set_data(G_OBJECT(pic), "active-toast", toast);
    gtk_widget_set_visible(toast, TRUE);

    guint timeout_id = g_timeout_add(2000, (GSourceFunc)destroy_toast, toast);
    g_object_set_data(G_OBJECT(toast), "timeout-id", GUINT_TO_POINTER(timeout_id));
}
static void on_image_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    ImageClickData *data = user_data;
    GtkPicture *pic = GTK_PICTURE(data->pic);
    GdkPaintable *paintable = gtk_picture_get_paintable(pic);

    int img_w = gdk_paintable_get_intrinsic_width(paintable);
    int img_h = gdk_paintable_get_intrinsic_height(paintable);

    // GtkAllocation alloc;
    // gtk_widget_get_alocation(GTK_WIDGET(pic), &alloc);
    graphene_rect_t bounds;
    if (!gtk_widget_compute_bounds(GTK_WIDGET(pic), GTK_WIDGET(pic), &bounds)) {
        // Fallback if it somehow fails
        return;
    }
    double scale_x = (double)img_w / bounds.size.width;
    double scale_y = (double)img_h / bounds.size.height;

    int px = (int)(x * scale_x);
    int py = (int)(y * scale_y);

    GdkTexture *texture = g_object_get_data(G_OBJECT(pic), "texture");
    if (!texture) return;
    int channels = 4; // gdk_texture_download always uses RGBA
    int stride = img_w * channels;
    guchar *pixels = g_malloc(img_h * stride);
    gdk_texture_download(texture, pixels, stride);

    if (px >= 0 && px < img_w && py >= 0 && py < img_h) {
        guchar *p = pixels + py * stride + px * channels;
        char hex_color[10];
        snprintf(hex_color, sizeof(hex_color), "#%02x%02x%02x", p[0], p[1], p[2]);
        GdkDisplay *display = gdk_display_get_default();
        GdkClipboard *clipboard = gdk_display_get_clipboard(display);
        gdk_clipboard_set_text(clipboard, hex_color);
        gchar *combined = g_strconcat("Copied: ", hex_color, NULL);
        show_toast(pic, combined);
        g_free(combined);
        save_color_to_history(hex_color);
    }
    AppState *state = data->state;
    g_free(pixels);
}
void clip_board_texture_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    GdkTexture *texture =
        gdk_clipboard_read_texture_finish(GDK_CLIPBOARD(source), res, &error);

    if (!texture) {
        g_printerr("Failed to read clipboard texture: %s\n", error->message);
        g_clear_error(&error);
        return;
    }

    GoBackData *appData = user_data;
    AppState *state = appData->state;
    gtk_window_set_default_size(GTK_WINDOW(state->window), 900, 700);

    int w = gdk_texture_get_width(texture);
    int h = gdk_texture_get_height(texture);

    // Picture
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    gtk_picture_set_can_shrink(GTK_PICTURE(pic), FALSE);

    g_object_set_data(G_OBJECT(pic), "texture", texture);
    g_object_set_data(G_OBJECT(pic), "orig-width", GINT_TO_POINTER(w));
    g_object_set_data(G_OBJECT(pic), "orig-height", GINT_TO_POINTER(h));

    // Scroll controller (zoom)
    ZoomState *zoom = g_new0(ZoomState, 1);
    zoom->scale = 1.0;
    g_object_set_data_full(G_OBJECT(pic), "zoom", zoom, g_free);

    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), zoom);
    gtk_widget_add_controller(pic, scroll);

    ImageClickData *imageData = g_malloc(sizeof(ImageClickData));
    imageData->pic = pic;
    imageData->state = state;
    // Click controller
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_image_click), imageData);
    gtk_widget_add_controller(pic, GTK_EVENT_CONTROLLER(click));

    // Scrolled window
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_ALWAYS,
        GTK_POLICY_ALWAYS);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), pic);

    // Overlay
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scrolled);

    // Back button
    GtkWidget *backButton = gtk_button_new_with_label("Back");
    gtk_widget_set_halign(backButton, GTK_ALIGN_START);
    gtk_widget_set_valign(backButton, GTK_ALIGN_START);
    gtk_widget_set_margin_top(backButton, 10);
    gtk_widget_set_margin_start(backButton, 10);
    g_signal_connect(backButton, "clicked", G_CALLBACK(go_back), appData);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), backButton);

    gtk_stack_add_named(GTK_STACK(state->stack), overlay, "image");
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "image");
}
gboolean read_clipboard(gpointer goBackData){
    GdkDisplay *display = gdk_display_get_default();
    GdkClipboard *clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_read_texture_async(clipboard, NULL, clip_board_texture_done, goBackData);
    return G_SOURCE_REMOVE;
}
void load_image_from_uri(gchar *uri, AppState *state, GoBackData *goBackData){
    gchar *local_path = g_filename_from_uri(uri, NULL, NULL);
    GdkTexture *texture = gdk_texture_new_from_filename(local_path, NULL);
    if (!texture) return;
    int w = gdk_texture_get_width(texture);
    int h = gdk_texture_get_height(texture);
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    g_object_set_data(G_OBJECT(pic), "texture", texture);
    g_object_set_data(G_OBJECT(pic), "orig-width", GINT_TO_POINTER(w));
    g_object_set_data(G_OBJECT(pic), "orig-height", GINT_TO_POINTER(h));
    // Scroll controller (zoom)
    ZoomState *zoom = g_new0(ZoomState, 1);
    zoom->scale = 1.0;
    g_object_set_data_full(G_OBJECT(pic), "zoom", zoom, g_free);

    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), zoom);
    gtk_widget_add_controller(pic, scroll);

    ImageClickData *imageData = g_malloc(sizeof(ImageClickData));
    imageData->pic = pic;
    imageData->state = state;
    // Click controller
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_image_click), imageData);
    gtk_widget_add_controller(pic, GTK_EVENT_CONTROLLER(click));

    // Scrolled window
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_ALWAYS,
        GTK_POLICY_ALWAYS);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), pic);

    // Overlay
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scrolled);

    // Back button
    GtkWidget *backButton = gtk_button_new_with_label("Back");
    gtk_widget_set_halign(backButton, GTK_ALIGN_START);
    gtk_widget_set_valign(backButton, GTK_ALIGN_START);
    gtk_widget_set_margin_top(backButton, 10);
    gtk_widget_set_margin_start(backButton, 10);
    g_signal_connect(backButton, "clicked", G_CALLBACK(go_back), goBackData);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), backButton);

    gtk_stack_add_named(GTK_STACK(state->stack), overlay, "image");
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "image");
}
void screenshot_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;

    // xdp_portal_take_screenshot_finish returns the uri
    gchar *uri = xdp_portal_take_screenshot_finish(XDP_PORTAL(source), res, &error);
    if (!uri) {
        if (error) {
            g_printerr("Error during XDP_PORTAL screenshot: %s\n", error->message);
            if (error && g_strcmp0(error->message, "Error during XDP_PORTAL screenshot: Screenshot canceled")) {
                g_print("Screenshot canceled\n");
            }
            g_clear_error(&error);
        } else {
            g_printerr("Screenshot failed for unknown reason\n");
        }
        return;
    }

    AppState *state = user_data;
    GoBackData *goBackData = g_malloc(sizeof(GoBackData));
    goBackData ->state = state;
    goBackData ->prevRoute = IMAGE;
    if (g_str_has_prefix(uri, "clipboard://")) {
        g_timeout_add(200, (GSourceFunc)read_clipboard, goBackData);
    }else {
        load_image_from_uri(uri, state, goBackData);
    }

    // Free filename after using it
    g_free(uri);
}
static void open_screenshot(GtkWidget *widget, gpointer data) {
    XdpPortal *portal = xdp_portal_new();
    xdp_portal_take_screenshot(portal, NULL, XDP_SCREENSHOT_FLAG_INTERACTIVE, NULL, screenshot_done, data);
}
static void show_help(GtkButton *button, gpointer data) {
    AppState *state = data;
    GtkBuilder *builder = gtk_builder_new_from_resource("/com/waypicker/ui/help.ui");
    GtkButton *backButton = GTK_BUTTON(gtk_builder_get_object(builder, "backButton"));
    GoBackData *appData = g_malloc(sizeof(GoBackData));
    appData->state = state;
    appData->prevRoute = HELP;
    g_signal_connect(backButton, "clicked", G_CALLBACK(go_back), appData);

    GtkOverlay *overlay = GTK_OVERLAY(gtk_builder_get_object(builder, "help-overlay"));
    gtk_stack_add_named(GTK_STACK(state->stack), GTK_WIDGET(overlay), "help");
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "help");

}
static void go_back(GtkButton *button, gpointer data) {
    GoBackData *appData = data;
    AppState *state = appData->state;
    enum ROUTES prevRoute = appData->prevRoute;
    if (prevRoute == IMAGE) {
        GtkWidget *imageStack = gtk_stack_get_child_by_name(GTK_STACK(state->stack), "image");
        gtk_stack_remove(GTK_STACK(state->stack), imageStack);
    }else if (prevRoute == HELP) {
        GtkWidget *helpStack = gtk_stack_get_child_by_name(GTK_STACK(state->stack), "help");
        gtk_stack_remove(GTK_STACK(state->stack), helpStack);
    }
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "home");
    if(state->history_container){
        populate_color_history(GTK_BOX(state->history_container));
    }
}
void on_image_selected(GObject *source, GAsyncResult *result, gpointer user_data){
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;

    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    if(!file){
        if(error && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)){
            g_printerr("Error opening file: %s\n", error->message);
        }
        g_clear_error(&error);
        return;
    }
    gchar *uri = g_file_get_uri(file);
    LoadImageData *loadData = user_data;
    load_image_from_uri(uri, loadData->state, loadData->goBackData);
    g_free(uri);
    g_object_unref(file);
}
void open_file_explorer(GtkWidget *button, gpointer data){
    AppState *state = data;
    GoBackData *goBackData = g_malloc(sizeof(GoBackData));
    goBackData ->state = state;
    goBackData ->prevRoute = IMAGE;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select an Image");

    GtkFileFilter *fileFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(fileFilter, "Image Files");
    gtk_file_filter_add_mime_type(fileFilter, "image/png");
    gtk_file_filter_add_mime_type(fileFilter, "image/jpeg");
    gtk_file_filter_add_mime_type(fileFilter, "image/jpg");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, fileFilter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));

    LoadImageData *loadData = g_malloc(sizeof(LoadImageData));
    loadData->state = state;
    loadData->goBackData = goBackData;

    gtk_file_dialog_open(dialog, GTK_WINDOW(state->window), NULL, on_image_selected, loadData);
    g_object_unref(fileFilter);
    g_object_unref(filters);
}
static void on_history_color_click(GtkButton *button, gpointer user_data){
    gchar *color_hex = user_data;

    GdkDisplay *display = gdk_display_get_default();
    GdkClipboard *clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_set_text(clipboard, color_hex);

}
static void draw_color_box(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data){
    GdkRGBA *color = user_data;
    gdk_cairo_set_source_rgba(cr, color);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
}
static void populate_color_history(GtkBox *container){
    GtkWidget *child;
    // If there are any children inside the container remove them
    while((child = gtk_widget_get_first_child(GTK_WIDGET(container))) != NULL){
        gtk_box_remove(container, child);
    }

    GList *history = load_color_history();
    if(history == NULL){
        GtkWidget *label = gtk_label_new("No color history yet.");
        gtk_widget_set_opacity(label, 0.5);
        gtk_box_append(container, label);
        return;
    }

    for(GList *l = history; l != NULL; l = l->next){
        ColorEntry *entry = l -> data;

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_start(row, 5);
        gtk_widget_set_margin_end(row, 5);

        // Color preview box
        GtkWidget *color_box = gtk_drawing_area_new();
        gtk_widget_set_size_request(color_box, 40, 40);

        GdkRGBA rgba;
        gdk_rgba_parse(&rgba, entry->hex_color);

        gtk_drawing_area_set_draw_func(
            GTK_DRAWING_AREA(color_box),
            (GtkDrawingAreaDrawFunc)draw_color_box,
            gdk_rgba_copy(&rgba),
            (GDestroyNotify)gdk_rgba_free
        );
        // Hex Label
        GtkWidget *label = gtk_label_new(entry->hex_color);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_halign(label, GTK_ALIGN_START);

        GtkWidget *copy_btn = gtk_button_new_with_label("Copy");
        gchar *color_copy = g_strdup(entry->hex_color);

        g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_history_color_click), color_copy);
        g_object_weak_ref(G_OBJECT(copy_btn), (GWeakNotify)g_free, color_copy);

        gtk_box_append(GTK_BOX(row), color_box);
        gtk_box_append(GTK_BOX(row), label);
        gtk_box_append(GTK_BOX(row), copy_btn);

        gtk_box_append(container, row);
    }
    free_color_history(history);
}
static void activate(GtkApplication *app, gpointer user_data) {
    GResource *resource = resources_get_resource();
    g_resources_register(resource);
    GtkCssProvider *cssprovider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(cssprovider, "/com/waypicker/styles.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(cssprovider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    g_object_unref(cssprovider);

    AppState *state = g_new0(AppState, 1);
    GError *error = NULL;
    GtkBuilder *builder = gtk_builder_new_from_resource("/com/waypicker/ui/init.ui");
    if (!builder) {
        g_printerr("Failed to load UI: %s\n", error->message);
        g_clear_error(&error);
        return;
    }
    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    state->window = window;
    GtkWidget *home = GTK_WIDGET(gtk_builder_get_object(builder, "home"));
    GtkWidget *stack = GTK_WIDGET(gtk_builder_get_object(builder, "stack"));
    state->stack = stack;

    GtkBox *history_container = GTK_BOX(gtk_builder_get_object(builder, "history_container"));
    state->history_container = GTK_WIDGET(history_container);
    populate_color_history(history_container);

    g_signal_connect(gtk_builder_get_object(builder, "pick_color_button"), "clicked", G_CALLBACK(open_screenshot), state);
    g_signal_connect(gtk_builder_get_object(builder, "load_image_button"), "clicked", G_CALLBACK(open_file_explorer), state);
    g_signal_connect(gtk_builder_get_object(builder, "help_button"), "clicked", G_CALLBACK(show_help), state);

    gtk_window_set_application(GTK_WINDOW(state->window), app);
    gtk_window_present(GTK_WINDOW(state->window));
}
int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    app = gtk_application_new("com.WayPicker", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
