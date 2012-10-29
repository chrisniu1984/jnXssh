#define _XOPEN_SOURCE
#define _GNU_SOURCE
#include <signal.h>
#include <fcntl.h>

#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "config.h"
#include "util.h"

#include "debug.h"
#include "page.h"
#include "site.h"

GtkWidget *m_window;

static gboolean on_window_key_press(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    GdkEventKey *key = (GdkEventKey*) event;

    if ((key->state & GDK_CONTROL_MASK) &&
        (key->state & GDK_SHIFT_MASK)) {

        // 关闭当前窗口
        if (key->keyval == GDK_KEY_W) {
            page_close_select();
            return TRUE;
        }

        // 打开一个本地窗口
        if (key->keyval == GDK_KEY_T) {
            page_shell_create();
            return TRUE;
        }
    }
    else if (key->state & GDK_CONTROL_MASK) {

        // 按下Ctrl+Right时，向右移动
        if (key->keyval == GDK_KEY_Right ||
            key->keyval == GDK_KEY_Page_Down) {
            int num = page_get_select_num();
            int count = page_get_count();
            num++;
            if (num >= count) {
                num = 0;
            }

            page_set_select_num(num);
            return TRUE;
        }
        // 按下Ctrl+Left时，向左移动
        if (key->keyval == GDK_KEY_Left ||
            key->keyval == GDK_KEY_Page_Up) {
            int num = page_get_select_num();
            num--;

            page_set_select_num(num);
            return TRUE;
        }
    }

    return FALSE;
}

static void *proc_allvte(void *p)
{
    int master = vte_pty_get_fd((VtePty*)p);
    int slave = open(ptsname(master), O_RDWR);

    // raw 模式
    struct termios tio;
    cfmakeraw(&tio);
    tcsetattr(slave, TCSADRAIN, &tio);

    fd_set set;
    while (1) {
        FD_ZERO(&set);
        FD_SET(slave, &set);
        struct timeval tv = {0, 100};

        if (select(slave+1, &set, NULL, NULL, &tv) > 0) {
            char buf[256];
            int len = read(slave, buf, sizeof(buf));
            if (len > 0) {
                buf[len] = '\0';
                page_foreach_send_string(buf);
            }
        }

        usleep(1000);
    }

    return NULL;
}


static int window_create_show()
{
    char *tmp;

    // window
    m_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    tmp = get_res_path(ICON_APP);
    gtk_window_set_icon_from_file(GTK_WINDOW(m_window), tmp, NULL);
    free(tmp);
    gtk_window_set_title(GTK_WINDOW(m_window), "jnXssh");
    gtk_window_maximize(GTK_WINDOW(m_window));
    gtk_widget_set_events(m_window, GDK_BUTTON_PRESS_MASK|GDK_KEY_PRESS_MASK);
    g_signal_connect(G_OBJECT(m_window), "key-press-event", G_CALLBACK(on_window_key_press), NULL);
    g_signal_connect(G_OBJECT(m_window), "destroy", G_CALLBACK (gtk_main_quit), NULL);
    
        // vbox
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(m_window), vbox);

            // notebook
            GtkWidget *notebook = page_get_notebook();
            gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

            // pty + vte
            GtkWidget *vte = vte_terminal_new();
            gtk_box_pack_start(GTK_BOX(vbox), vte, FALSE, FALSE, 1);
            vte_terminal_set_size((VteTerminal*)vte, 1, 1);
            VtePty *pty = vte_pty_new(VTE_PTY_DEFAULT, NULL); 
            vte_terminal_set_pty_object((VteTerminal*)vte, pty);
            pthread_t tid;
            pthread_create(&tid, NULL, proc_allvte, pty);

    gtk_widget_show_all(m_window);

    return 0;
}

const char *HOME = NULL;
char PATH[256] = {0x00};
int init()
{
    // home
    HOME = getenv("HOME");
    if (HOME == NULL) {
        return -1;
    }

    // path
    memset(PATH, 0x00, sizeof(PATH));
    sprintf(PATH, "%s/%s", HOME, CONFIG_DIR);

    // mkdir PATH if it is not exits.
    char cmd[256];
    memset(cmd, 0x00, sizeof(cmd));
    sprintf(cmd, "mkdir -p %s", PATH);
    system(cmd);

    return 0;
}

int main(int argc, char **argv)
{
    if (init() != 0) {
        return -1;
    }

    g_thread_init(NULL);
    gdk_threads_init();

    // 初始化
    gtk_init(&argc, &argv);

    // 创建site页
    site_init();

    // 装载site配置
    site_load();

    // 创建tab容（使用site页作为hub_page)
    GtkWidget *hub = site_get_object();
    page_init(hub);
    gtk_widget_grab_focus(hub);

    // 创建主窗口
    window_create_show();

    // 创建DEBUG_WINDOW
    //debug_create_show(m_window);

    gtk_main();

    // 回收资源
    site_term();

    return 0;
}
