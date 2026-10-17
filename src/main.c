/* MIRC-Client – Einstiegspunkt. */
#include "mirc.h"

App app;

static void image_unref(gpointer p) { if (p) g_object_unref(p); }

static void me_loaded(guint status, JsonNode *root, const char *error, gpointer data) {
    (void)data;
    /* Hold the application here, otherwise it would quit without an open
     * window. Release it after the window opens. */
    if (status == 200 && root && JSON_NODE_HOLDS_OBJECT(root)) {
        app.me = profile_from_json(json_node_get_object(root));
        app.me_json = json_to_string(root, FALSE);
        app_start_session();
        g_application_release(G_APPLICATION(app.gapp));
        return;
    }
    if (status == 401) {
        g_clear_pointer(&app.token, g_free);
        config_set_session(NULL, NULL, NULL);
    }
    dialog_login();
    g_application_release(G_APPLICATION(app.gapp));
    if (error && status != 401) g_message("Sessionserver: %s", error);
}

/* After successful sign-in: load servers, connect, and show the window. */
void app_start_session(void) {
    ui_apply_theme();
    if (!app.servers->len) {
        GPtrArray *cfgs = config_load_servers();
        for (guint i = 0; i < cfgs->len; i++) {
            ServerCfg *c = g_ptr_array_index(cfgs, i);
            g_ptr_array_add(app.servers, server_new(c));
            g_free(c->id); g_free(c->name); g_free(c->host); g_free(c);
        }
        g_ptr_array_free(cfgs, TRUE);
    }
    ui_show_main();
    char *last_id = config_get_string("ui", "last_server");
    char *last_conv = config_get_string("ui", "last_conv");
    Server *sel = NULL;
    for (guint i = 0; i < app.servers->len; i++) {
        Server *s = g_ptr_array_index(app.servers, i);
        server_connect(s);
        if (last_id && !g_strcmp0(s->cfg.id, last_id)) sel = s;
    }
    if (!sel && app.servers->len) sel = g_ptr_array_index(app.servers, 0);
    if (sel) {
        app.active = sel;
        g_free(app.active_key);
        app.active_key = g_strdup("*");
        /* Der zuletzt offene Channel existiert erst nach dem "welcome" des Servers. */
        if (last_conv) ui_switch_when_joined(sel, last_conv);
    }
    g_free(last_id); g_free(last_conv);
    ui_changed(NULL, NULL, TRUE);
    session_start_profile_watch();
    if (!app.servers->len) dialog_add_server(NULL);
}

void app_logout(void) {
    if (app.token) http_json("POST", "/api/logout", "{}", TRUE, NULL, NULL);
    for (guint i = 0; i < app.servers->len; i++) server_disconnect(g_ptr_array_index(app.servers, i), "Signed out");
    g_clear_pointer(&app.token, g_free);
    config_set_session(NULL, NULL, NULL);
    GtkWindow *w = ui_window();
    if (w) gtk_widget_hide(GTK_WIDGET(w));
    dialog_login();
}

static void activate(GtkApplication *gapp, gpointer data) {
    (void)data;
    GList *wins = gtk_application_get_windows(gapp);
    if (wins) { gtk_window_present(wins->data); return; }
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
    ui_apply_theme();
    if (app.token) { g_application_hold(G_APPLICATION(gapp)); session_fetch_me(me_loaded, NULL); }
    else dialog_login();
}

int main(int argc, char **argv) {
    app.servers = g_ptr_array_new();
    app.images = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, image_unref);
    app.active_key = g_strdup("*");
    config_load();
    app.http = soup_session_new_with_options("timeout", 15, "user-agent", "MIRC-Client/" MIRC_VERSION, NULL);

    g_set_application_name("MIRC");
    app.gapp = gtk_application_new("chat.mirc.Client", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app.gapp, "activate", G_CALLBACK(activate), NULL);
    int r = g_application_run(G_APPLICATION(app.gapp), argc, argv);
    g_object_unref(app.gapp);
    return r;
}
