/* Communication with the session server (HTTP/JSON over libsoup) and
 * image cache for avatars and banners. */
#include "mirc.h"
#include <string.h>

typedef struct {
    SoupMessage *msg;
    HttpCallback cb;
    gpointer data;
} HttpReq;

static void http_done(GObject *src, GAsyncResult *res, gpointer user) {
    HttpReq *r = user;
    GError *err = NULL;
    GBytes *body = soup_session_send_and_read_finish(SOUP_SESSION(src), res, &err);
    guint status = soup_message_get_status(r->msg);
    JsonNode *root = NULL;
    const char *error = NULL;
    char *errbuf = NULL;
    JsonParser *parser = NULL;

    if (err) {
        errbuf = g_strdup_printf("Session server unreachable (%s)", err->message);
        error = errbuf;
        status = 0;
        g_error_free(err);
    } else {
        parser = json_parser_new();
        gsize len = 0;
        const char *data = g_bytes_get_data(body, &len);
        if (len && json_parser_load_from_data(parser, data, len, NULL)) root = json_parser_get_root(parser);
        if (status < 200 || status >= 300) {
            const char *e = root && JSON_NODE_HOLDS_OBJECT(root) ? jstr(json_node_get_object(root), "error") : NULL;
            errbuf = g_strdup(e ? e : soup_message_get_reason_phrase(r->msg));
            error = errbuf;
        }
    }
    if (r->cb) r->cb(status, root, error, r->data);
    if (parser) g_object_unref(parser);
    if (body) g_bytes_unref(body);
    g_free(errbuf);
    g_object_unref(r->msg);
    g_free(r);
}

static void http_request(const char *url, const char *method, const char *body_json, const char *token, HttpCallback cb, gpointer data) {
    SoupMessage *msg = soup_message_new(method, url);
    if (!msg) {
        if (cb) cb(0, NULL, "Invalid session server address", data);
        return;
    }
    SoupMessageHeaders *h = soup_message_get_request_headers(msg);
    soup_message_headers_append(h, "Accept", "application/json");
    if (token) {
        char *a = g_strdup_printf("Bearer %s", token);
        soup_message_headers_append(h, "Authorization", a);
        g_free(a);
    }
    if (body_json) {
        GBytes *b = g_bytes_new(body_json, strlen(body_json));
        soup_message_set_request_body_from_bytes(msg, "application/json", b);
        g_bytes_unref(b);
    }
    HttpReq *r = g_new0(HttpReq, 1);
    r->msg = msg; r->cb = cb; r->data = data;
    soup_session_send_and_read_async(app.http, msg, G_PRIORITY_DEFAULT, NULL, http_done, r);
}

void http_json(const char *method, const char *path, const char *body_json, gboolean auth, HttpCallback cb, gpointer data) {
    char *url = g_strconcat(app.session_url, path, NULL);
    http_request(url, method, body_json, auth ? app.token : NULL, cb, data);
    g_free(url);
}

static char *obj_json(const char *k1, const char *v1, const char *k2, const char *v2, const char *k3, const char *v3) {
    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    const char *k[] = { k1, k2, k3 }, *v[] = { v1, v2, v3 };
    for (int i = 0; i < 3; i++)
        if (k[i]) { json_builder_set_member_name(b, k[i]); json_builder_add_string_value(b, v[i] ? v[i] : ""); }
    json_builder_end_object(b);
    JsonNode *n = json_builder_get_root(b);
    char *s = json_to_string(n, FALSE);
    json_node_free(n);
    g_object_unref(b);
    return s;
}

void session_login(const char *url, const char *user, const char *password, HttpCallback cb, gpointer data) {
    char *device = g_strdup_printf("MIRC-Client %s (%s)", MIRC_VERSION, g_get_host_name());
    char *body = obj_json("username", user, "password", password, "device", device);
    char *full = g_strconcat(url, "/api/login", NULL);
    http_request(full, "POST", body, NULL, cb, data);
    g_free(full); g_free(body); g_free(device);
}

void session_fetch_me(HttpCallback cb, gpointer data) { http_json("GET", "/api/me", NULL, TRUE, cb, data); }

void session_server_token(const char *hostport, HttpCallback cb, gpointer data) {
    char *body = obj_json("server", hostport, NULL, NULL, NULL, NULL);
    http_json("POST", "/api/session/server-token", body, TRUE, cb, data);
    g_free(body);
}

/* ---------- Bilder ---------- */

typedef struct { SoupMessage *msg; char *url; } ImgReq;

static void image_done(GObject *src, GAsyncResult *res, gpointer user) {
    ImgReq *r = user;
    GError *err = NULL;
    GBytes *body = soup_session_send_and_read_finish(SOUP_SESSION(src), res, &err);
    GdkPixbuf *pb = NULL;
    if (body && soup_message_get_status(r->msg) == 200) {
        GdkPixbufLoader *l = gdk_pixbuf_loader_new();
        gsize len;
        const guchar *d = g_bytes_get_data(body, &len);
        if (gdk_pixbuf_loader_write(l, d, len, NULL) && gdk_pixbuf_loader_close(l, NULL)) {
            GdkPixbuf *orig = gdk_pixbuf_loader_get_pixbuf(l);
            if (orig) {
                int w = gdk_pixbuf_get_width(orig), h = gdk_pixbuf_get_height(orig);
                double scale = MIN(1.0, 480.0 / MAX(w, h));
                pb = gdk_pixbuf_scale_simple(orig, MAX(1, (int)(w * scale)), MAX(1, (int)(h * scale)), GDK_INTERP_BILINEAR);
            }
        } else gdk_pixbuf_loader_close(l, NULL);
        g_object_unref(l);
    }
    if (err) g_error_free(err);
    if (body) g_bytes_unref(body);
    if (pb) {
        g_hash_table_replace(app.images, g_strdup(r->url), pb);
        ui_changed(NULL, NULL, TRUE);
    }
    /* On failure, keep the NULL entry: do not retry until restart. */
    g_object_unref(r->msg);
    g_free(r->url);
    g_free(r);
}

GdkPixbuf *image_get(const char *url) {
    if (!url || !*url) return NULL;
    gpointer val;
    if (g_hash_table_lookup_extended(app.images, url, NULL, &val)) return val;
    if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://")) return NULL;
    g_hash_table_insert(app.images, g_strdup(url), NULL);
    SoupMessage *msg = soup_message_new("GET", url);
    if (!msg) return NULL;
    ImgReq *r = g_new0(ImgReq, 1);
    r->msg = msg;
    r->url = g_strdup(url);
    soup_session_send_and_read_async(app.http, msg, G_PRIORITY_LOW, NULL, image_done, r);
    return NULL;
}

/* ---------- Apply profile changes from the website ---------- */

static void me_done(guint status, JsonNode *root, const char *error, gpointer data) {
    (void)data;
    if (status == 401) { app_logout(); return; }
    if (error || !root || !JSON_NODE_HOLDS_OBJECT(root)) return;
    char *js = json_to_string(root, FALSE);
    if (g_strcmp0(js, app.me_json) != 0) {
        g_free(app.me_json);
        app.me_json = js;
        profile_free(app.me);
        app.me = profile_from_json(json_node_get_object(root));
        ui_apply_theme();
        servers_refresh_profile();
        ui_changed(NULL, NULL, TRUE);
    } else g_free(js);
}

static gboolean profile_tick(gpointer data) {
    (void)data;
    if (app.token) session_fetch_me(me_done, NULL);
    return G_SOURCE_CONTINUE;
}

void session_start_profile_watch(void) {
    static guint id = 0;
    if (!id) id = g_timeout_add_seconds(45, profile_tick, NULL);
}

void session_check_profile(void) { profile_tick(NULL); }
