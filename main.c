#include <stdio.h>
#include <gtk/gtk.h>
#include <libportal/portal.h>

typedef struct {
    double scale;
} ZoomState;

typedef struct {
    GtkWidget *window;
    GtkWidget *stack;
} AppState;
static void go_back(GtkButton *button, gpointer user_data);
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
static void on_image_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    GtkPicture *pic = GTK_PICTURE(user_data);
    GdkPaintable *paintable = gtk_picture_get_paintable(pic);

    int img_w = gdk_paintable_get_intrinsic_width(paintable);
    int img_h = gdk_paintable_get_intrinsic_height(paintable);

    GtkAllocation alloc;
    gtk_widget_get_allocation(GTK_WIDGET(pic), &alloc);
    double scale_x = (double)img_w / alloc.width;
    double scale_y = (double)img_h / alloc.height;

    int px = (int)(x * scale_x);
    int py = (int)(y * scale_y);

    GdkTexture *texture = g_object_get_data(G_OBJECT(pic), "texture");
    if (!texture) return;
    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_texture(texture);
    if (!pixbuf) return;
    int channels = gdk_pixbuf_get_n_channels(pixbuf);
    int stride = gdk_pixbuf_get_rowstride(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);

    if (px >= 0 && px < img_w && py >= 0 && py < img_h) {
        guchar *p = pixels + py * stride + px * channels;
        char hex_color[10];
        if (channels == 4) {
            snprintf(hex_color, sizeof(hex_color), "#%02x%02x%02x%02x", p[0], p[1], p[2], p[3]);
        }else {
            snprintf(hex_color, sizeof(hex_color), "#%02x%02x%02x", p[0], p[1], p[2]);
        }
        GdkDisplay *display = gdk_display_get_default();
        GdkClipboard *clipboard = gdk_display_get_clipboard(display);
        gdk_clipboard_set_text(clipboard, hex_color);

        GNotification *notification = g_notification_new("Mela Color Picker");
        g_notification_set_body(notification, hex_color);
        GtkWindow *window = gtk_widget_get_root(GTK_WIDGET(pic));
        g_application_send_notification(G_APPLICATION(gtk_window_get_application(window)), "color-picked", notification);
        g_object_unref(notification);
    }
    g_object_unref(pixbuf);
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

    AppState *state = user_data;
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

    // Click controller
    GtkGestureClick *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_image_click), pic);
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
    g_signal_connect(backButton, "clicked", G_CALLBACK(go_back), state);
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
    if (g_str_has_prefix(uri, "clipboard://")) {
        GdkDisplay *display = gdk_display_get_default();
        GdkClipboard *clipboard = gdk_display_get_clipboard(display);
        gdk_clipboard_read_texture_async(clipboard, NULL, clip_board_texture_done, state);
    }else {
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

        // Click controller
        GtkGestureClick *click = gtk_gesture_click_new();
        g_signal_connect(click, "pressed", G_CALLBACK(on_image_click), pic);
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
        g_signal_connect(backButton, "clicked", G_CALLBACK(go_back), state);

        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), backButton);

        gtk_stack_add_named(GTK_STACK(state->stack), overlay, "image");
        gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "image");
    }

    // Free filename after using it
    g_free(uri);
}
static void open_screenshot(GtkWidget *widget, gpointer data) {
    XdpPortal *portal = xdp_portal_new();
    xdp_portal_take_screenshot(portal, NULL, XDP_SCREENSHOT_FLAG_INTERACTIVE, NULL, screenshot_done, data);
}
static void go_back(GtkButton *button, gpointer user_data) {
    AppState *state = user_data;
    gtk_stack_set_visible_child_name(GTK_STACK(state->stack), "home");
}

static void activate(GtkApplication *app, gpointer user_data) {
    AppState *state = g_new0(AppState, 1);
    state->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->window), "Color Picker");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 900, 700);

    state->stack = gtk_stack_new();
    gtk_window_set_child(GTK_WINDOW(state->window), state->stack);

    GtkWidget *home = gtk_center_box_new();
    GtkWidget *button = gtk_button_new_with_label("Pick Color");
    gtk_widget_set_halign(GTK_WIDGET(button), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(button), GTK_ALIGN_CENTER);
    g_signal_connect(button, "clicked", G_CALLBACK(open_screenshot), state);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(home), button);
    gtk_stack_add_named(GTK_STACK(state->stack), home, "home");
    gtk_window_present(GTK_WINDOW(state->window));
}
int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.Mela", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}