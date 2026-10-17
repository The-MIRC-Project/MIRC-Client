/* Settings in an INI file:
 *   Linux:   ~/.config/mirc/client.ini
 *   Windows: %LOCALAPPDATA%\mirc\client.ini
 * The file contains the login token and is therefore created readable only
 * by the current user. */
#include "mirc.h"
#include <glib/gstdio.h>

void config_load(void) {
    char *dir = g_build_filename(g_get_user_config_dir(), "mirc", NULL);
    g_mkdir_with_parents(dir, 0700);
    app.config_path = g_build_filename(dir, "client.ini", NULL);
    g_free(dir);
    app.config = g_key_file_new();
    g_key_file_load_from_file(app.config, app.config_path, G_KEY_FILE_KEEP_COMMENTS, NULL);

    char *url = g_key_file_get_string(app.config, "session", "url", NULL);
    app.session_url = url && *url ? url : g_strdup(MIRC_DEFAULT_SESSION);
    if (url && !*url) g_free(url);
    char *tok = g_key_file_get_string(app.config, "session", "token", NULL);
    app.token = tok && *tok ? tok : NULL;
    if (tok && !*tok) g_free(tok);
}

void config_save(void) {
    GError *err = NULL;
    if (!g_key_file_save_to_file(app.config, app.config_path, &err)) {
        g_warning("Settings could not be saved: %s", err->message);
        g_error_free(err);
        return;
    }
    g_chmod(app.config_path, 0600);
}

char *config_get_string(const char *group, const char *key) {
    return g_key_file_get_string(app.config, group, key, NULL);
}

void config_set_string(const char *group, const char *key, const char *value) {
    if (value) g_key_file_set_string(app.config, group, key, value);
    else g_key_file_remove_key(app.config, group, key, NULL);
    config_save();
}

void config_set_session(const char *url, const char *username, const char *token) {
    if (url) g_key_file_set_string(app.config, "session", "url", url);
    if (username) g_key_file_set_string(app.config, "session", "username", username);
    if (token) g_key_file_set_string(app.config, "session", "token", token);
    else g_key_file_remove_key(app.config, "session", "token", NULL);
    config_save();
}

GPtrArray *config_load_servers(void) {
    GPtrArray *out = g_ptr_array_new();
    gsize n = 0;
    char **groups = g_key_file_get_groups(app.config, &n);
    for (gsize i = 0; i < n; i++) {
        if (!g_str_has_prefix(groups[i], "server ")) continue;
        ServerCfg *c = g_new0(ServerCfg, 1);
        c->id = g_strdup(groups[i] + 7);
        c->name = g_key_file_get_string(app.config, groups[i], "name", NULL);
        c->host = g_key_file_get_string(app.config, groups[i], "host", NULL);
        c->port = g_key_file_get_integer(app.config, groups[i], "port", NULL);
        c->tls = g_key_file_get_boolean(app.config, groups[i], "tls", NULL);
        if (!c->host || !*c->host || c->port <= 0) { g_free(c); continue; }
        g_ptr_array_add(out, c);
    }
    g_strfreev(groups);
    return out;
}

void config_save_servers(void) {
    gsize n = 0;
    char **groups = g_key_file_get_groups(app.config, &n);
    for (gsize i = 0; i < n; i++)
        if (g_str_has_prefix(groups[i], "server ")) g_key_file_remove_group(app.config, groups[i], NULL);
    g_strfreev(groups);
    for (guint i = 0; i < app.servers->len; i++) {
        Server *s = g_ptr_array_index(app.servers, i);
        char *g = g_strdup_printf("server %s", s->cfg.id);
        g_key_file_set_string(app.config, g, "name", s->cfg.name ? s->cfg.name : "");
        g_key_file_set_string(app.config, g, "host", s->cfg.host);
        g_key_file_set_integer(app.config, g, "port", s->cfg.port);
        g_key_file_set_boolean(app.config, g, "tls", s->cfg.tls);
        g_free(g);
    }
    config_save();
}

char *config_get_pin(const char *hostport) {
    return g_key_file_get_string(app.config, "pins", hostport, NULL);
}

void config_set_pin(const char *hostport, const char *fp) {
    g_key_file_set_string(app.config, "pins", hostport, fp);
    config_save();
}
