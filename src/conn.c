/* Connection to a MIRC chat server: TCP/TLS over GIO, one JSON line per
 * message, authenticated with a one-time token from the session server. */
#include "mirc.h"
#include <string.h>

void ui_conv_ready(Server *s, const char *key);

/* ---------- Unterhaltungen ---------- */

static void member_free(gpointer p) {
    Member *m = p;
    g_free(m->username); g_free(m->role); g_free(m);
}

static Conv *conv_new(ConvType type, const char *key, const char *name, const char *target) {
    Conv *c = g_new0(Conv, 1);
    c->type = type;
    c->key = g_strdup(key);
    c->name = g_strdup(name);
    c->target = g_strdup(target);
    c->members = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, member_free);
    c->messages = g_ptr_array_new_with_free_func((GDestroyNotify)message_free);
    c->typing = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    return c;
}

static void conv_free(gpointer p) {
    Conv *c = p;
    g_free(c->key); g_free(c->name); g_free(c->target); g_free(c->topic); g_free(c->owner);
    g_hash_table_destroy(c->members);
    g_ptr_array_free(c->messages, TRUE);
    g_hash_table_destroy(c->typing);
    g_free(c);
}

Conv *server_conv(Server *s, const char *key) { return key ? g_hash_table_lookup(s->convs, key) : NULL; }

Conv *server_status_conv(Server *s) {
    Conv *c = server_conv(s, "*");
    if (!c) {
        c = conv_new(CONV_STATUS, "*", "Server", NULL);
        c->history_loaded = TRUE;
        g_hash_table_insert(s->convs, g_strdup("*"), c);
    }
    return c;
}

Conv *server_ensure_dm(Server *s, const char *username) {
    char *low = str_lower(username);
    char *key = g_strconcat("@", low, NULL);
    Conv *c = server_conv(s, key);
    if (!c) {
        const Profile *p = server_profile(s, username);
        c = conv_new(CONV_DM, key, p->display_name, username);
        g_hash_table_insert(s->convs, g_strdup(key), c);
    }
    g_free(low); g_free(key);
    return c;
}

static void conv_push(Conv *c, Message *m) {
    g_ptr_array_add(c->messages, m);
    if (c->messages->len > 2000) g_ptr_array_remove_range(c->messages, 0, c->messages->len - 2000);
    if (m->ts > c->last_ts) c->last_ts = m->ts;
}

void server_system(Server *s, const char *key, const char *text, const char *kind) {
    Conv *c = server_conv(s, key);
    if (!c) c = server_status_conv(s);
    Message *m = message_new_system(text, kind);
    conv_push(c, m);
    ui_new_message(s, c, m, FALSE);
}

/* ---------- Nutzerprofile ---------- */

static void remember_user(Server *s, JsonObject *o) {
    Profile *p = profile_from_json(o);
    if (!p) return;
    g_hash_table_replace(s->users, str_lower(p->username), p);
}

const Profile *server_profile(Server *s, const char *username) {
    char *low = str_lower(username);
    Profile *p = g_hash_table_lookup(s->users, low);
    if (!p) {
        p = profile_fallback(username);
        p->accent = g_strdup("#7c6cf2"); /* MIRC-Nutzer: neutrale Textfarbe statt Zufallsfarbe */
        g_hash_table_insert(s->users, low, p);
    } else g_free(low);
    return p;
}

/* ---------- Server-Objekt ---------- */

Server *server_new(const ServerCfg *cfg) {
    Server *s = g_new0(Server, 1);
    s->refs = 1;
    s->cfg.id = g_strdup(cfg->id);
    s->cfg.name = g_strdup(cfg->name);
    s->cfg.host = g_strdup(cfg->host);
    s->cfg.port = cfg->port;
    s->cfg.tls = cfg->tls;
    s->convs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, conv_free);
    s->users = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)profile_free);
    s->outq = g_queue_new();
    s->status_text = g_strdup("Offline");
    server_status_conv(s);
    return s;
}

Server *server_ref(Server *s) { s->refs++; return s; }

void server_unref(Server *s) {
    if (--s->refs > 0) return;
    g_hash_table_destroy(s->convs);
    g_hash_table_destroy(s->users);
    g_queue_free_full(s->outq, (GDestroyNotify)g_bytes_unref);
    g_free(s->cfg.id); g_free(s->cfg.name); g_free(s->cfg.host);
    g_free(s->status_text); g_free(s->server_name); g_free(s->motd); g_free(s->me); g_free(s->pending_fp);
    g_free(s);
}

const char *server_title(Server *s) {
    if (s->cfg.name && *s->cfg.name) return s->cfg.name;
    if (s->server_name) return s->server_name;
    return s->cfg.host;
}

static char *hostport(Server *s) {
    char *h = str_lower(s->cfg.host);
    char *r = strchr(h, ':') ? g_strdup_printf("[%s]:%d", h, s->cfg.port) : g_strdup_printf("%s:%d", h, s->cfg.port);
    g_free(h);
    return r;
}

static void set_state(Server *s, ServerState st, const char *text) {
    s->state = st;
    g_free(s->status_text);
    s->status_text = g_strdup(text ? text : "");
    ui_changed(s, NULL, FALSE);
}

static void close_streams(Server *s) {
    if (s->cancel) { g_cancellable_cancel(s->cancel); g_clear_object(&s->cancel); }
    if (s->conn) { g_io_stream_close(G_IO_STREAM(s->conn), NULL, NULL); g_clear_object(&s->conn); }
    g_clear_object(&s->in);
    s->out = NULL; /* owned by the connection */
    g_clear_object(&s->client);
    g_queue_clear_full(s->outq, (GDestroyNotify)g_bytes_unref);
    if (s->writing) { g_bytes_unref(s->writing); s->writing = NULL; }
    GHashTableIter it;
    gpointer v;
    g_hash_table_iter_init(&it, s->convs);
    while (g_hash_table_iter_next(&it, NULL, &v)) {
        Conv *c = v;
        GHashTableIter mi;
        gpointer mv;
        g_hash_table_iter_init(&mi, c->members);
        while (g_hash_table_iter_next(&mi, NULL, &mv)) ((Member *)mv)->online = FALSE;
        g_hash_table_remove_all(c->typing);
    }
}

static gboolean reconnect_cb(gpointer data) {
    Server *s = data;
    s->reconnect_id = 0;
    server_connect(s);
    return G_SOURCE_REMOVE;
}

static void connection_lost(Server *s, const char *why) {
    close_streams(s);
    if (s->manual_close || s->removed) return;
    int delay = MIN(30, 1 << MIN(s->retry, 5));
    s->retry++;
    char *t = g_strdup_printf("%s – neuer Versuch in %d s", why, delay);
    set_state(s, ST_OFFLINE, t);
    g_free(t);
    if (s->reconnect_id) g_source_remove(s->reconnect_id);
    s->reconnect_id = g_timeout_add_seconds(delay, reconnect_cb, s);
}

void server_disconnect(Server *s, const char *why) {
    s->manual_close = TRUE;
    if (s->reconnect_id) { g_source_remove(s->reconnect_id); s->reconnect_id = 0; }
    close_streams(s);
    set_state(s, ST_OFFLINE, why ? why : "Disconnected");
}

/* ---------- Senden ---------- */

JsonBuilder *proto_begin(const char *op) {
    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "op");
    json_builder_add_string_value(b, op);
    return b;
}

void proto_str(JsonBuilder *b, const char *key, const char *val) {
    json_builder_set_member_name(b, key);
    json_builder_add_string_value(b, val ? val : "");
}

void proto_int(JsonBuilder *b, const char *key, gint64 val) {
    json_builder_set_member_name(b, key);
    json_builder_add_int_value(b, val);
}

static void write_next(Server *s);

static void write_done(GObject *src, GAsyncResult *res, gpointer data) {
    Server *s = data;
    GError *err = NULL;
    g_output_stream_write_all_finish(G_OUTPUT_STREAM(src), res, NULL, &err);
    if (err) {
        if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) connection_lost(s, "Connection interrupted");
        g_error_free(err);
    } else {
        if (s->writing) { g_bytes_unref(s->writing); s->writing = NULL; }
        write_next(s);
    }
    server_unref(s);
}

static void write_next(Server *s) {
    if (s->writing || !s->out || g_queue_is_empty(s->outq)) return;
    s->writing = g_queue_pop_head(s->outq);
    gsize len;
    const void *d = g_bytes_get_data(s->writing, &len);
    g_output_stream_write_all_async(s->out, d, len, G_PRIORITY_DEFAULT, s->cancel, write_done, server_ref(s));
}

void server_send(Server *s, JsonBuilder *b) {
    json_builder_end_object(b);
    JsonNode *n = json_builder_get_root(b);
    char *str = json_to_string(n, FALSE);
    json_node_free(n);
    g_object_unref(b);
    if (!s->out) { g_free(str); return; }
    gsize len = strlen(str);
    char *line = g_realloc(str, len + 2);
    line[len] = '\n';
    line[len + 1] = 0;
    g_queue_push_tail(s->outq, g_bytes_new_take(line, len + 1));
    write_next(s);
}

/* ---------- Protokoll ---------- */

static char *conv_key_for_message(Server *s, Message *m) {
    if (m->target && m->target[0] == '#') return str_lower(m->target);
    char *from = str_lower(m->from);
    const char *other = g_strcmp0(from, s->me) == 0 ? m->target : m->from;
    char *low = str_lower(other);
    char *key = g_strconcat("@", low, NULL);
    g_free(low); g_free(from);
    return key;
}

static char *key_for_target(const char *target) {
    if (target && target[0] == '#') return str_lower(target);
    char *low = str_lower(target);
    char *k = g_strconcat("@", low, NULL);
    g_free(low);
    return k;
}

static Conv *apply_channel(Server *s, JsonObject *info) {
    const char *name = jstr(info, "name");
    if (!name) return NULL;
    char *key = str_lower(name);
    Conv *c = server_conv(s, key);
    if (!c) {
        c = conv_new(CONV_CHANNEL, key, name, name);
        g_hash_table_insert(s->convs, g_strdup(key), c);
    }
    g_free(c->topic); c->topic = g_strdup(jstr(info, "topic"));
    g_free(c->owner); c->owner = g_strdup(jstr(info, "owner"));
    g_hash_table_remove_all(c->members);
    JsonArray *ms = json_object_has_member(info, "members") && JSON_NODE_HOLDS_ARRAY(json_object_get_member(info, "members"))
                        ? json_object_get_array_member(info, "members") : NULL;
    for (guint i = 0; ms && i < json_array_get_length(ms); i++) {
        JsonObject *m = json_array_get_object_element(ms, i);
        remember_user(s, m);
        Member *mem = g_new0(Member, 1);
        mem->username = g_strdup(jstr(m, "username"));
        mem->role = g_strdup(jstr(m, "role") ? jstr(m, "role") : "member");
        mem->online = jbool(m, "online");
        g_hash_table_replace(c->members, str_lower(mem->username), mem);
    }
    g_free(key);
    return c;
}

static void token_done(guint status, JsonNode *root, const char *error, gpointer data) {
    Server *s = data;
    if (!s->conn || s->removed) { server_unref(s); return; }
    if (error || !root || !JSON_NODE_HOLDS_OBJECT(root)) {
        char *t = g_strdup_printf("No token: %s", error ? error : "unknown error");
        server_disconnect(s, t);
        s->state = ST_ERROR;
        ui_changed(s, NULL, FALSE);
        g_free(t);
        if (status == 401) app_logout();
        server_unref(s);
        return;
    }
    JsonBuilder *b = proto_begin("auth");
    proto_str(b, "token", jstr(json_node_get_object(root), "token"));
    server_send(s, b);
    set_state(s, ST_CONNECTING, "Server is checking login …");
    server_unref(s);
}

static void handle_message(Server *s, JsonObject *o) {
    const char *op = jstr(o, "op");
    if (!op) return;

    if (!strcmp(op, "ping")) { server_send(s, proto_begin("pong")); return; }

    if (!strcmp(op, "hello")) {
        JsonObject *srv = json_object_has_member(o, "server") ? json_object_get_object_member(o, "server") : NULL;
        g_free(s->server_name); s->server_name = g_strdup(jstr(srv, "name"));
        g_free(s->motd); s->motd = g_strdup(jstr(srv, "motd"));
        char *want = g_strdup(app.session_url), *got = g_strdup(jstr(o, "sessionServer") ? jstr(o, "sessionServer") : "");
        g_strchomp(want); g_strchomp(got);
        while (*want && want[strlen(want) - 1] == '/') want[strlen(want) - 1] = 0;
        while (*got && got[strlen(got) - 1] == '/') got[strlen(got) - 1] = 0;
        if (g_ascii_strcasecmp(want, got) != 0) {
            char *t = g_strdup_printf("Server uses a different session server (%s)", got);
            server_disconnect(s, t);
            s->state = ST_ERROR;
            ui_changed(s, NULL, FALSE);
            g_free(t);
        } else {
            set_state(s, ST_CONNECTING, "Fetching login token …");
            char *hp = hostport(s);
            session_server_token(hp, token_done, server_ref(s));
            g_free(hp);
        }
        g_free(want); g_free(got);
        return;
    }

    if (!strcmp(op, "welcome")) {
        s->retry = 0;
        JsonObject *me = json_object_get_object_member(o, "me");
        g_free(s->me); s->me = str_lower(jstr(me, "username"));
        remember_user(s, me);
        s->is_admin = jbool(o, "isAdmin");
        JsonObject *srv = json_object_has_member(o, "server") ? json_object_get_object_member(o, "server") : NULL;
        if (srv) { g_free(s->server_name); s->server_name = g_strdup(jstr(srv, "name")); g_free(s->motd); s->motd = g_strdup(jstr(srv, "motd")); }

        GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        JsonArray *chs = json_object_get_array_member(o, "channels");
        for (guint i = 0; chs && i < json_array_get_length(chs); i++) {
            Conv *c = apply_channel(s, json_array_get_object_element(chs, i));
            if (c) g_hash_table_add(seen, g_strdup(c->key));
        }
        JsonArray *dms = json_object_has_member(o, "dms") && JSON_NODE_HOLDS_ARRAY(json_object_get_member(o, "dms")) ? json_object_get_array_member(o, "dms") : NULL;
        for (guint i = 0; dms && i < json_array_get_length(dms); i++) {
            JsonObject *d = json_array_get_object_element(dms, i);
            JsonObject *u = json_object_get_object_member(d, "user");
            remember_user(s, u);
            Conv *c = server_ensure_dm(s, jstr(u, "username"));
            g_free(c->name); c->name = g_strdup(server_profile(s, jstr(u, "username"))->display_name);
            c->online = jbool(d, "online");
            c->last_ts = jint(d, "lastTs");
            g_hash_table_add(seen, g_strdup(c->key));
        }
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, s->convs);
        while (g_hash_table_iter_next(&it, &k, &v)) {
            Conv *c = v;
            if (c->type == CONV_STATUS) continue;
            if (c->type == CONV_CHANNEL && !g_hash_table_contains(seen, k)) { g_hash_table_iter_remove(&it); continue; }
            g_ptr_array_set_size(c->messages, 0);
            c->history_loaded = FALSE;
        }
        g_hash_table_destroy(seen);
        Conv *st = server_status_conv(s);
        if (st->messages->len == 0 && s->motd) server_system(s, "*", s->motd, "motd");
        set_state(s, ST_ONLINE, "connected");
        g_hash_table_iter_init(&it, s->convs);
        while (g_hash_table_iter_next(&it, &k, NULL)) ui_conv_ready(s, k);
        if (app.active == s) {
            Conv *c = server_conv(s, app.active_key);
            if (c && !c->history_loaded) server_load_history(s, c);
        }
        ui_changed(s, NULL, TRUE);
        return;
    }

    if (!strcmp(op, "channel")) {
        Conv *c = apply_channel(s, json_object_get_object_member(o, "channel"));
        ui_changed(s, NULL, FALSE);
        if (c) ui_conv_ready(s, c->key);
        return;
    }

    if (!strcmp(op, "member_join")) {
        char *key = str_lower(jstr(o, "channel"));
        Conv *c = server_conv(s, key);
        JsonObject *m = json_object_get_object_member(o, "member");
        if (c && m) {
            remember_user(s, m);
            Member *mem = g_new0(Member, 1);
            mem->username = g_strdup(jstr(m, "username"));
            mem->role = g_strdup(jstr(m, "role") ? jstr(m, "role") : "member");
            mem->online = jbool(m, "online");
            char *low = str_lower(mem->username);
            gboolean self = g_strcmp0(low, s->me) == 0;
            g_hash_table_replace(c->members, low, mem);
            if (!self) {
                char *t = g_strdup_printf("%s joined", server_profile(s, mem->username)->display_name);
                server_system(s, key, t, "join");
                g_free(t);
            }
        }
        g_free(key);
        ui_changed(s, NULL, FALSE);
        return;
    }

    if (!strcmp(op, "member_part")) {
        char *key = str_lower(jstr(o, "channel"));
        Conv *c = server_conv(s, key);
        const char *user = jstr(o, "username"), *by = jstr(o, "kickedBy"), *reason = jstr(o, "reason");
        if (c && user) {
            char *low = str_lower(user);
            if (!g_strcmp0(low, s->me)) {
                if (by) {
                    char *t = g_strdup_printf("You were %s from %s by %s%s%s", jbool(o, "banned") ? "banned" : "kicked", c->name, by, reason && *reason ? ": " : "", reason ? reason : "");
                    server_system(s, "*", t, "error");
                    g_free(t);
                }
                g_hash_table_remove(s->convs, key);
                ui_conv_removed(s, key);
            } else {
                g_hash_table_remove(c->members, low);
                const char *dn = server_profile(s, user)->display_name;
                char *t = by ? g_strdup_printf("%s was %s by %s%s%s%s", dn, jbool(o, "banned") ? "banned" : "kicked", by, reason && *reason ? " (" : "", reason ? reason : "", reason && *reason ? ")" : "")
                             : g_strdup_printf("%s left the channel", dn);
                server_system(s, key, t, "part");
                g_free(t);
            }
            g_free(low);
        }
        g_free(key);
        ui_changed(s, NULL, FALSE);
        return;
    }

    if (!strcmp(op, "channel_deleted")) {
        char *key = str_lower(jstr(o, "channel"));
        g_hash_table_remove(s->convs, key);
        char *t = g_strdup_printf("%s was deleted", jstr(o, "channel"));
        server_system(s, "*", t, "system");
        g_free(t);
        ui_conv_removed(s, key);
        g_free(key);
        return;
    }

    if (!strcmp(op, "message")) {
        Message *m = message_from_json(json_object_get_object_member(o, "message"));
        if (!m) return;
        char *key = conv_key_for_message(s, m);
        Conv *c = server_conv(s, key);
        if (!c && key[0] == '@') c = server_ensure_dm(s, key + 1);
        if (!c) { message_free(m); g_free(key); return; }
        char *from = str_lower(m->from);
        g_hash_table_remove(c->typing, from);
        gboolean own = !g_strcmp0(from, s->me);
        gboolean mention = !own && (c->type == CONV_DM || text_mentions(m->text, s->me));
        gboolean keep = c->history_loaded;
        if (keep) conv_push(c, m);
        else c->last_ts = m->ts;
        ui_new_message(s, c, m, mention);
        if (!keep) message_free(m);
        g_free(from); g_free(key);
        return;
    }

    if (!strcmp(op, "message_edit") || !strcmp(op, "message_delete")) {
        gboolean del = !strcmp(op, "message_delete");
        Message *m = del ? NULL : message_from_json(json_object_get_object_member(o, "message"));
        Message tmp = { .target = (char *)jstr(o, "target"), .from = (char *)jstr(o, "from") };
        char *key = conv_key_for_message(s, del ? &tmp : m);
        const char *id = del ? jstr(o, "id") : m->id;
        Conv *c = server_conv(s, key);
        for (guint i = 0; c && i < c->messages->len; i++) {
            Message *x = g_ptr_array_index(c->messages, i);
            if (!g_strcmp0(x->id, id)) {
                if (del) g_ptr_array_remove_index(c->messages, i);
                else { g_free(x->text); x->text = g_strdup(m->text); x->edited = m->edited; }
                break;
            }
        }
        if (m) message_free(m);
        ui_changed(s, key, TRUE);
        g_free(key);
        return;
    }

    if (!strcmp(op, "history")) {
        char *key = key_for_target(jstr(o, "target"));
        Conv *c = server_conv(s, key);
        if (c) {
            GHashTable *known = g_hash_table_new(g_str_hash, g_str_equal);
            for (guint i = 0; i < c->messages->len; i++) g_hash_table_add(known, ((Message *)g_ptr_array_index(c->messages, i))->id);
            GPtrArray *merged = g_ptr_array_new_with_free_func((GDestroyNotify)message_free);
            JsonArray *arr = json_object_get_array_member(o, "messages");
            for (guint i = 0; arr && i < json_array_get_length(arr); i++) {
                Message *m = message_from_json(json_array_get_object_element(arr, i));
                if (g_hash_table_contains(known, m->id)) { message_free(m); continue; }
                g_ptr_array_add(merged, m);
                if (m->ts > c->last_ts) c->last_ts = m->ts;
            }
            g_hash_table_destroy(known);
            /* Append existing messages (discard system messages before the first load). */
            for (guint i = 0; i < c->messages->len; i++) {
                Message *m = g_ptr_array_index(c->messages, i);
                if (!c->history_loaded && g_str_has_prefix(m->id, "sys")) continue;
                g_ptr_array_add(merged, m);
                c->messages->pdata[i] = NULL;
            }
            g_ptr_array_free(c->messages, TRUE);
            c->messages = merged;
            c->has_more = jbool(o, "hasMore");
            c->history_loaded = TRUE;
        }
        ui_changed(s, key, TRUE);
        g_free(key);
        return;
    }

    if (!strcmp(op, "dm_open")) {
        JsonObject *d = json_object_get_object_member(o, "dm");
        JsonObject *u = json_object_get_object_member(d, "user");
        remember_user(s, u);
        Conv *c = server_ensure_dm(s, jstr(u, "username"));
        g_free(c->name); c->name = g_strdup(server_profile(s, jstr(u, "username"))->display_name);
        c->online = jbool(d, "online");
        if (jint(d, "lastTs") > c->last_ts) c->last_ts = jint(d, "lastTs");
        ui_changed(s, NULL, FALSE);
        ui_conv_ready(s, c->key);
        return;
    }

    if (!strcmp(op, "typing")) {
        char *key = key_for_target(jstr(o, "target"));
        Conv *c = server_conv(s, key);
        if (c) g_hash_table_replace(c->typing, str_lower(jstr(o, "username")), (gpointer)(gintptr)(g_get_monotonic_time() / 1000 + 6000));
        ui_changed(s, key, FALSE);
        g_free(key);
        return;
    }

    if (!strcmp(op, "topic")) {
        char *key = str_lower(jstr(o, "channel"));
        Conv *c = server_conv(s, key);
        if (c) {
            g_free(c->topic); c->topic = g_strdup(jstr(o, "topic"));
            char *t = g_strdup_printf("%s changed the topic: %s", jstr(o, "by"), c->topic && *c->topic ? c->topic : "(empty)");
            server_system(s, key, t, "topic");
            g_free(t);
        }
        g_free(key);
        ui_changed(s, NULL, FALSE);
        return;
    }

    if (!strcmp(op, "role")) {
        char *key = str_lower(jstr(o, "channel"));
        Conv *c = server_conv(s, key);
        char *u = str_lower(jstr(o, "username"));
        Member *m = c ? g_hash_table_lookup(c->members, u) : NULL;
        if (m) { g_free(m->role); m->role = g_strdup(jstr(o, "role")); }
        g_free(u); g_free(key);
        ui_changed(s, NULL, FALSE);
        return;
    }

    if (!strcmp(op, "presence")) {
        char *u = str_lower(jstr(o, "username"));
        gboolean on = jbool(o, "online");
        GHashTableIter it;
        gpointer v;
        g_hash_table_iter_init(&it, s->convs);
        while (g_hash_table_iter_next(&it, NULL, &v)) {
            Conv *c = v;
            Member *m = g_hash_table_lookup(c->members, u);
            if (m) m->online = on;
            if (c->type == CONV_DM) {
                char *t = str_lower(c->target);
                if (!g_strcmp0(t, u)) c->online = on;
                g_free(t);
            }
            if (!on) g_hash_table_remove(c->typing, u);
        }
        g_free(u);
        ui_changed(s, NULL, FALSE);
        return;
    }

    if (!strcmp(op, "user_update")) {
        JsonObject *u = json_object_get_object_member(o, "user");
        remember_user(s, u);
        char *low = str_lower(jstr(u, "username"));
        char *key = g_strconcat("@", low, NULL);
        Conv *c = server_conv(s, key);
        if (c) { g_free(c->name); c->name = g_strdup(server_profile(s, low)->display_name); }
        g_free(key); g_free(low);
        ui_changed(s, NULL, TRUE);
        return;
    }

    if (!strcmp(op, "channel_list")) {
        dialog_channel_list(s, json_object_get_array_member(o, "channels"));
        return;
    }

    if (!strcmp(op, "whois")) {
        JsonObject *u = json_object_get_object_member(o, "user");
        remember_user(s, u);
        GString *t = g_string_new(NULL);
        g_string_printf(t, "%s (@%s) – %s", jstr(u, "displayName"), jstr(u, "username"), jbool(o, "online") ? "online" : "offline");
        JsonArray *sc = json_object_has_member(o, "sharedChannels") && JSON_NODE_HOLDS_ARRAY(json_object_get_member(o, "sharedChannels")) ? json_object_get_array_member(o, "sharedChannels") : NULL;
        if (sc && json_array_get_length(sc)) {
            g_string_append(t, " – shared channels:");
            for (guint i = 0; i < json_array_get_length(sc); i++) g_string_append_printf(t, " %s", json_array_get_string_element(sc, i));
        }
        if (jstr(u, "bio")) g_string_append_printf(t, " – %s", jstr(u, "bio"));
        server_system(s, app.active == s ? app.active_key : "*", t->str, "system");
        g_string_free(t, TRUE);
        return;
    }

    if (!strcmp(op, "error")) {
        const char *code = jstr(o, "code"), *msg = jstr(o, "message");
        if (code && !strcmp(code, "auth_failed")) {
            char *t = g_strdup_printf("Login rejected: %s", msg ? msg : "");
            server_system(s, "*", t, "error");
            server_disconnect(s, t);
            s->state = ST_ERROR;
            ui_changed(s, NULL, FALSE);
            g_free(t);
            return;
        }
        server_system(s, app.active == s && server_conv(s, app.active_key) ? app.active_key : "*", msg ? msg : "Error", "error");
        return;
    }
}

/* ---------- Reading ---------- */

static void read_line(Server *s);

static void read_done(GObject *src, GAsyncResult *res, gpointer data) {
    Server *s = data;
    GError *err = NULL;
    gsize len = 0;
    char *line = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(src), res, &len, &err);
    if (err) {
        if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED) && G_INPUT_STREAM(src) == (GInputStream *)s->in)
            connection_lost(s, err->domain == G_IO_ERROR && err->code == G_IO_ERROR_INVALID_DATA ? "Invalid data from server" : "Connection interrupted");
        g_error_free(err);
        server_unref(s);
        return;
    }
    if (G_INPUT_STREAM(src) != (GInputStream *)s->in) { g_free(line); server_unref(s); return; }
    if (!line) {
        connection_lost(s, "Server closed the connection");
        server_unref(s);
        return;
    }
    if (len > 0) {
        JsonParser *p = json_parser_new();
        if (json_parser_load_from_data(p, line, len, NULL)) {
            JsonNode *root = json_parser_get_root(p);
            if (root && JSON_NODE_HOLDS_OBJECT(root)) handle_message(s, json_node_get_object(root));
        }
        g_object_unref(p);
    }
    g_free(line);
    if (s->in && G_INPUT_STREAM(src) == (GInputStream *)s->in) read_line(s);
    server_unref(s);
}

static void read_line(Server *s) {
    g_data_input_stream_read_line_async(s->in, G_PRIORITY_DEFAULT, s->cancel, read_done, server_ref(s));
}

/* ---------- Connecting (including TLS certificate pinning) ---------- */

static char *cert_fingerprint(GTlsCertificate *cert) {
    GByteArray *der = NULL;
    g_object_get(cert, "certificate", &der, NULL);
    if (!der) return NULL;
    char *hex = g_compute_checksum_for_data(G_CHECKSUM_SHA256, der->data, der->len);
    g_byte_array_unref(der);
    GString *out = g_string_new(NULL);
    for (int i = 0; hex[i]; i += 2) {
        if (i) g_string_append_c(out, ':');
        g_string_append_c(out, g_ascii_toupper(hex[i]));
        g_string_append_c(out, g_ascii_toupper(hex[i + 1]));
    }
    g_free(hex);
    return g_string_free(out, FALSE);
}

/* Called only when the certificate is NOT signed by a known CA
 * (e.g. self-signed). Trust on First Use applies: if the fingerprint is
 * stored and matches, connect; otherwise ask the client. */
static gboolean on_accept_cert(GTlsConnection *conn, GTlsCertificate *cert, GTlsCertificateFlags errors, gpointer data) {
    (void)conn; (void)errors;
    Server *s = data;
    char *fp = cert_fingerprint(cert);
    char *hp = hostport(s);
    char *pin = config_get_pin(hp);
    gboolean ok = fp && pin && !g_ascii_strcasecmp(fp, pin);
    if (!ok) {
        g_free(s->pending_fp);
        s->pending_fp = g_strdup(fp);
        s->pending_mismatch = pin != NULL;
    }
    g_free(fp); g_free(hp); g_free(pin);
    return ok;
}

static void on_client_event(GSocketClient *client, GSocketClientEvent ev, GSocketConnectable *c, GIOStream *stream, gpointer data) {
    (void)client; (void)c;
    if (ev == G_SOCKET_CLIENT_TLS_HANDSHAKING && G_IS_TLS_CONNECTION(stream))
        g_signal_connect(stream, "accept-certificate", G_CALLBACK(on_accept_cert), data);
}

static void connect_done(GObject *src, GAsyncResult *res, gpointer data) {
    Server *s = data;
    GError *err = NULL;
    GSocketConnection *conn = g_socket_client_connect_finish(G_SOCKET_CLIENT(src), res, &err);
    if ((GSocketClient *)src != s->client) {
        if (conn) g_object_unref(conn);
        if (err) g_error_free(err);
        server_unref(s);
        return;
    }
    if (err) {
        gboolean cancelled = g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        if (!cancelled) {
            if (s->pending_fp) {
                close_streams(s);
                set_state(s, ST_ERROR, s->pending_mismatch ? "Certificate has changed!" : "Unknown certificate – please confirm");
                dialog_trust_cert(s);
            } else {
                char *t = g_strdup_printf("No connection: %s", err->message);
                connection_lost(s, t);
                g_free(t);
            }
        }
        g_error_free(err);
        server_unref(s);
        return;
    }
    s->conn = conn;
    s->in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(conn)));
    g_data_input_stream_set_newline_type(s->in, G_DATA_STREAM_NEWLINE_TYPE_ANY);
    s->out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    set_state(s, ST_CONNECTING, "Connected, waiting for server …");
    read_line(s);
    server_unref(s);
}

void server_connect(Server *s) {
    if (s->removed) return;
    close_streams(s);
    if (s->reconnect_id) { g_source_remove(s->reconnect_id); s->reconnect_id = 0; }
    s->manual_close = FALSE;
    g_clear_pointer(&s->pending_fp, g_free);
    s->cancel = g_cancellable_new();
    s->client = g_socket_client_new();
    g_socket_client_set_timeout(s->client, 20);
    g_socket_client_set_tls(s->client, s->cfg.tls);
    g_signal_connect(s->client, "event", G_CALLBACK(on_client_event), s);
    GSocketConnectable *addr = g_network_address_new(s->cfg.host, s->cfg.port);
    char *t = g_strdup_printf("Connecting to %s:%d …", s->cfg.host, s->cfg.port);
    set_state(s, ST_CONNECTING, t);
    g_free(t);
    g_socket_client_connect_async(s->client, addr, s->cancel, connect_done, server_ref(s));
    g_object_unref(addr);
}

/* ---------- Actions ---------- */

void server_load_history(Server *s, Conv *c) {
    if (!c || c->type == CONV_STATUS || s->state != ST_ONLINE) return;
    JsonBuilder *b = proto_begin("history");
    proto_str(b, "target", c->target);
    if (c->history_loaded) {
        for (guint i = 0; i < c->messages->len; i++) {
            Message *m = g_ptr_array_index(c->messages, i);
            if (!g_str_has_prefix(m->id, "sys")) { proto_str(b, "before", m->id); break; }
        }
    }
    proto_int(b, "limit", 50);
    server_send(s, b);
}

void server_send_message(Server *s, Conv *c, const char *text, const char *kind) {
    if (!c || c->type == CONV_STATUS) return;
    JsonBuilder *b = proto_begin("msg");
    proto_str(b, "target", c->target);
    proto_str(b, "text", text);
    proto_str(b, "kind", kind ? kind : "text");
    server_send(s, b);
}

void server_typing(Server *s, Conv *c) {
    gint64 now = g_get_monotonic_time() / 1000;
    if (!c || c->type == CONV_STATUS || s->state != ST_ONLINE || now - s->last_typing < 3000) return;
    s->last_typing = now;
    JsonBuilder *b = proto_begin("typing");
    proto_str(b, "target", c->target);
    server_send(s, b);
}

gboolean server_can_moderate(Server *s, Conv *c) {
    if (!c || c->type != CONV_CHANNEL || !s->me) return FALSE;
    if (s->is_admin) return TRUE;
    Member *m = g_hash_table_lookup(c->members, s->me);
    return m && (!g_strcmp0(m->role, "op") || !g_strcmp0(m->role, "owner"));
}

void servers_refresh_profile(void) {
    for (guint i = 0; i < app.servers->len; i++) {
        Server *s = g_ptr_array_index(app.servers, i);
        if (s->state == ST_ONLINE) server_send(s, proto_begin("refresh_profile"));
    }
}
