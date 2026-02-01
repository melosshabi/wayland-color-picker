#include <gtk/gtk.h>

typedef struct {
    double scale;
} ZoomState;

typedef struct {
    GtkWidget *window;
    GtkWidget *stack;
} AppState;

enum ROUTES {
    IMAGE,
    HELP
};

typedef struct {
    enum ROUTES prevRoute;
    AppState *state;
}GoBackData;

typedef struct {
    GtkWidget *pic;
    AppState *state;
}ImageClickData;
typedef struct{
    gchar uri;
    AppState *state;
    GoBackData *goBackData;
}LoadImageData;
