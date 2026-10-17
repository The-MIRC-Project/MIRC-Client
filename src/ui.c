/* Main window: server rail, channels/DMs, chat history, member list. */
#include "mirc.h"
#include <string.h>

void ui_conv_ready(Server *s, const char *key);

static struct {
    GtkWidget *win, *header;
    GtkWidget *rail, *sidebar_title, *sidebar_status, *server_menu_btn, *conv_list, *user_panel;
    GtkWidget *chat_title, *chat_topic, *members_btn, *msg_scroll, *msg_list, *typing, *entry, *edit_label;
    GtkWidget *members_scroll, *members_list, *main_stack;
    GtkCssProvider *theme_css;
    guint render_id;
    gboolean rebuild_messages, show_members, stick;
    double keep_from_bottom;
    char *editing_id;
    Server *pending_server;
    char *pending_key;
    gint64 last_history_req;
} ui;

GtkWindow *ui_window(void) { return GTK_WINDOW(ui.win); }

void ui_open_url(const char *url) {
    GError *err = NULL;
    if (!gtk_show_uri_on_window(GTK_WINDOW(ui.win), url, GDK_CURRENT_TIME, &err)) {
        ui_toast(err ? err->message : "Could not open link");
        if (err) g_error_free(err);
    }
}

static gboolean on_activate_link(GtkLabel *l, const char *uri, gpointer d) {
    (void)l; (void)d;
    if (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://")) ui_open_url(uri);
    return TRUE;
}

/* ---------- kleine Widget-Helfer ---------- */

static GtkWidget *label(const char *text, const char *css_class) {
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    if (css_class) gtk_style_context_add_class(gtk_widget_get_style_context(l), css_class);
    return l;
}

static GtkWidget *markup_label(const char *markup, const char *css_class) {
    GtkWidget *l = label(NULL, css_class);
    gtk_label_set_markup(GTK_LABEL(l), markup);
    return l;
}

static void add_class(GtkWidget *w, const char *c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

static void clear_container(GtkWidget *c) {
    GList *kids = gtk_container_get_children(GTK_CONTAINER(c));
    for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
    g_list_free(kids);
}

void ui_toast(const char *text) {
    if (!ui.win) { g_message("%s", text); return; }
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(ui.win), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", text);
    g_signal_connect(d, "response", G_CALLBACK(gtk_widget_destroy), NULL);
    gtk_widget_show(d);
}

static Conv *active_conv(void) { return app.active ? server_conv(app.active, app.active_key) : NULL; }

/* ---------- Theme ---------- */

static const char *BASE_CSS =
    "window, .main { background: #1f1f23; color: #e9e7ef; } label { color: #e9e7ef; }"
    "headerbar { background: #111113; border: 0; box-shadow: none; color: #e9e7ef; }"
    "headerbar .title { font-weight: 700; }"
    ".rail { background: #111113; padding: 10px 0; }"
    ".rail list, .sidebar list, .members list, .messages list { background: transparent; }"
    ".rail-item { min-width: 48px; min-height: 48px; border-radius: 24px; background: #2a2a30; color: #e9e7ef; font-weight: 800; padding: 0; margin: 4px 12px; border: 0; box-shadow: none; }"
    ".rail-item:hover, .rail-item.active { border-radius: 15px; background: @accent; color: #fff; }"
    ".rail-item.offline { opacity: 0.55; }"
    ".rail-item.add { color: #3ccf7f; font-size: 22px; }"
    ".rail-badge { background: #ef5a5a; color: white; border-radius: 9px; padding: 0 5px; font-size: 10px; font-weight: 800; }"
    ".sidebar { background: #18181b; }"
    ".server-head { padding: 12px 10px 12px 16px; border-bottom: 1px solid #2c2c33; }"
    ".server-title { font-weight: 800; font-size: 15px; }"
    ".status { color: #9a98a6; font-size: 12px; }"
    ".status.online { color: #3ccf7f; } .status.error { color: #ef5a5a; }"
    ".section { color: #9a98a6; font-size: 11px; font-weight: 800; padding: 14px 8px 4px 8px; }"
    ".conv-row { border-radius: 7px; margin: 1px 8px; padding: 6px 8px; color: #9a98a6; }"
    ".conv-row:hover { background: #2a2a30; color: #e9e7ef; }"
    ".conv-row.active { background: #34343c; color: #e9e7ef; }"
    ".conv-row.unread { color: #e9e7ef; font-weight: 700; }"
    ".badge { background: #ef5a5a; color: #fff; border-radius: 9px; padding: 0 6px; font-size: 11px; font-weight: 800; }"
    ".presence { color: #6c6b75; font-size: 10px; } .presence.on { color: #3ccf7f; }"
    ".user-panel { background: #141416; padding: 8px 10px; }"
    ".muted { color: #9a98a6; } .small { font-size: 12px; }"
    ".chat-head { padding: 12px 16px; border-bottom: 1px solid #2c2c33; }"
    ".chat-title { font-weight: 800; font-size: 16px; }"
    ".topic { color: #9a98a6; padding-left: 12px; }"
    ".msg { padding: 2px 48px 2px 16px; }"
    ".msg.first { padding-top: 12px; }"
    ".msg.mention { background: rgba(242,184,75,0.10); box-shadow: inset 3px 0 #f2b84b; }"
    ".msg.editing { background: alpha(@accent, 0.14); box-shadow: inset 3px 0 @accent; }"
    ".msg:hover { background: rgba(0,0,0,0.12); }"
    ".msg-text { color: #e9e7ef; }"
    ".msg-text.action { font-style: italic; }"
    ".time { color: #9a98a6; font-size: 11px; }"
    ".sys { color: #9a98a6; font-size: 13px; padding: 2px 16px 2px 70px; }"
    ".sys.error { color: #ef5a5a; } .sys.motd { font-family: monospace; color: #c9c7d1; } .sys.join { color: #7fd8a5; }"
    ".day { color: #9a98a6; font-size: 12px; font-weight: 700; padding: 16px 16px 4px 16px; }"
    ".intro { padding: 24px 16px 8px 16px; }"
    ".intro-title { font-size: 24px; font-weight: 800; color: #e9e7ef; }"
    ".composer { background: #2a2a30; border-radius: 12px; margin: 0 16px; padding: 4px 6px; }"
    ".composer entry { background: transparent; border: 0; box-shadow: none; color: #e9e7ef; padding: 8px; min-height: 26px; }"
    ".typing { color: #9a98a6; font-size: 12px; padding: 2px 20px 6px 20px; min-height: 18px; }"
    ".edit-banner { color: #9a98a6; font-size: 12px; padding: 0 20px 4px 20px; }"
    ".members { background: #18181b; }"
    ".member-row { padding: 4px 10px; border-radius: 7px; margin: 0 6px; }"
    ".member-row:hover { background: #2a2a30; }"
    ".member-row.off { opacity: 0.45; }"
    ".role { color: #f2b84b; font-weight: 800; }"
    "button.flat-btn { background: transparent; border: 0; box-shadow: none; color: #9a98a6; padding: 4px 8px; }"
    "button.flat-btn:hover { background: #2a2a30; color: #e9e7ef; }"
    "button.primary { background: @accent; color: white; border: 0; box-shadow: none; font-weight: 700; padding: 6px 14px; }"
    "button.ghost { background: #2a2a30; color: #e9e7ef; border: 0; box-shadow: none; padding: 6px 14px; }"
    "button.danger { background: #c94545; color: white; border: 0; box-shadow: none; padding: 6px 14px; }"
    "dialog button, messagedialog button, .dialog-action-area button { background: #2a2a30; color: #e9e7ef; border: 0; box-shadow: none; padding: 8px 14px; text-shadow: none; }"
    "dialog button.primary, messagedialog button.primary { background: @accent; color: white; }"
    "dialog button.danger, messagedialog button.danger { background: #c94545; color: white; }"
    "entry { background: #111113; color: #e9e7ef; border: 1px solid #2c2c33; border-radius: 8px; padding: 7px 10px; }"
    "entry:focus { border-color: @accent; }"
    "dialog, dialog .dialog-vbox, messagedialog { background: #18181b; color: #e9e7ef; }"
    "popover.profile { background: #111113; padding: 0; border-radius: 14px; }"
    ".card-name { font-size: 20px; font-weight: 800; }"
    ".card-body { background: rgba(0,0,0,0.28); border-radius: 10px; padding: 12px; margin: 8px 12px 12px 12px; }"
    "menu, .menu { background: #111113; color: #e9e7ef; }"
    "scrollbar slider { background: #34343c; min-width: 6px; border-radius: 6px; }"
    "link, *:link { color: #b3a9ff; }";

void ui_apply_theme(void) {
    const char *accent = app.me && app.me->accent ? app.me->accent : "#7c6cf2";
    char *css = g_strdup_printf("@define-color accent %s; %s", accent, BASE_CSS);
    if (!ui.theme_css) {
        ui.theme_css = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(ui.theme_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    gtk_css_provider_load_from_data(ui.theme_css, css, -1, NULL);
    g_free(css);
}

/* ---------- Selection & changes ---------- */

static gboolean render_all(gpointer d);

void ui_changed(Server *s, const char *conv_key, gboolean messages) {
    if (messages && (!s || (s == app.active && (!conv_key || !g_strcmp0(conv_key, app.active_key)))))
        ui.rebuild_messages = TRUE;
    if (!ui.render_id && ui.win) ui.render_id = g_idle_add(render_all, NULL);
}

void ui_select(Server *s, const char *key) {
    if (!s) return;
    if (!server_conv(s, key)) key = "*";
    app.active = s;
    g_free(app.active_key);
    app.active_key = g_strdup(key);
    g_clear_pointer(&ui.editing_id, g_free);
    Conv *c = active_conv();
    if (c) {
        c->unread = c->mentions = 0;
        if (!c->history_loaded) server_load_history(s, c);
    }
    config_set_string("ui", "last_server", s->cfg.id);
    config_set_string("ui", "last_conv", key);
    ui.stick = TRUE;
    ui.rebuild_messages = TRUE;
    ui_changed(s, NULL, TRUE);
    gtk_widget_grab_focus(ui.entry);
}

void ui_switch_when_joined(Server *s, const char *key) {
    ui.pending_server = s;
    g_free(ui.pending_key);
    ui.pending_key = g_strdup(key);
    if (server_conv(s, key)) ui_conv_ready(s, key);
}

void ui_conv_ready(Server *s, const char *key) {
    if (ui.pending_server == s && ui.pending_key && !g_strcmp0(ui.pending_key, key)) {
        ui.pending_server = NULL;
        g_clear_pointer(&ui.pending_key, g_free);
        ui_select(s, key);
    }
}

void ui_conv_removed(Server *s, const char *key) {
    if (app.active == s && !g_strcmp0(app.active_key, key)) ui_select(s, "*");
    else ui_changed(s, NULL, FALSE);
}

void ui_new_message(Server *s, Conv *c, Message *m, gboolean mention) {
    gboolean visible = app.active == s && !g_strcmp0(app.active_key, c->key);
    gboolean focused = ui.win && gtk_window_is_active(GTK_WINDOW(ui.win));
    char *from = str_lower(m ? m->from : "");
    gboolean own = s->me && !g_strcmp0(from, s->me);
    gboolean system = !m || !m->from || !*m->from;
    if (!visible && !own && !system) {
        c->unread++;
        if (mention) c->mentions++;
    }
    if (mention && m && (!visible || !focused)) {
        const Profile *p = server_profile(s, m->from);
        char *title = c->type == CONV_CHANNEL ? g_strdup_printf("%s in %s", p->display_name, c->name) : g_strdup(p->display_name);
        GNotification *n = g_notification_new(title);
        g_notification_set_body(n, m->text);
        g_application_send_notification(G_APPLICATION(app.gapp), NULL, n);
        g_object_unref(n);
        g_free(title);
    }
    g_free(from);
    if (visible) ui.rebuild_messages = TRUE;
    ui_changed(s, NULL, FALSE);
}

/* ---------- Serverleiste ---------- */

static void on_rail_clicked(GtkButton *b, gpointer d) {
    (void)b;
    Server *s = d;
    char *last = g_strdup(g_object_get_data(G_OBJECT(b), "last") ? g_object_get_data(G_OBJECT(b), "last") : "*");
    ui_select(s, last);
    g_free(last);
}

static void on_add_server(GtkButton *b, gpointer d) { (void)b; (void)d; dialog_add_server(NULL); }

static char *short_name(const char *t) {
    char **words = g_regex_split_simple("[^\\p{L}\\p{N}]+", t ? t : "?", 0, 0);
    GString *out = g_string_new(NULL);
    int n = 0;
    for (int i = 0; words[i] && n < 2; i++) {
        if (!*words[i]) continue;
        g_string_append_unichar(out, g_unichar_toupper(g_utf8_get_char(words[i])));
        if (n == 0 && (!words[i + 1] || !*words[i + 1])) {
            const char *second = g_utf8_next_char(words[i]);
            if (*second) g_string_append_unichar(out, g_unichar_toupper(g_utf8_get_char(second)));
            n = 2;
            break;
        }
        n++;
    }
    g_strfreev(words);
    if (!out->len) g_string_append(out, "?");
    return g_string_free(out, FALSE);
}

static GHashTable *last_conv_per_server;

static void render_rail(void) {
    clear_container(ui.rail);
    for (guint i = 0; i < app.servers->len; i++) {
        Server *s = g_ptr_array_index(app.servers, i);
        int mentions = 0, unread = 0;
        GHashTableIter it;
        gpointer v;
        g_hash_table_iter_init(&it, s->convs);
        while (g_hash_table_iter_next(&it, NULL, &v)) { mentions += ((Conv *)v)->mentions; unread += ((Conv *)v)->unread; }
        char *sn = short_name(server_title(s));
        GtkWidget *overlay = gtk_overlay_new();
        GtkWidget *b = gtk_button_new_with_label(sn);
        g_free(sn);
        add_class(b, "rail-item");
        if (s == app.active) add_class(b, "active");
        if (s->state != ST_ONLINE) add_class(b, "offline");
        char *tip = g_strdup_printf("%s\n%s:%d%s\n%s", server_title(s), s->cfg.host, s->cfg.port, s->cfg.tls ? " (TLS)" : "", s->status_text);
        gtk_widget_set_tooltip_text(b, tip);
        g_free(tip);
        const char *last = g_hash_table_lookup(last_conv_per_server, s->cfg.id);
        if (last) g_object_set_data_full(G_OBJECT(b), "last", g_strdup(last), g_free);
        g_signal_connect(b, "clicked", G_CALLBACK(on_rail_clicked), s);
        gtk_container_add(GTK_CONTAINER(overlay), b);
        if (mentions || (unread && s != app.active)) {
            char *t = mentions ? g_strdup_printf("%d", mentions) : g_strdup("•");
            GtkWidget *badge = label(t, "rail-badge");
            g_free(t);
            gtk_widget_set_halign(badge, GTK_ALIGN_END);
            gtk_widget_set_valign(badge, GTK_ALIGN_END);
            gtk_widget_set_margin_end(badge, 8);
            gtk_overlay_add_overlay(GTK_OVERLAY(overlay), badge);
        }
        gtk_box_pack_start(GTK_BOX(ui.rail), overlay, FALSE, FALSE, 0);
    }
    GtkWidget *add = gtk_button_new_with_label("+");
    add_class(add, "rail-item");
    add_class(add, "add");
    gtk_widget_set_tooltip_text(add, "Add server");
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_server), NULL);
    gtk_box_pack_start(GTK_BOX(ui.rail), add, FALSE, FALSE, 0);
    gtk_widget_show_all(ui.rail);
}

/* ---------- Seitenleiste ---------- */

static void on_conv_row_activated(GtkListBox *lb, GtkListBoxRow *row, gpointer d) {
    (void)lb; (void)d;
    const char *key = g_object_get_data(G_OBJECT(row), "key");
    if (key && app.active) ui_select(app.active, key);
}

static void on_conv_part(GtkMenuItem *mi, gpointer d) {
    (void)d;
    const char *key = g_object_get_data(G_OBJECT(mi), "key");
    Conv *c = server_conv(app.active, key);
    if (!c) return;
    if (c->type == CONV_CHANNEL) {
        JsonBuilder *b = proto_begin("part");
        proto_str(b, "channel", c->target);
        server_send(app.active, b);
    } else if (c->type == CONV_DM) {
        char *k = g_strdup(key);
        g_hash_table_remove(app.active->convs, k);
        ui_conv_removed(app.active, k);
        g_free(k);
    }
}

static gboolean on_conv_button(GtkWidget *w, GdkEventButton *ev, gpointer d) {
    (void)d;
    if (ev->type != GDK_BUTTON_PRESS || ev->button != 3) return FALSE;
    const char *key = g_object_get_data(G_OBJECT(w), "key");
    Conv *c = app.active ? server_conv(app.active, key) : NULL;
    if (!c || c->type == CONV_STATUS) return FALSE;
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *mi = gtk_menu_item_new_with_label(c->type == CONV_CHANNEL ? "Leave channel" : "Hide conversation");
    g_object_set_data_full(G_OBJECT(mi), "key", g_strdup(key), g_free);
    g_signal_connect(mi, "activate", G_CALLBACK(on_conv_part), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)ev);
    return TRUE;
}

static gint conv_cmp(gconstpointer a, gconstpointer b) {
    const Conv *x = *(Conv *const *)a, *y = *(Conv *const *)b;
    if (x->type == CONV_DM && y->type == CONV_DM) return (y->last_ts > x->last_ts) - (y->last_ts < x->last_ts);
    return g_utf8_collate(x->key, y->key);
}

static void add_section(const char *title, GCallback plus_cb) {
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    char *up = g_utf8_strup(title, -1);
    GtkWidget *l = label(up, "section");
    g_free(up);
    gtk_box_pack_start(GTK_BOX(box), l, TRUE, TRUE, 0);
    if (plus_cb) {
        GtkWidget *b = gtk_button_new_with_label("+");
        add_class(b, "flat-btn");
        g_signal_connect(b, "clicked", plus_cb, NULL);
        gtk_box_pack_end(GTK_BOX(box), b, FALSE, FALSE, 4);
    }
    gtk_container_add(GTK_CONTAINER(row), box);
    gtk_container_add(GTK_CONTAINER(ui.conv_list), row);
}

static void on_add_channel(GtkButton *b, gpointer d) { (void)b; (void)d; if (app.active) dialog_join(app.active); }
static void on_add_dm(GtkButton *b, gpointer d) { (void)b; (void)d; if (app.active) dialog_new_dm(app.active); }

static void add_conv_row(Server *s, Conv *c) {
    GtkWidget *row = gtk_list_box_row_new();
    g_object_set_data_full(G_OBJECT(row), "key", g_strdup(c->key), g_free);
    GtkWidget *ev = gtk_event_box_new();
    g_object_set_data_full(G_OBJECT(ev), "key", g_strdup(c->key), g_free);
    g_signal_connect(ev, "button-press-event", G_CALLBACK(on_conv_button), NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    add_class(box, "conv-row");
    if (!g_strcmp0(c->key, app.active_key) && s == app.active) add_class(box, "active");
    if (c->unread) add_class(box, "unread");

    if (c->type == CONV_DM) {
        const Profile *p = server_profile(s, c->target);
        gtk_box_pack_start(GTK_BOX(box), avatar_new(p, 24), FALSE, FALSE, 0);
        char *nm = name_markup(p, c->target, 0);
        GtkWidget *l = markup_label(nm, NULL);
        g_free(nm);
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(box), l, TRUE, TRUE, 0);
        GtkWidget *dot = label("●", "presence");
        if (c->online) add_class(dot, "on");
        gtk_box_pack_start(GTK_BOX(box), dot, FALSE, FALSE, 0);
    } else {
        GtkWidget *icon = label(c->type == CONV_STATUS ? "≡" : "#", "muted");
        gtk_widget_set_size_request(icon, 18, -1);
        gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
        GtkWidget *l = label(c->type == CONV_STATUS ? "Server" : c->name + (c->name[0] == '#'), NULL);
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(box), l, TRUE, TRUE, 0);
    }
    if (c->mentions) {
        char *t = g_strdup_printf("%d", c->mentions);
        gtk_box_pack_end(GTK_BOX(box), label(t, "badge"), FALSE, FALSE, 0);
        g_free(t);
    }
    gtk_container_add(GTK_CONTAINER(ev), box);
    gtk_container_add(GTK_CONTAINER(row), ev);
    gtk_container_add(GTK_CONTAINER(ui.conv_list), row);
}

static void render_sidebar(void) {
    clear_container(ui.conv_list);
    Server *s = app.active;
    GtkStyleContext *sc = gtk_widget_get_style_context(ui.sidebar_status);
    gtk_style_context_remove_class(sc, "online");
    gtk_style_context_remove_class(sc, "error");
    if (!s) {
        gtk_label_set_text(GTK_LABEL(ui.sidebar_title), "No server");
        gtk_label_set_text(GTK_LABEL(ui.sidebar_status), "Add a server with +");
        gtk_widget_set_sensitive(ui.server_menu_btn, FALSE);
        return;
    }
    gtk_widget_set_sensitive(ui.server_menu_btn, TRUE);
    g_hash_table_replace(last_conv_per_server, g_strdup(s->cfg.id), g_strdup(app.active_key));
    gtk_label_set_text(GTK_LABEL(ui.sidebar_title), server_title(s));
    char *st = s->state == ST_ONLINE ? g_strdup_printf("● connected%s", s->cfg.tls ? " · TLS" : "") : g_strdup(s->status_text);
    gtk_label_set_text(GTK_LABEL(ui.sidebar_status), st);
    g_free(st);
    if (s->state == ST_ONLINE) gtk_style_context_add_class(sc, "online");
    if (s->state == ST_ERROR) gtk_style_context_add_class(sc, "error");

    add_conv_row(s, server_status_conv(s));
    GPtrArray *chs = g_ptr_array_new(), *dms = g_ptr_array_new();
    GHashTableIter it;
    gpointer v;
    g_hash_table_iter_init(&it, s->convs);
    while (g_hash_table_iter_next(&it, NULL, &v)) {
        Conv *c = v;
        if (c->type == CONV_CHANNEL) g_ptr_array_add(chs, c);
        else if (c->type == CONV_DM) g_ptr_array_add(dms, c);
    }
    g_ptr_array_sort(chs, conv_cmp);
    g_ptr_array_sort(dms, conv_cmp);
    add_section("Channels", G_CALLBACK(on_add_channel));
    for (guint i = 0; i < chs->len; i++) add_conv_row(s, g_ptr_array_index(chs, i));
    add_section("Direct messages", G_CALLBACK(on_add_dm));
    for (guint i = 0; i < dms->len; i++) add_conv_row(s, g_ptr_array_index(dms, i));
    g_ptr_array_free(chs, TRUE);
    g_ptr_array_free(dms, TRUE);
    gtk_widget_show_all(ui.conv_list);
}

static void on_profile_menu(GtkButton *b, gpointer d);

static void render_user_panel(void) {
    clear_container(ui.user_panel);
    if (!app.me) return;
    GtkWidget *ev = gtk_event_box_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 9);
    gtk_box_pack_start(GTK_BOX(box), avatar_new(app.me, 34), FALSE, FALSE, 0);
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    char *nm = name_markup(app.me, NULL, 0);
    GtkWidget *n = markup_label(nm, NULL);
    g_free(nm);
    gtk_label_set_ellipsize(GTK_LABEL(n), PANGO_ELLIPSIZE_END);
    char *at = g_strdup_printf("@%s", app.me->username);
    gtk_box_pack_start(GTK_BOX(v), n, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), label(at, "status"), FALSE, FALSE, 0);
    g_free(at);
    gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), v, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(ev), box);
    gtk_box_pack_start(GTK_BOX(ui.user_panel), ev, TRUE, TRUE, 0);
    GtkWidget *gear = gtk_button_new_with_label("⚙");
    add_class(gear, "flat-btn");
    gtk_widget_set_tooltip_text(gear, "Profile & account");
    g_signal_connect(gear, "clicked", G_CALLBACK(on_profile_menu), NULL);
    gtk_box_pack_end(GTK_BOX(ui.user_panel), gear, FALSE, FALSE, 0);
    gtk_widget_show_all(ui.user_panel);
}

/* ---------- Kopfzeile des Chats ---------- */

static void render_header(void) {
    Server *s = app.active;
    Conv *c = active_conv();
    if (!s || !c) {
        gtk_label_set_text(GTK_LABEL(ui.chat_title), "MIRC");
        gtk_label_set_text(GTK_LABEL(ui.chat_topic), "");
        gtk_widget_hide(ui.members_btn);
        return;
    }
    if (c->type == CONV_DM) {
        char *nm = name_markup(server_profile(s, c->target), c->target, 0);
        char *full = g_strdup_printf("<span foreground=\"%s\">●</span> %s", c->online ? "#3ccf7f" : "#6c6b75", nm);
        gtk_label_set_markup(GTK_LABEL(ui.chat_title), full);
        g_free(full); g_free(nm);
    } else if (c->type == CONV_CHANNEL) {
        char *esc = g_markup_escape_text(c->name + (c->name[0] == '#'), -1);
        char *m = g_strdup_printf("<span foreground=\"#9a98a6\">#</span> %s", esc);
        gtk_label_set_markup(GTK_LABEL(ui.chat_title), m);
        g_free(m); g_free(esc);
    } else {
        char *esc = g_markup_escape_text(server_title(s), -1);
        char *m = g_strdup_printf("<span foreground=\"#9a98a6\">≡</span> %s", esc);
        gtk_label_set_markup(GTK_LABEL(ui.chat_title), m);
        g_free(m); g_free(esc);
    }
    const char *topic = c->type == CONV_STATUS ? s->motd : c->topic;
    gtk_label_set_text(GTK_LABEL(ui.chat_topic), topic ? topic : "");
    gtk_widget_set_tooltip_text(ui.chat_topic, topic);
    gtk_widget_set_visible(ui.members_btn, c->type == CONV_CHANNEL);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(ui.header), server_title(s));

    if (c->type == CONV_STATUS) gtk_entry_set_placeholder_text(GTK_ENTRY(ui.entry), "Enter a command, e.g. /join #channel or /help");
    else {
        char *ph = c->type == CONV_DM ? g_strdup_printf("Message to @%s", c->target) : g_strdup_printf("Message to %s", c->name);
        gtk_entry_set_placeholder_text(GTK_ENTRY(ui.entry), ph);
        g_free(ph);
    }
}

/* ---------- Mitglieder ---------- */

static gboolean on_member_click(GtkWidget *w, GdkEventButton *ev, gpointer d) {
    (void)ev; (void)d;
    popover_profile(w, app.active, g_object_get_data(G_OBJECT(w), "user"));
    return TRUE;
}

static int role_rank(const char *r) { return !g_strcmp0(r, "owner") ? 0 : !g_strcmp0(r, "op") ? 1 : 2; }

static gint member_cmp(gconstpointer a, gconstpointer b) {
    const Member *x = *(Member *const *)a, *y = *(Member *const *)b;
    int r = role_rank(x->role) - role_rank(y->role);
    if (r) return r;
    const Profile *px = server_profile(app.active, x->username), *py = server_profile(app.active, y->username);
    char *lx = str_lower(px->display_name), *ly = str_lower(py->display_name);
    int c = g_utf8_collate(lx, ly);
    g_free(lx); g_free(ly);
    return c;
}

static void render_members(void) {
    clear_container(ui.members_list);
    Conv *c = active_conv();
    gboolean show = c && c->type == CONV_CHANNEL && ui.show_members;
    gtk_widget_set_visible(ui.members_scroll, show);
    if (!show) return;
    GPtrArray *on = g_ptr_array_new(), *off = g_ptr_array_new();
    GHashTableIter it;
    gpointer v;
    g_hash_table_iter_init(&it, c->members);
    while (g_hash_table_iter_next(&it, NULL, &v)) g_ptr_array_add(((Member *)v)->online ? on : off, v);
    g_ptr_array_sort(on, member_cmp);
    g_ptr_array_sort(off, member_cmp);
    GPtrArray *groups[2] = { on, off };
    const char *titles[2] = { "ONLINE", "OFFLINE" };
    for (int gi = 0; gi < 2; gi++) {
        if (!groups[gi]->len) continue;
        char *t = g_strdup_printf("%s — %u", titles[gi], groups[gi]->len);
        GtkWidget *hdr = label(t, "section");
        g_free(t);
        GtkWidget *hrow = gtk_list_box_row_new();
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(hrow), FALSE);
        gtk_container_add(GTK_CONTAINER(hrow), hdr);
        gtk_container_add(GTK_CONTAINER(ui.members_list), hrow);
        for (guint i = 0; i < groups[gi]->len; i++) {
            Member *m = g_ptr_array_index(groups[gi], i);
            const Profile *p = server_profile(app.active, m->username);
            GtkWidget *ev = gtk_event_box_new();
            g_object_set_data_full(G_OBJECT(ev), "user", g_strdup(m->username), g_free);
            g_signal_connect(ev, "button-press-event", G_CALLBACK(on_member_click), NULL);
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            add_class(box, "member-row");
            if (!m->online) add_class(box, "off");
            gtk_box_pack_start(GTK_BOX(box), avatar_new(p, 30), FALSE, FALSE, 0);
            char *nm = name_markup(p, m->username, 0);
            GtkWidget *l = markup_label(nm, NULL);
            g_free(nm);
            gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
            gtk_box_pack_start(GTK_BOX(box), l, TRUE, TRUE, 0);
            if (role_rank(m->role) < 2) {
                GtkWidget *r = label(!g_strcmp0(m->role, "owner") ? "♛" : "@", "role");
                gtk_widget_set_tooltip_text(r, !g_strcmp0(m->role, "owner") ? "Channel owner" : "Operator");
                gtk_box_pack_end(GTK_BOX(box), r, FALSE, FALSE, 0);
            }
            gtk_container_add(GTK_CONTAINER(ev), box);
            GtkWidget *row = gtk_list_box_row_new();
            gtk_container_add(GTK_CONTAINER(row), ev);
            gtk_container_add(GTK_CONTAINER(ui.members_list), row);
        }
    }
    g_ptr_array_free(on, TRUE);
    g_ptr_array_free(off, TRUE);
    gtk_widget_show_all(ui.members_list);
}

/* ---------- Messages ---------- */

static void start_edit(Message *m) {
    g_free(ui.editing_id);
    ui.editing_id = g_strdup(m->id);
    gtk_entry_set_text(GTK_ENTRY(ui.entry), m->text);
    gtk_editable_set_position(GTK_EDITABLE(ui.entry), -1);
    gtk_widget_grab_focus(ui.entry);
    ui.rebuild_messages = TRUE;
    ui_changed(app.active, NULL, TRUE);
}

static void cancel_edit(void) {
    g_clear_pointer(&ui.editing_id, g_free);
    gtk_entry_set_text(GTK_ENTRY(ui.entry), "");
    ui.rebuild_messages = TRUE;
    ui_changed(app.active, NULL, TRUE);
}

static Message *find_message(const char *id) {
    Conv *c = active_conv();
    for (guint i = 0; c && i < c->messages->len; i++) {
        Message *m = g_ptr_array_index(c->messages, i);
        if (!g_strcmp0(m->id, id)) return m;
    }
    return NULL;
}

static void on_msg_edit(GtkMenuItem *mi, gpointer d) { (void)d; Message *m = find_message(g_object_get_data(G_OBJECT(mi), "id")); if (m) start_edit(m); }

static void on_msg_delete(GtkMenuItem *mi, gpointer d) {
    (void)d;
    Conv *c = active_conv();
    const char *id = g_object_get_data(G_OBJECT(mi), "id");
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(ui.win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Delete message?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "This cannot be undone.");
    gtk_dialog_add_buttons(GTK_DIALOG(dlg), "Cancel", GTK_RESPONSE_CANCEL, "Delete", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT && c) {
        JsonBuilder *b = proto_begin("delete");
        proto_str(b, "target", c->target);
        proto_str(b, "edit", id);
        server_send(app.active, b);
    }
    gtk_widget_destroy(dlg);
}

static void on_msg_copy(GtkMenuItem *mi, gpointer d) {
    (void)d;
    Message *m = find_message(g_object_get_data(G_OBJECT(mi), "id"));
    if (m) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), m->text, -1);
}

static gboolean on_msg_button(GtkWidget *w, GdkEventButton *ev, gpointer d) {
    (void)d;
    if (ev->type != GDK_BUTTON_PRESS || ev->button != 3) return FALSE;
    const char *id = g_object_get_data(G_OBJECT(w), "id");
    Message *m = find_message(id);
    if (!m) return FALSE;
    Conv *c = active_conv();
    char *from = str_lower(m->from);
    gboolean own = !g_strcmp0(from, app.active->me);
    g_free(from);
    GtkWidget *menu = gtk_menu_new();
    struct { const char *label; GCallback cb; gboolean show; } items[] = {
        { "Copy text", G_CALLBACK(on_msg_copy), TRUE },
        { "Edit", G_CALLBACK(on_msg_edit), own && g_strcmp0(m->kind, "action") },
        { "Delete", G_CALLBACK(on_msg_delete), own || server_can_moderate(app.active, c) },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(items); i++) {
        if (!items[i].show) continue;
        GtkWidget *mi = gtk_menu_item_new_with_label(items[i].label);
        g_object_set_data_full(G_OBJECT(mi), "id", g_strdup(id), g_free);
        g_signal_connect(mi, "activate", items[i].cb, NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)ev);
    return TRUE;
}

static gboolean on_author_click(GtkWidget *w, GdkEventButton *ev, gpointer d) {
    (void)d;
    if (ev->button != 1) return FALSE;
    popover_profile(w, app.active, g_object_get_data(G_OBJECT(w), "user"));
    return TRUE;
}

static void on_load_more(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    Conv *c = active_conv();
    if (c) server_load_history(app.active, c);
}

static GtkWidget *clickable_author(GtkWidget *child, const char *username) {
    GtkWidget *ev = gtk_event_box_new();
    gtk_widget_set_valign(ev, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(ev), "user", g_strdup(username), g_free);
    g_signal_connect(ev, "button-press-event", G_CALLBACK(on_author_click), NULL);
    gtk_container_add(GTK_CONTAINER(ev), child);
    return ev;
}

static void add_msg_row(GtkWidget *child) {
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_container_add(GTK_CONTAINER(row), child);
    gtk_container_add(GTK_CONTAINER(ui.msg_list), row);
}

static void render_messages(void) {
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ui.msg_scroll));
    ui.keep_from_bottom = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_value(adj);
    clear_container(ui.msg_list);
    Server *s = app.active;
    Conv *c = active_conv();
    if (!s) {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        add_class(box, "intro");
        gtk_box_pack_start(GTK_BOX(box), label("Welcome to MIRC", "intro-title"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), label("Add a chat server with + on the left, e.g. localhost:6697.", "muted"), FALSE, FALSE, 0);
        add_msg_row(box);
        gtk_widget_show_all(ui.msg_list);
        return;
    }
    if (!c) return;

    if (c->has_more) {
        GtkWidget *b = gtk_button_new_with_label("Load older messages");
        add_class(b, "ghost");
        gtk_widget_set_halign(b, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_top(b, 8);
        g_signal_connect(b, "clicked", G_CALLBACK(on_load_more), NULL);
        add_msg_row(b);
    } else if (c->type != CONV_STATUS && c->history_loaded) {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        add_class(box, "intro");
        if (c->type == CONV_DM) {
            const Profile *p = server_profile(s, c->target);
            GtkWidget *av = avatar_new(p, 72);
            gtk_widget_set_halign(av, GTK_ALIGN_START);
            gtk_box_pack_start(GTK_BOX(box), av, FALSE, FALSE, 0);
            char *nm = name_markup(p, c->target, 160);
            gtk_box_pack_start(GTK_BOX(box), markup_label(nm, NULL), FALSE, FALSE, 0);
            g_free(nm);
            char *t = g_strdup_printf("This is the beginning of your conversation with @%s.", p->username);
            gtk_box_pack_start(GTK_BOX(box), label(t, "muted"), FALSE, FALSE, 0);
            g_free(t);
        } else {
            char *t = g_strdup_printf("Willkommen in %s", c->name);
            gtk_box_pack_start(GTK_BOX(box), label(t, "intro-title"), FALSE, FALSE, 0);
            g_free(t);
            gtk_box_pack_start(GTK_BOX(box), label(c->topic && *c->topic ? c->topic : "This is the beginning of this channel.", "muted"), FALSE, FALSE, 0);
        }
        add_msg_row(box);
    }

    guint start = c->messages->len > 400 ? c->messages->len - 400 : 0;
    Message *prev = NULL;
    char *last_day = NULL;
    for (guint i = start; i < c->messages->len; i++) {
        Message *m = g_ptr_array_index(c->messages, i);
        char *day = format_day(m->ts);
        if (g_strcmp0(day, last_day)) {
            add_msg_row(label(day, "day"));
            g_free(last_day);
            last_day = day;
            prev = NULL;
        } else g_free(day);

        gboolean system = !m->from || !*m->from;
        if (system) {
            char *t = format_time(m->ts, FALSE);
            char *txt = linkify_markup(m->text);
            char *full = g_strdup_printf("<span size=\"small\">%s</span>   %s%s", t,
                                         !g_strcmp0(m->kind, "join") ? "→ " : !g_strcmp0(m->kind, "part") ? "← " : "", txt);
            GtkWidget *l = markup_label(full, "sys");
            add_class(l, m->kind);
            gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
            gtk_label_set_line_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR);
            gtk_label_set_selectable(GTK_LABEL(l), TRUE);
            g_signal_connect(l, "activate-link", G_CALLBACK(on_activate_link), NULL);
            add_msg_row(l);
            g_free(t); g_free(txt); g_free(full);
            prev = NULL;
            continue;
        }

        const Profile *p = server_profile(s, m->from);
        gboolean action = !g_strcmp0(m->kind, "action");
        gboolean grouped = prev && !g_ascii_strcasecmp(prev->from, m->from) && m->ts - prev->ts < 7 * 60 * 1000 && !action && g_strcmp0(prev->kind, "action");
        gboolean mention = s->me && g_ascii_strcasecmp(m->from, s->me) && text_mentions(m->text, s->me);

        GtkWidget *ev = gtk_event_box_new();
        g_object_set_data_full(G_OBJECT(ev), "id", g_strdup(m->id), g_free);
        g_signal_connect(ev, "button-press-event", G_CALLBACK(on_msg_button), NULL);
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
        add_class(row, "msg");
        if (!grouped) add_class(row, "first");
        if (mention) add_class(row, "mention");
        if (ui.editing_id && !g_strcmp0(ui.editing_id, m->id)) add_class(row, "editing");

        if (grouped) {
            char *t = format_time(m->ts, FALSE);
            GtkWidget *tl = label(t, "time");
            g_free(t);
            gtk_widget_set_size_request(tl, 40, -1);
            gtk_label_set_xalign(GTK_LABEL(tl), 1);
            gtk_widget_set_opacity(tl, 0.5);
            gtk_widget_set_valign(tl, GTK_ALIGN_START);
            gtk_widget_set_margin_top(tl, 2);
            gtk_box_pack_start(GTK_BOX(row), tl, FALSE, FALSE, 0);
        } else {
            gtk_box_pack_start(GTK_BOX(row), clickable_author(avatar_new(p, 40), m->from), FALSE, FALSE, 0);
        }

        GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        if (!grouped) {
            GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            char *nm = name_markup(p, m->from, 0);
            gtk_box_pack_start(GTK_BOX(head), clickable_author(markup_label(nm, NULL), m->from), FALSE, FALSE, 0);
            g_free(nm);
            char *t = format_time(m->ts, TRUE);
            GtkWidget *tl = label(t, "time");
            gtk_widget_set_valign(tl, GTK_ALIGN_END);
            gtk_box_pack_start(GTK_BOX(head), tl, FALSE, FALSE, 0);
            g_free(t);
            gtk_box_pack_start(GTK_BOX(body), head, FALSE, FALSE, 0);
        }
        char *txt = linkify_markup(m->text);
        char *full;
        if (action) {
            char *nm = name_markup(p, m->from, 0);
            full = g_strdup_printf("%s <i>%s</i>", nm, txt);
            g_free(nm);
        } else if (m->edited) full = g_strdup_printf("%s <span size=\"x-small\" foreground=\"#9a98a6\">(bearbeitet)</span>", txt);
        else full = g_strdup(txt);
        GtkWidget *tl = markup_label(full, "msg-text");
        if (action) add_class(tl, "action");
        gtk_label_set_line_wrap(GTK_LABEL(tl), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(tl), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_selectable(GTK_LABEL(tl), TRUE);
        gtk_widget_set_can_focus(tl, FALSE);
        g_signal_connect(tl, "activate-link", G_CALLBACK(on_activate_link), NULL);
        g_free(txt); g_free(full);
        gtk_box_pack_start(GTK_BOX(body), tl, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), body, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(ev), row);
        add_msg_row(ev);
        prev = m;
    }
    g_free(last_day);
    gtk_widget_show_all(ui.msg_list);
}

static void on_adj_changed(GtkAdjustment *adj, gpointer d) {
    (void)d;
    double upper = gtk_adjustment_get_upper(adj), page = gtk_adjustment_get_page_size(adj);
    if (ui.stick) gtk_adjustment_set_value(adj, upper - page);
    else if (ui.keep_from_bottom > 0) gtk_adjustment_set_value(adj, upper - ui.keep_from_bottom);
}

static void on_adj_value(GtkAdjustment *adj, gpointer d) {
    (void)d;
    double v = gtk_adjustment_get_value(adj), upper = gtk_adjustment_get_upper(adj), page = gtk_adjustment_get_page_size(adj);
    ui.stick = v >= upper - page - 40;
    ui.keep_from_bottom = upper - v;
    Conv *c = active_conv();
    gint64 now = g_get_monotonic_time() / 1000;
    if (c && c->has_more && v < 30 && now - ui.last_history_req > 1000) {
        ui.last_history_req = now;
        server_load_history(app.active, c);
    }
}

/* ---------- Typing indicator ---------- */

static void render_typing(void) {
    Conv *c = active_conv();
    GString *names = g_string_new(NULL);
    int n = 0;
    gint64 now = g_get_monotonic_time() / 1000;
    if (c) {
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, c->typing);
        while (g_hash_table_iter_next(&it, &k, &v)) {
            if ((gint64)(gintptr)v < now) { g_hash_table_iter_remove(&it); continue; }
            if (n++) g_string_append(names, ", ");
            g_string_append(names, server_profile(app.active, k)->display_name);
        }
    }
    if (!n) gtk_label_set_text(GTK_LABEL(ui.typing), "");
    else {
        char *t = n > 3 ? g_strdup("Mehrere Leute schreiben …") : g_strdup_printf("%s %s …", names->str, n > 1 ? "schreiben" : "schreibt");
        gtk_label_set_text(GTK_LABEL(ui.typing), t);
        g_free(t);
    }
    g_string_free(names, TRUE);
    if (ui.editing_id) {
        gtk_label_set_text(GTK_LABEL(ui.edit_label), "Editing message · Esc to cancel · Enter to save");
        gtk_widget_show(ui.edit_label);
    } else gtk_widget_hide(ui.edit_label);
}

static gboolean typing_tick(gpointer d) { (void)d; if (ui.win) render_typing(); return G_SOURCE_CONTINUE; }

static gboolean render_all(gpointer d) {
    (void)d;
    ui.render_id = 0;
    render_rail();
    render_sidebar();
    render_header();
    render_members();
    render_typing();
    render_user_panel();
    if (ui.rebuild_messages) {
        ui.rebuild_messages = FALSE;
        render_messages();
    }
    gtk_widget_set_sensitive(ui.entry, app.active != NULL);
    return G_SOURCE_REMOVE;
}

/* ---------- Eingabe ---------- */

static void on_entry_activate(GtkEntry *e, gpointer d) {
    (void)d;
    Server *s = app.active;
    Conv *c = active_conv();
    char *text = g_strstrip(g_strdup(gtk_entry_get_text(e)));
    if (!s || !c || !*text) { g_free(text); return; }

    if (ui.editing_id) {
        JsonBuilder *b = proto_begin("edit");
        proto_str(b, "target", c->target);
        proto_str(b, "edit", ui.editing_id);
        proto_str(b, "text", text);
        server_send(s, b);
        cancel_edit();
        g_free(text);
        return;
    }
    gtk_entry_set_text(e, "");
    ui.stick = TRUE;
    if (text[0] == '/' && text[1] != '/') command_run(s, c, text);
    else if (c->type == CONV_STATUS) server_system(s, "*", "Only commands can be entered here, e.g. /join #channel – /help shows all commands.", "error");
    else if (s->state != ST_ONLINE) server_system(s, c->key, "Not connected – the message was not sent.", "error");
    else server_send_message(s, c, text[0] == '/' ? text + 1 : text, "text");
    g_free(text);
}

static gboolean on_entry_key(GtkWidget *w, GdkEventKey *ev, gpointer d) {
    (void)w; (void)d;
    if (ev->keyval == GDK_KEY_Escape && ui.editing_id) { cancel_edit(); return TRUE; }
    if (ev->keyval == GDK_KEY_Up && !*gtk_entry_get_text(GTK_ENTRY(ui.entry)) && app.active) {
        Conv *c = active_conv();
        for (gint i = c ? (gint)c->messages->len - 1 : -1; i >= 0; i--) {
            Message *m = g_ptr_array_index(c->messages, i);
            if (m->from && app.active->me && !g_ascii_strcasecmp(m->from, app.active->me) && !g_strcmp0(m->kind, "text")) { start_edit(m); return TRUE; }
        }
    }
    return FALSE;
}

static void on_entry_changed(GtkEditable *e, gpointer d) {
    (void)d;
    const char *t = gtk_entry_get_text(GTK_ENTRY(e));
    if (app.active && *t && t[0] != '/' && !ui.editing_id) server_typing(app.active, active_conv());
}

/* ---------- Menus ---------- */

static void menu_add(GtkWidget *menu, const char *labeltext, GCallback cb, gpointer data) {
    GtkWidget *mi = gtk_menu_item_new_with_label(labeltext);
    g_signal_connect(mi, "activate", cb, data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
}

static void act_open_account(GtkMenuItem *m, gpointer d) {
    (void)m; (void)d;
    char *u = g_strconcat(app.session_url, "/account", NULL);
    ui_open_url(u);
    g_free(u);
}
static void act_reload_profile(GtkMenuItem *m, gpointer d) { (void)m; (void)d; session_check_profile(); }
static void act_logout(GtkMenuItem *m, gpointer d) { (void)m; (void)d; app_logout(); }
static void act_about(GtkMenuItem *m, gpointer d) { (void)m; (void)d; dialog_about(); }

static void on_profile_menu(GtkButton *b, gpointer d) {
    (void)d;
    GtkWidget *menu = gtk_menu_new();
    menu_add(menu, "Edit profile & cosmetics (website)", G_CALLBACK(act_open_account), NULL);
    menu_add(menu, "Reload profile", G_CALLBACK(act_reload_profile), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    menu_add(menu, "About MIRC", G_CALLBACK(act_about), NULL);
    menu_add(menu, "Sign out", G_CALLBACK(act_logout), NULL);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(b), GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST, NULL);
}

static void act_toggle_conn(GtkMenuItem *m, gpointer d) {
    (void)m; (void)d;
    Server *s = app.active;
    if (!s) return;
    if (s->state == ST_ONLINE || s->state == ST_CONNECTING) server_disconnect(s, "Disconnected");
    else { s->retry = 0; server_connect(s); }
}
static void act_list(GtkMenuItem *m, gpointer d) { (void)m; (void)d; if (app.active && app.active->state == ST_ONLINE) server_send(app.active, proto_begin("list")); }
static void act_join(GtkMenuItem *m, gpointer d) { (void)m; (void)d; if (app.active) dialog_join(app.active); }
static void act_edit_server(GtkMenuItem *m, gpointer d) { (void)m; (void)d; if (app.active) dialog_add_server(&app.active->cfg); }

static void act_remove_server(GtkMenuItem *m, gpointer d) {
    (void)m; (void)d;
    Server *s = app.active;
    if (!s) return;
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(ui.win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Remove server »%s«?", server_title(s));
    gtk_dialog_add_buttons(GTK_DIALOG(dlg), "Cancel", GTK_RESPONSE_CANCEL, "Remove", GTK_RESPONSE_ACCEPT, NULL);
    int r = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (r != GTK_RESPONSE_ACCEPT) return;
    server_disconnect(s, "Removed");
    s->removed = TRUE;
    g_ptr_array_remove(app.servers, s);
    config_save_servers();
    app.active = app.servers->len ? g_ptr_array_index(app.servers, 0) : NULL;
    g_free(app.active_key);
    app.active_key = g_strdup("*");
    server_unref(s);
    ui.rebuild_messages = TRUE;
    ui_changed(NULL, NULL, TRUE);
}

static void on_server_menu(GtkButton *b, gpointer d) {
    (void)d;
    Server *s = app.active;
    if (!s) return;
    GtkWidget *menu = gtk_menu_new();
    menu_add(menu, s->state == ST_ONLINE || s->state == ST_CONNECTING ? "Disconnect" : "Connect", G_CALLBACK(act_toggle_conn), NULL);
    menu_add(menu, "Join / create channel …", G_CALLBACK(act_join), NULL);
    menu_add(menu, "Browse channels …", G_CALLBACK(act_list), NULL);
    menu_add(menu, "Edit server …", G_CALLBACK(act_edit_server), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    menu_add(menu, "Remove server", G_CALLBACK(act_remove_server), NULL);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(b), GDK_GRAVITY_SOUTH_EAST, GDK_GRAVITY_NORTH_EAST, NULL);
}

static void on_members_toggle(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    ui.show_members = !ui.show_members;
    ui_changed(NULL, NULL, FALSE);
}

static gboolean on_focus_in(GtkWidget *w, GdkEvent *e, gpointer d) {
    (void)w; (void)e; (void)d;
    static gint64 last = 0;
    gint64 now = g_get_monotonic_time();
    if (now - last > 5 * G_USEC_PER_SEC) { last = now; session_check_profile(); } /* after changes on the website */
    return FALSE;
}

/* ---------- Construction ---------- */

static GtkWidget *scrolled(GtkWidget *child, GtkPolicyType h) {
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), h, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), child);
    return sw;
}

void ui_build(void) {
    if (ui.win) return;
    last_conv_per_server = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    ui.show_members = TRUE;
    ui.stick = TRUE;
    ui_apply_theme();

    ui.win = gtk_application_window_new(app.gapp);
    gtk_window_set_default_size(GTK_WINDOW(ui.win), 1240, 780);
    gtk_window_set_icon_name(GTK_WINDOW(ui.win), "mirc");
    g_signal_connect(ui.win, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
    ui.header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(ui.header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(ui.header), "MIRC");
    gtk_window_set_titlebar(GTK_WINDOW(ui.win), ui.header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(root, "main");

    /* Serverleiste */
    ui.rail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *rail_sw = scrolled(ui.rail, GTK_POLICY_NEVER);
    add_class(rail_sw, "rail");
    gtk_widget_set_size_request(rail_sw, 72, -1);
    gtk_box_pack_start(GTK_BOX(root), rail_sw, FALSE, FALSE, 0);

    /* Seitenleiste */
    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(side, "sidebar");
    gtk_widget_set_size_request(side, 250, -1);
    GtkWidget *shead = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add_class(shead, "server-head");
    GtkWidget *stitles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    ui.sidebar_title = label("", "server-title");
    gtk_label_set_ellipsize(GTK_LABEL(ui.sidebar_title), PANGO_ELLIPSIZE_END);
    ui.sidebar_status = label("", "status");
    gtk_label_set_ellipsize(GTK_LABEL(ui.sidebar_status), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(ui.sidebar_status), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(ui.sidebar_title), 1);
    gtk_box_pack_start(GTK_BOX(stitles), ui.sidebar_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stitles), ui.sidebar_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(shead), stitles, TRUE, TRUE, 0);
    ui.server_menu_btn = gtk_button_new_with_label("⋯");
    add_class(ui.server_menu_btn, "flat-btn");
    g_signal_connect(ui.server_menu_btn, "clicked", G_CALLBACK(on_server_menu), NULL);
    gtk_box_pack_end(GTK_BOX(shead), ui.server_menu_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(side), shead, FALSE, FALSE, 0);
    ui.conv_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.conv_list), GTK_SELECTION_NONE);
    g_signal_connect(ui.conv_list, "row-activated", G_CALLBACK(on_conv_row_activated), NULL);
    gtk_box_pack_start(GTK_BOX(side), scrolled(ui.conv_list, GTK_POLICY_NEVER), TRUE, TRUE, 0);
    ui.user_panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add_class(ui.user_panel, "user-panel");
    gtk_box_pack_end(GTK_BOX(side), ui.user_panel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), side, FALSE, FALSE, 0);

    /* Chat */
    GtkWidget *chat = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *chead = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(chead, "chat-head");
    ui.chat_title = label("MIRC", "chat-title");
    ui.chat_topic = label("", "topic");
    gtk_label_set_ellipsize(GTK_LABEL(ui.chat_topic), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(chead), ui.chat_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(chead), ui.chat_topic, TRUE, TRUE, 0);
    ui.members_btn = gtk_button_new_with_label("Mitglieder");
    add_class(ui.members_btn, "flat-btn");
    g_signal_connect(ui.members_btn, "clicked", G_CALLBACK(on_members_toggle), NULL);
    gtk_box_pack_end(GTK_BOX(chead), ui.members_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(chat), chead, FALSE, FALSE, 0);

    ui.msg_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.msg_list), GTK_SELECTION_NONE);
    add_class(ui.msg_list, "messages");
    ui.msg_scroll = scrolled(ui.msg_list, GTK_POLICY_NEVER);
    add_class(ui.msg_scroll, "messages");
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ui.msg_scroll));
    g_signal_connect(adj, "changed", G_CALLBACK(on_adj_changed), NULL);
    g_signal_connect(adj, "value-changed", G_CALLBACK(on_adj_value), NULL);
    gtk_box_pack_start(GTK_BOX(chat), ui.msg_scroll, TRUE, TRUE, 0);

    ui.edit_label = label("", "edit-banner");
    gtk_box_pack_start(GTK_BOX(chat), ui.edit_label, FALSE, FALSE, 0);
    GtkWidget *composer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(composer, "composer");
    ui.entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(ui.entry), 2000);
    g_signal_connect(ui.entry, "activate", G_CALLBACK(on_entry_activate), NULL);
    g_signal_connect(ui.entry, "key-press-event", G_CALLBACK(on_entry_key), NULL);
    g_signal_connect(ui.entry, "changed", G_CALLBACK(on_entry_changed), NULL);
    gtk_box_pack_start(GTK_BOX(composer), ui.entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(chat), composer, FALSE, FALSE, 0);
    ui.typing = label("", "typing");
    gtk_box_pack_start(GTK_BOX(chat), ui.typing, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), chat, TRUE, TRUE, 0);

    /* Mitglieder */
    ui.members_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.members_list), GTK_SELECTION_NONE);
    ui.members_scroll = scrolled(ui.members_list, GTK_POLICY_NEVER);
    add_class(ui.members_scroll, "members");
    gtk_widget_set_size_request(ui.members_scroll, 230, -1);
    gtk_box_pack_start(GTK_BOX(root), ui.members_scroll, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(ui.win), root);
    g_timeout_add_seconds(1, typing_tick, NULL);
}

void ui_show_main(void) {
    ui_build();
    gtk_widget_show_all(ui.win);
    gtk_widget_hide(ui.edit_label);
    ui.rebuild_messages = TRUE;
    render_all(NULL);
    gtk_window_present(GTK_WINDOW(ui.win));
}
