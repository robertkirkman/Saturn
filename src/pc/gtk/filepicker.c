#include <gtk/gtk.h>

#include "../platform.h"
#include "../cliopts.h"

static GtkApplication *app;

// callback which sets the ROM path to the chosen file's path
static void open_dialog_callback(GObject *source_object, GAsyncResult *res) {
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GFile *file;
    GtkAlertDialog *alert_dialog;
    GError *err = NULL;

    file = gtk_file_dialog_open_finish(dialog, res, &err);
    if (file != NULL) {
        // the user clicked the "Open" button, so set that path.
        g_autofree gchar *filePath = g_file_get_path(file);
        strncpy(gCLIOpts.RomPath, filePath, SYS_MAX_PATH);
    } else {
        // if the the user clicked the "Cancel" button do nothing, but if a
        // different error happened, display that.
        if (err->code != GTK_DIALOG_ERROR_DISMISSED) {
            alert_dialog = gtk_alert_dialog_new("%s", err->message);
            gtk_alert_dialog_show(alert_dialog, GTK_WINDOW(dialog));
            g_object_unref(alert_dialog);
        }
        g_clear_error(&err);
    }
    // terminate the GTK GUI so that the Saturn GUI can take over
    g_application_quit(G_APPLICATION(app));
}

// spawn an invisible toplevel GTK window which seems to be required for
// g_application_run() to successfully block execution, then spawn the GTK
// file picker prefab.
static void launch_gtk_gui(GtkApplication *app) {
    GtkWidget *window = gtk_application_window_new(app);
    GtkFileDialog *dialog = gtk_file_dialog_new();

    gtk_file_dialog_set_title(dialog, "Select ROM");
    gtk_file_dialog_open(dialog, GTK_WINDOW(window), NULL, open_dialog_callback, NULL);

    g_object_unref(dialog);
}

// entrypoint to spawn the file picker GUI and block execution until return
int open_file_picker(void) {
    int status;

    app = gtk_application_new("com.Llennpie.Saturn", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(launch_gtk_gui), NULL);
    status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);

    return status;
}