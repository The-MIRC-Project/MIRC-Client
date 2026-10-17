/* MIRC client – shared types and functions.
 * GTK 3 + libsoup 3 + json-glib, networking over GIO (TCP/TLS). */
#ifndef MIRC_H
#define MIRC_H

#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#define MIRC_VERSION "0.1.0"
#define MIRC_DEFAULT_SESSION "http://localhost:8440"
#define MIRC_DEFAULT_PORT_TLS 6697
#define MIRC_DEFAULT_PORT_PLAIN 6680

/* ---------- Data model ---------- */

typedef struct {
    char *username;
    char *display_name;
    char *bio;
    char *name_color;      /* "#rrggbb" oder NULL */
    char *grad[4];         /* Farbverlauf */
    int   grad_n;
    char *avatar_url;
    char *banner_url;
    char *banner_color;
    char *accent;
    char *background;
    gint64 created;
} Profile;

typedef struct {
    char *id;
    char *target;
    char *from;            /* NULL/"" = Systemmeldung */
    char *text;
    char *kind;            /* text | action | system | join | part | error | motd | topic */
    gint64 ts;             /* ms */
    gint64 edited;
} Message;

typedef enum { CONV_STATUS, CONV_CHANNEL, CONV_DM } ConvType;

typedef struct {
    char *username;
    char *role;            /* owner | op | member */
    gboolean online;
} Member;

typedef struct {
    ConvType type;
    char *key;             /* "*" | "#chan" (klein) | "@nutzer" (klein) */
    char *name;            /* Display name */
    char *target;          /* Protocol target */
    char *topic;
    char *owner;
    GHashTable *members;   /* username(klein) -> Member* */
    GPtrArray *messages;   /* Message* */
    GHashTable *typing;    /* username(klein) -> Ablaufzeit (gint64 ms) */
    int unread, mentions;
    gboolean history_loaded, has_more, online;
    gint64 last_ts;
} Conv;

typedef enum { ST_OFFLINE, ST_CONNECTING, ST_ONLINE, ST_ERROR } ServerState;

typedef struct {
    char *id;
    char *name;            /* optional, vom Nutzer */
    char *host;
    int   port;
    gboolean tls;
} ServerCfg;

typedef struct Server Server;
struct Server {
    int refs;
    ServerCfg cfg;
    ServerState state;
    char *status_text;
    char *server_name;
    char *motd;

    GSocketClient *client;
    GSocketConnection *conn;
    GDataInputStream *in;
    GOutputStream *out;
    GCancellable *cancel;
    GQueue *outq;          /* GBytes* */
    GBytes *writing;

    char *pending_fp;      /* certificate not yet trusted */
    gboolean pending_mismatch;
    gboolean manual_close, removed;
    guint reconnect_id;
    int retry;

    GHashTable *convs;     /* key -> Conv* */
    GHashTable *users;     /* username(klein) -> Profile* */
    char *me;              /* own username (lowercase) */
    gboolean is_admin;
    gint64 last_typing;
};

/* ---------- Global state ---------- */

typedef struct {
    GtkApplication *gapp;
    SoupSession *http;
    GKeyFile *config;
    char *config_path;

    char *session_url;
    char *token;
    Profile *me;
    char *me_json;         /* used to detect profile changes */

    GPtrArray *servers;    /* Server* */
    Server *active;
    char *active_key;

    GHashTable *images;    /* url -> GdkPixbuf* (NULL while downloading) */
} App;

extern App app;

/* util.c */
Profile *profile_from_json(JsonObject *o);
Profile *profile_dup(const Profile *p);
void     profile_free(Profile *p);
Profile *profile_fallback(const char *username);
char    *name_markup(const Profile *p, const char *fallback, int size_pct);
char    *linkify_markup(const char *text);
char    *format_time(gint64 ms, gboolean with_day);
char    *format_day(gint64 ms);
gboolean parse_color(const char *hex, double *r, double *g, double *b);
void     color_for_name(const char *name, double *r, double *g, double *b);
const char *jstr(JsonObject *o, const char *key);
gint64   jint(JsonObject *o, const char *key);
gboolean jbool(JsonObject *o, const char *key);
char    *json_to_string_obj(JsonObject *o);
Message *message_from_json(JsonObject *o);
Message *message_new_system(const char *text, const char *kind);
void     message_free(Message *m);
char    *str_lower(const char *s);
gboolean text_mentions(const char *text, const char *username);

/* config.c */
void config_load(void);
void config_save(void);
void config_set_session(const char *url, const char *username, const char *token);
void config_save_servers(void);
GPtrArray *config_load_servers(void);
char *config_get_pin(const char *hostport);
void  config_set_pin(const char *hostport, const char *fp);
char *config_get_string(const char *group, const char *key);
void  config_set_string(const char *group, const char *key, const char *value);

/* session.c */
typedef void (*HttpCallback)(guint status, JsonNode *root, const char *error, gpointer data);
void http_json(const char *method, const char *path, const char *body_json, gboolean auth, HttpCallback cb, gpointer data);
void session_login(const char *url, const char *user, const char *password, HttpCallback cb, gpointer data);
void session_fetch_me(HttpCallback cb, gpointer data);
void session_server_token(const char *hostport, HttpCallback cb, gpointer data);
GdkPixbuf *image_get(const char *url);   /* loads asynchronously, NULL while unknown */
void session_start_profile_watch(void);
void session_check_profile(void);

/* conn.c */
Server *server_new(const ServerCfg *cfg);
Server *server_ref(Server *s);
void    server_unref(Server *s);
void    server_connect(Server *s);
void    server_disconnect(Server *s, const char *why);
void    server_send(Server *s, JsonBuilder *b);       /* takes ownership of b */
JsonBuilder *proto_begin(const char *op);              /* starts an object with "op" */
void    proto_str(JsonBuilder *b, const char *key, const char *val);
void    proto_int(JsonBuilder *b, const char *key, gint64 val);
Conv   *server_conv(Server *s, const char *key);
Conv   *server_status_conv(Server *s);
Conv   *server_ensure_dm(Server *s, const char *username);
const Profile *server_profile(Server *s, const char *username);
void    server_system(Server *s, const char *key, const char *text, const char *kind);
void    server_load_history(Server *s, Conv *c);
void    server_send_message(Server *s, Conv *c, const char *text, const char *kind);
void    server_typing(Server *s, Conv *c);
gboolean server_can_moderate(Server *s, Conv *c);
const char *server_title(Server *s);
void    servers_refresh_profile(void);

/* ui.c */
void ui_build(void);
void ui_show_main(void);
void ui_changed(Server *s, const char *conv_key, gboolean messages);  /* request redraw */
void ui_select(Server *s, const char *key);
void ui_new_message(Server *s, Conv *c, Message *m, gboolean mention);
void ui_conv_removed(Server *s, const char *key);
void ui_switch_when_joined(Server *s, const char *key);
void ui_conv_ready(Server *s, const char *key);
void ui_apply_theme(void);
GtkWindow *ui_window(void);
void ui_toast(const char *text);
void ui_open_url(const char *url);

/* dialogs.c */
void dialog_login(void);
void dialog_add_server(ServerCfg *edit);
void dialog_trust_cert(Server *s);
void dialog_join(Server *s);
void dialog_new_dm(Server *s);
void dialog_channel_list(Server *s, JsonArray *channels);
void popover_profile(GtkWidget *relative, Server *s, const char *username);
void dialog_about(void);

/* commands.c */
gboolean command_run(Server *s, Conv *c, const char *line);

/* avatar.c */
GtkWidget *avatar_new(const Profile *p, int size);

void app_logout(void);
void app_start_session(void);

#endif
