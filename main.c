#include <stdio.h>
#include <gtk/gtk.h>
#include <libportal/portal.h>

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

    g_print("PX: %d \n PY %d\n", px, py);

    GdkTexture *texture = g_object_get_data(G_OBJECT(pic), "texture");
    if (!texture) return;
    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_texture(texture);
    if (!pixbuf) return;
    int channels = gdk_pixbuf_get_n_channels(pixbuf);
    int stride = gdk_pixbuf_get_rowstride(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);

    if (px >= 0 && px < img_w && py >= 0 && py < img_h) {
        guchar *p = pixels + py * stride + px * channels;
        g_print("COLOR: R=%d G=%d B=%d A=%d", p[0], p[1], p[2], channels == 4 ? p[3]: 255);
    }
    g_object_unref(pixbuf);
}
void clip_board_texture_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    GdkTexture *texture = gdk_clipboard_read_texture_finish(GDK_CLIPBOARD(source), res, &error);
    if (!texture) {
        g_printerr("Failed to read clipboard texture: %s\n", error->message);
        g_clear_error(&error);
        return;
    }
    GtkWindow *window = GTK_WINDOW(user_data);

    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    g_object_set_data(G_OBJECT(pic), "texture", texture);
    gtk_window_set_child(window, pic);

    GtkGestureClick *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_image_click), pic);
    gtk_widget_add_controller(pic, GTK_EVENT_CONTROLLER(click));
}
void screenshot_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;  // initialize error to NULL

    // xdp_portal_take_screenshot_finish returns the filename
    gchar *filename = xdp_portal_take_screenshot_finish(XDP_PORTAL(source), res, &error);

    if (!filename) {
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

    g_print("Screenshot saved at: %s\n", filename);
    if (g_str_has_prefix(filename, "clipboard://")) {
        g_print("CLIPBOARD\n");
        GtkWindow *window = GTK_WINDOW(user_data);
        GdkDisplay *display = gdk_display_get_default();
        GdkClipboard *clipboard = gdk_display_get_clipboard(display);
        gdk_clipboard_read_texture_async(clipboard, NULL, clip_board_texture_done, window);
    }

    // Free filename after using it
    g_free(filename);
}
static void open_screenshot(GtkWidget *widget, gpointer data) {
    XdpPortal *portal = xdp_portal_new();
    xdp_portal_take_screenshot(portal, NULL, XDP_SCREENSHOT_FLAG_INTERACTIVE, NULL, screenshot_done, data);
}
static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window;
    GtkWidget *button;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Color Picker");
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);

    button = gtk_button_new_with_label("Pick Color");
    gtk_widget_set_halign(GTK_WIDGET(button), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(button), GTK_ALIGN_CENTER);
    g_signal_connect(button, "clicked", G_CALLBACK(open_screenshot), window);
    gtk_window_set_child(GTK_WINDOW(window), button);

    gtk_window_present(GTK_WINDOW(window));
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