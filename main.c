#define _XOPEN_SOURCE
#include <signal.h>
#include <fcntl.h>

#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "config.h"
#include "util.h"

#include "page.h"
#include "site.h"

GtkWidget *m_window;
GtkWidget *m_allssh;
GtkWidget *m_command;

static void on_combo_changed(GtkWidget *widget, gpointer user_data)
{
    gtk_widget_hide(m_allssh);
    gtk_widget_hide(m_command);

    switch(gtk_combo_box_get_active(GTK_COMBO_BOX(widget))) {
    case 0:
        gtk_widget_show(m_allssh);
        break;
    case 1:
        gtk_widget_show(m_command);
        break;
    }
}

static gboolean on_allssh_key_press(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    GdkEventKey *key = (GdkEventKey*) event;
    char *cmd = (char*) gtk_entry_get_text(GTK_ENTRY(m_allssh));
    int len = strlen(cmd);

    if (key->state & GDK_CONTROL_MASK) {
        // 按下Ctrl+(a-z)时，发送控制字符
        if (key->keyval >= GDK_KEY_a && key->keyval <= GDK_KEY_z) {
            page_foreach_send_char(key->keyval-'a'+1);
            return TRUE;
        }

        // 按下Ctrl+(A-Z)时，发送控制字符
        if (key->keyval >= GDK_KEY_A && key->keyval <= GDK_KEY_Z) {
            page_foreach_send_char(key->keyval-'A'+1);
            return TRUE;
        }

        // 按下Ctrl+Enter时，发送无'\n'结尾的字符串
        if (key->keyval == GDK_KEY_Return){
            page_foreach_send_string(cmd);
            gtk_entry_set_text(GTK_ENTRY(m_allssh), "");
            return TRUE;
        }
    }
    // 按下Enter时发送带'\n'结尾的字符串
    else if (key->keyval == GDK_KEY_Return) {
        cmd[len] = '\n';
        cmd[len+1] = '\0';
        page_foreach_send_string(cmd);
        gtk_entry_set_text(GTK_ENTRY(m_allssh), "");
        return TRUE;
    }

    return FALSE;
}

static gboolean on_command_key_press(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    GdkEventKey *key = (GdkEventKey*) event;

    if (key->keyval == GDK_KEY_Return) {
        //char *cmd = (char*) gtk_entry_get_text(GTK_ENTRY(m_allssh));
        //int len = strlen(cmd);

        // FIXME: 完成命令解析

        gtk_entry_set_text(GTK_ENTRY(m_command), "");

        return TRUE;
    }

    return FALSE;
}

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

            // 当焦点在m_allssh上时，为防止标签切换时自动把焦点移到当前page上，
            // 先关闭自动获取焦点。
            if (gtk_widget_is_focus(m_allssh)) {
                page_set_auto_focus(0);
            }
            page_set_select_num(num);
            page_set_auto_focus(1);
            return TRUE;
        }
        // 按下Ctrl+Left时，向左移动
        if (key->keyval == GDK_KEY_Left ||
            key->keyval == GDK_KEY_Page_Up) {
            int num = page_get_select_num();
            num--;

            // 当焦点在m_allssh上时，为防止标签切换时自动把焦点移到当前page上，
            // 先关闭自动获取焦点。
            if (gtk_widget_is_focus(m_allssh)) {
                page_set_auto_focus(0);
            }
            page_set_select_num(num);
            page_set_auto_focus(1);
            return TRUE;
        }
    }

    return FALSE;
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
    //g_signal_connect(G_OBJECT(m_window), "destroy", G_CALLBACK (on_window_destory), NULL);
    g_signal_connect(G_OBJECT(m_window), "destroy", G_CALLBACK (gtk_main_quit), NULL);
    
        // vbox
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(m_window), vbox);

            // notebook
            GtkWidget *notebook = page_get_notebook();
            gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

            // hbox
            GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

                // liststore
                GtkTreeIter iter;
                GtkListStore *list = gtk_list_store_new(1, G_TYPE_STRING);
                gtk_list_store_append(list, &iter);
                gtk_list_store_set(list, &iter, 0, "All SSHs", -1);
                gtk_list_store_append(list, &iter);
                gtk_list_store_set(list, &iter, 0, "Command", -1);
                
                // combo
                GtkWidget *combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(list));
                GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
                gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, FALSE);
                gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), renderer, "text", 0, NULL);    
                g_signal_connect(G_OBJECT(combo), "changed" , G_CALLBACK(on_combo_changed), NULL);
                gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 10);

                // style
                GdkRGBA rgba;
                PangoFontDescription *desc = pango_font_description_new();
                pango_font_description_set_family(desc, "WenQuanYi Micro Hei Mono, Microsoft YaHei");
                pango_font_description_set_size(desc, 14*PANGO_SCALE);

                // all_ssh
                m_allssh = gtk_entry_new();
                gdk_rgba_parse(&rgba, "rgb(255,0,0)");
                gtk_widget_override_color(m_allssh, GTK_STATE_FLAG_FOCUSED, &rgba);
                gtk_widget_override_font(m_allssh, desc);
                g_signal_connect(G_OBJECT(m_allssh), "key-press-event", G_CALLBACK(on_allssh_key_press), NULL);
                gtk_box_pack_start(GTK_BOX(hbox), m_allssh, TRUE, TRUE, 5);

                // command
                m_command = gtk_entry_new();
                gdk_rgba_parse(&rgba, "rgb(0,0,255)");
                gtk_widget_override_color(m_command, GTK_STATE_FLAG_FOCUSED, &rgba);
                gtk_widget_override_font(m_command, desc);
                g_signal_connect(G_OBJECT(m_command), "key-press-event", G_CALLBACK(on_command_key_press), NULL);
                gtk_box_pack_start(GTK_BOX(hbox), m_command, TRUE, TRUE, 5);


    //gtk_widget_grab_focus(notebook);
    gtk_widget_show_all(m_window);

    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

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
    page_init(site_get_object());

    // 创建主窗口
    window_create_show();

    gtk_main();

    // 回收资源
    site_term();

    return 0;
}
