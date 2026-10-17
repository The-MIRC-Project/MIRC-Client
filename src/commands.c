/* Slash-Befehle im Eingabefeld. */
#include "mirc.h"
#include <string.h>

static void sys(Server *s, Conv *c, const char *text, const char *kind) { server_system(s, c ? c->key : "*", text, kind); }

static void send_simple(Server *s, const char *op, const char *k1, const char *v1, const char *k2, const char *v2, const char *k3, const char *v3) {
    JsonBuilder *b = proto_begin(op);
    if (k1) proto_str(b, k1, v1);
    if (k2) proto_str(b, k2, v2);
    if (k3) proto_str(b, k3, v3);
    server_send(s, b);
}

static void help(Server *s, Conv *c) {
    static const char *lines[] = {
        "/join #channel – Join or create channel",
        "/part [reason] – leave the current channel",
        "/msg user [text] – direct message",
        "/me text – action",
        "/topic text – set topic (operator)",
        "/kick user [reason] · /ban user [reason] · /unban user",
        "/op user · /deop user (channel owner)",
        "/delchannel – delete channel (owner)",
        "/whois nutzer · /list · /clear · /reconnect",
        "↑ in an empty input – edit your last message · //text sends text beginning with /",
        NULL,
    };
    for (int i = 0; lines[i]; i++) sys(s, c, lines[i], "system");
}

gboolean command_run(Server *s, Conv *c, const char *line) {
    const char *p = line + 1;
    const char *sp = strchr(p, ' ');
    char *cmd = g_ascii_strdown(p, sp ? sp - p : -1);
    char *rest = g_strstrip(g_strdup(sp ? sp + 1 : ""));
    char *arg = NULL, *after = NULL;
    if (*rest) {
        char *s2 = strchr(rest, ' ');
        arg = g_strndup(rest, s2 ? (gsize)(s2 - rest) : strlen(rest));
        after = g_strstrip(g_strdup(s2 ? s2 + 1 : ""));
    } else after = g_strdup("");
    const char *chan = c && c->type == CONV_CHANNEL ? c->target : NULL;
    gboolean ok = TRUE;

#define NEED(cond, msg) if (!(cond)) { sys(s, c, msg, "error"); goto done; }
    if (!strcmp(cmd, "help")) { help(s, c); goto done; }
    if (!strcmp(cmd, "clear")) { if (c) g_ptr_array_set_size(c->messages, 0); ui_changed(s, c ? c->key : NULL, TRUE); goto done; }
    if (!strcmp(cmd, "reconnect")) { s->retry = 0; server_connect(s); goto done; }
    NEED(s->state == ST_ONLINE, "Not connected.");

    if (!strcmp(cmd, "join") || !strcmp(cmd, "j")) {
        NEED(arg, "Usage: /join #channel");
        char *ch = arg[0] == '#' ? g_strdup(arg) : g_strconcat("#", arg, NULL);
        char *key = str_lower(ch);
        ui_switch_when_joined(s, key);
        send_simple(s, "join", "channel", ch, NULL, NULL, NULL, NULL);
        g_free(ch); g_free(key);
    } else if (!strcmp(cmd, "part") || !strcmp(cmd, "leave")) {
        NEED(chan, "Only available in channels.");
        send_simple(s, "part", "channel", chan, "reason", rest, NULL, NULL);
    } else if (!strcmp(cmd, "me")) {
        NEED(c && c->type != CONV_STATUS && *rest, "Usage: /me text (in a channel or DM)");
        server_send_message(s, c, rest, "action");
    } else if (!strcmp(cmd, "msg") || !strcmp(cmd, "query") || !strcmp(cmd, "dm")) {
        NEED(arg, "Usage: /msg user [text]");
        char *low = str_lower(arg[0] == '@' ? arg + 1 : arg);
        char *key = g_strconcat("@", low, NULL);
        if (*after) send_simple(s, "msg", "target", low, "text", after, "kind", "text");
        else send_simple(s, "open_dm", "username", low, NULL, NULL, NULL, NULL);
        ui_switch_when_joined(s, key);
        g_free(low); g_free(key);
    } else if (!strcmp(cmd, "topic")) {
        NEED(chan, "Only available in channels.");
        send_simple(s, "topic", "channel", chan, "topic", rest, NULL, NULL);
    } else if (!strcmp(cmd, "kick") || !strcmp(cmd, "ban")) {
        NEED(chan && arg, "Usage: /kick user [reason] (in a channel)");
        send_simple(s, cmd, "channel", chan, "username", arg, "reason", after);
    } else if (!strcmp(cmd, "unban") || !strcmp(cmd, "op") || !strcmp(cmd, "deop")) {
        NEED(chan && arg, "Usage: /op user (in a channel)");
        send_simple(s, cmd, "channel", chan, "username", arg, NULL, NULL);
        if (!strcmp(cmd, "unban")) { char *t = g_strdup_printf("%s was unbanned", arg); sys(s, c, t, "system"); g_free(t); }
    } else if (!strcmp(cmd, "delchannel")) {
        NEED(chan, "Only available in channels.");
        send_simple(s, "delete_channel", "channel", chan, NULL, NULL, NULL, NULL);
    } else if (!strcmp(cmd, "whois")) {
        NEED(arg, "Usage: /whois user");
        send_simple(s, "whois", "username", arg, NULL, NULL, NULL, NULL);
    } else if (!strcmp(cmd, "list")) {
        server_send(s, proto_begin("list"));
    } else {
        char *t = g_strdup_printf("Unknown command: /%s – /help shows all commands", cmd);
        sys(s, c, t, "error");
        g_free(t);
        ok = FALSE;
    }
#undef NEED
done:
    g_free(cmd); g_free(rest); g_free(arg); g_free(after);
    return ok;
}
