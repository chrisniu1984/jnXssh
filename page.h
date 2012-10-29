#ifndef __PAGE_H__
#define __PAGE_H__

//#include <glib-object.h>
//#include <glib/gthread.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSH_PASSWORD "password: "

typedef struct {
    char    name[256];
    char    str[256];
} cmd_t;

typedef struct {
    char    name[256];
    cmd_t   cmd[CMD_MAX_COUNT];
} btn_t;

typedef struct {
    char    name[256];
    char    host[256];
    char    port[256];
    char    user[256];
    char    pass[256];

    btn_t   btn[BTN_MAX_COUNT];
} cfg_t;

typedef struct {
    enum {
        PG_TYPE_HUB,
        PG_TYPE_SSH,
        PG_TYPE_SHELL,
    } type;

    // head
    struct {
        GtkWidget   *box;
        GtkWidget   *image;
        GtkWidget   *label;
        GtkWidget   *button;
    } head;

    // body
    GtkWidget   *body;

    union {
        struct {
        } hub;

        struct {
            GtkWidget *vte;
            VtePty  *pty;
            pid_t   child;
            int     need_stop;
            cfg_t   cfg;
        } ssh;

        struct {
            GtkWidget *vte;
            VtePty  *pty;
            pid_t   child;
        } shell;
    };

} pg_t;

int page_init(GtkWidget *widget);
int page_term();

GtkWidget *page_get_notebook();

gint page_create_show(cfg_t *cfg);
gint page_shell_create();

int page_get_count();

int page_get_select_num();
void page_set_select_num(int i);

int page_close(int n);
int page_close_select();

void page_set_auto_focus(int b);

int page_foreach_send_char(char c);
int page_foreach_send_string(char *str);
int page_foreach_close();

int page_send_string(int i, char *str);
int page_send_string_crlf(int i, char *str);

#ifdef __cplusplus
};
#endif

#endif // __PAGE_H__
