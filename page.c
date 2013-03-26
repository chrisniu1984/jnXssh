#define _XOPEN_SOURCE
#define _GNU_SOURCE

#include <fcntl.h>
#include <pthread.h>

#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "util.h"

#include "page.h"

#define SSH     "/usr/bin/ssh"
#define SHELL   "/bin/bash"

static GtkWidget *m_notebook;
static GtkWidget *m_menu;
static GtkWidget *m_menu_copy;
static GtkWidget *m_menu_paste;
static GtkWidget *m_menu_copy_paste;

static int m_auto_focus = 1;

static void *wait_ssh_child(void *p)
{
    pg_t *pg = (pg_t*) p;
    if (pg->type == PG_TYPE_SSH) {
        waitpid(pg->ssh.child, NULL, 0);
        pg->ssh.need_stop = 1;
    }

    return NULL;
}

static void run_shell(pg_t *pg)
{
    if (NULL == pg || pg->type != PG_TYPE_SHELL) {
        return;
    }

    pg->shell.child = fork();
    if (pg->shell.child == 0) {
        vte_pty_child_setup(pg->shell.pty);
        execlp(SHELL, SHELL, NULL);
    }
    waitpid(pg->shell.child, NULL, 0);

    vte_pty_close(pg->shell.pty);
}

static void run_ssh(pg_t *pg)
{
    if (NULL == pg || pg->type != PG_TYPE_SSH) {
        return;
    }

    /* 
     * [output flow-chart]
     * vte << vte_master_fd << vte_slave_fd << this_app << mine_master_fd << mine_slave_fd << ssh
     * 
     * [intput flow-chart]
     * vte >> vte_master_fd >> vte_slave_fd >> this_app >> mine_master_fd >> mine_slave_fd >> ssh
     */
    int vte_master_fd = vte_pty_get_fd(pg->ssh.pty);
    int vte_slave_fd = open(ptsname(vte_master_fd), O_RDWR);

    // raw 模式
    struct termios tio;
    tcgetattr(vte_slave_fd, &tio);
    cfmakeraw(&tio);
    tcsetattr(vte_slave_fd, TCSADRAIN, &tio);

    // open mine_master_fd
    int mine_master_fd = getpt();
    if (mine_master_fd < 0) {
        return;
    }
    int flags = fcntl(mine_master_fd, F_GETFL, 0);
    if (flags < 0) {
        return;
    }
    flags &= ~O_NONBLOCK; // blocking it
    flags |= FD_CLOEXEC; // close on exec
    if (fcntl(mine_master_fd, F_SETFL, flags) < 0) {
        return;
    }

    // grant and unlock slave
    char *slave = ptsname(mine_master_fd);
    if (grantpt(mine_master_fd) != 0 ||
        unlockpt(mine_master_fd) != 0) {
        return;
    }

    pg->ssh.child = fork();
    // child for exec
    if (pg->ssh.child == 0) {
        setenv("TERM", "xterm", 1);
        int mine_slave_fd = open(slave, O_RDWR);   // used by ssh
        setsid();
        setpgid(0, 0);
        ioctl(mine_slave_fd, TIOCSCTTY, mine_slave_fd);

        close(0);
        close(1);
        close(2);
        dup2(mine_slave_fd, 0);
        dup2(mine_slave_fd, 1);
        dup2(mine_slave_fd, 2);

        printf("\n");
        printf(PACKAGE" v"VERSION"\n");
        printf(COPYRIGHT"\n");
        printf("\n");
        printf("Connecting ... %s:%s\n", pg->ssh.cfg.host, pg->ssh.cfg.port);
        printf("\n");

        char host[256];
        memset(host, 0x00, sizeof(host));
        sprintf(host, "%s@%s", pg->ssh.cfg.user, pg->ssh.cfg.host);
        execlp(SSH, SSH, host, "-p", pg->ssh.cfg.port, NULL);
    }

    // thread for waitpid
    pthread_t tid;
    pthread_create(&tid, NULL, wait_ssh_child, pg);
    
    // for expect
    int already_login = 0;
    struct winsize old_size = {0,0,0,0};
    int row = 0;
    int col = 0;
    fd_set set;
    struct timeval tv = {0, 100};
    while (pg->ssh.need_stop == 0) {
        // vte_pty的尺寸变化时，修改mine_master_fd,以便它去通知ssh
        if (vte_pty_get_size(pg->ssh.pty, &row, &col, NULL) == TRUE) {
            if (row != old_size.ws_row ||
                col != old_size.ws_col) {
                old_size.ws_row = (short) row;
                old_size.ws_col = (short) col;
                ioctl(mine_master_fd, TIOCSWINSZ, &old_size);
            }
        }

        //
        // 打印ssh返回的输出，并判断是否应该执行自动输入
        //
        // mine_master_fd -> vte_slave_fd
        FD_ZERO(&set);
        FD_SET(mine_master_fd, &set);
        if (select(mine_master_fd+1, &set, NULL, NULL, &tv) > 0) {
            char buf[256];
            int len = read(mine_master_fd, buf, sizeof(buf));
            if (len > 0) {
                write(vte_slave_fd, buf, len);

                // 如果当前没有登录成功, 自动输入密码
                if (already_login == 0 && str_is_endwith(buf, len, SSH_PASSWORD)) {
                    already_login = 1;
                    write(mine_master_fd, pg->ssh.cfg.pass, strlen(pg->ssh.cfg.pass));
                    write(mine_master_fd, "\n", 1);
                }
            }
        }

        //
        // 读取vte_pty的用户输入，并发送到给ssh
        //
        // vte_slave_fd -> mine_master_fd
        FD_ZERO(&set);
        FD_SET(vte_slave_fd, &set);
        if (select(vte_slave_fd+1, &set, NULL, NULL, &tv) > 0) {
            char buf[256];
            int len = read(vte_slave_fd, buf, sizeof(buf));
            if (len > 0) {
                write(mine_master_fd, buf, len);
            }
        }

        usleep(1000);
    }

    vte_pty_close(pg->ssh.pty);
}

static void *work(void *p)
{
    pg_t *pg = (pg_t*) p;
    switch (pg->type) {
    case PG_TYPE_SHELL:
        run_shell(pg); // block here;
        break;

    case PG_TYPE_SSH:  // block here;
        run_ssh(pg);
        break;

    default:
        break;
    }

    gdk_threads_enter();
    int num = gtk_notebook_page_num(GTK_NOTEBOOK(m_notebook), pg->body);
    gtk_notebook_remove_page(GTK_NOTEBOOK(m_notebook), num);
    gdk_threads_leave();

    return NULL;
}

static void on_menu_copy_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
    int i = gtk_notebook_get_current_page(GTK_NOTEBOOK(m_notebook));
    GtkWidget *p = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), i);
    pg_t *pg = (pg_t*) g_object_get_data(G_OBJECT(p), "pg");

    VteTerminal *vte = (VteTerminal*) NULL;
    if (pg->type == PG_TYPE_SHELL) {
        vte = (VteTerminal*) pg->shell.vte;
    }
    else if (pg->type == PG_TYPE_SSH) {
        vte = (VteTerminal*) pg->ssh.vte;
    }

    vte_terminal_copy_clipboard(vte);
    vte_terminal_select_none(vte);
}

static void on_cmd_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
    int i = gtk_notebook_get_current_page(GTK_NOTEBOOK(m_notebook));
    page_send_string_crlf(i, (char*) user_data);

    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), i);
    gtk_widget_grab_focus(page);
}

static void on_btn_clicked(GtkToolButton *item, gpointer user_data)
{
    GtkWidget *btn = (GtkWidget*) user_data;
    gtk_menu_popup(GTK_MENU(btn), NULL, NULL, NULL, NULL, 0, 0);
}

static void on_menu_paste_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
    int i = gtk_notebook_get_current_page(GTK_NOTEBOOK(m_notebook));
    GtkWidget *p = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), i);
    pg_t *pg = (pg_t*) g_object_get_data(G_OBJECT(p), "pg");

    VteTerminal *vte = (VteTerminal*) NULL;
    if (pg->type == PG_TYPE_SHELL) {
        vte = (VteTerminal*) pg->shell.vte;
    }
    else if (pg->type == PG_TYPE_SSH) {
        vte = (VteTerminal*) pg->ssh.vte;
    }

    vte_terminal_paste_clipboard(vte);
}

static void on_menu_copy_paste_clicked(GtkMenuItem *menuitem, gpointer user_data)
{
    int i = gtk_notebook_get_current_page(GTK_NOTEBOOK(m_notebook));
    GtkWidget *p = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), i);
    pg_t *pg = (pg_t*) g_object_get_data(G_OBJECT(p), "pg");

    VteTerminal *vte = (VteTerminal*) NULL;
    if (pg->type == PG_TYPE_SHELL) {
        vte = (VteTerminal*) pg->shell.vte;
    }
    else if (pg->type == PG_TYPE_SSH) {
        vte = (VteTerminal*) pg->ssh.vte;
    }

    vte_terminal_copy_clipboard(vte);
    vte_terminal_select_none(vte);
    vte_terminal_paste_clipboard(vte);
}

// 标签选中改变时
// 1、修改标签颜色
// 2、使标签内vte获得焦点
static void on_notebook_switch(GtkNotebook *notebook, GtkWidget *page,
                               guint page_num, gpointer user_data)
{
    // 修改标签颜色
    //
    // 未被选中为黑色，被选中为红色
    GdkColor color;

    pg_t *pg = NULL;
    gdk_color_parse("black", &color);
    int count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(m_notebook));
    int i = 0;
    for (i = 1; i<count; i++) {
        GtkWidget *p = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), i);
        pg = (pg_t*) g_object_get_data(G_OBJECT(p), "pg");
        if (pg->head.label) {
            gtk_widget_modify_fg(pg->head.label, GTK_STATE_NORMAL, &color);     
        }
    }

    pg = (pg_t*) g_object_get_data(G_OBJECT(page), "pg");
    gdk_color_parse("red", &color);
    if (pg->head.label) {
        gtk_widget_modify_fg(pg->head.label, GTK_STATE_NORMAL, &color);
    }

    // 移动焦点到vte上
    if (m_auto_focus) {
        gtk_widget_grab_focus(page);
    }
}

static void on_close_clicked(GtkWidget *widget, gpointer user_data)
{
    pg_t *pg = (pg_t*) user_data;
    int num = gtk_notebook_page_num(GTK_NOTEBOOK(m_notebook), pg->body);
    page_close(num);
}

// 右键显示菜单
static gboolean on_vte_button_press(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    VteTerminal *vte = (VteTerminal*) widget;
    GdkEventButton *button = (GdkEventButton*) event;

    if (button->type == GDK_BUTTON_PRESS && // 按下
        button->button == 3) {  // 右键

        gtk_widget_set_sensitive(m_menu_copy, FALSE);
        gtk_widget_set_sensitive(m_menu_copy_paste, FALSE);
        gtk_widget_set_sensitive(m_menu_paste, FALSE);

        // copy / copy_paste
        if (vte_terminal_get_has_selection(vte)) {
            gtk_widget_set_sensitive(m_menu_copy, TRUE);
            gtk_widget_set_sensitive(m_menu_copy_paste, TRUE);
        }

        // paste
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        if (clipboard && gtk_clipboard_wait_is_text_available(clipboard)) {
            gtk_widget_set_sensitive(m_menu_paste, TRUE);
        }
        
        // popup menu
        gtk_menu_popup(GTK_MENU(m_menu), NULL, NULL, NULL, NULL, button->button, button->time);;
    }

    return FALSE;
}

static int menu_create()
{
    int row = 0;
    GtkWidget *mi = NULL;

    m_menu = gtk_menu_new();

    m_menu_copy  = gtk_image_menu_item_new_with_label("Copy");
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(m_menu_copy),
                                  img_from_stock(GTK_STOCK_COPY, GTK_ICON_SIZE_MENU));
    gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(m_menu_copy), TRUE);
    g_signal_connect(G_OBJECT(m_menu_copy), "activate", G_CALLBACK(on_menu_copy_clicked), NULL);
    gtk_menu_attach(GTK_MENU(m_menu), m_menu_copy, 0, 1, row, row+1);
    row++;

    m_menu_paste  = gtk_image_menu_item_new_with_label("Paste");
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(m_menu_paste),
                                  img_from_stock(GTK_STOCK_PASTE, GTK_ICON_SIZE_MENU));
    gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(m_menu_paste), TRUE);
    g_signal_connect(G_OBJECT(m_menu_paste), "activate", G_CALLBACK(on_menu_paste_clicked), NULL);
    gtk_menu_attach(GTK_MENU(m_menu), m_menu_paste, 0, 1, row, row+1);
    row++;

    m_menu_copy_paste = gtk_menu_item_new_with_label("Copy & Paste");
    g_signal_connect(G_OBJECT(m_menu_copy_paste), "activate", G_CALLBACK(on_menu_copy_paste_clicked), NULL);
    gtk_menu_attach(GTK_MENU(m_menu), m_menu_copy_paste, 0, 1, row, row+1);
    row++;

    // separator
    mi = gtk_separator_menu_item_new();
    gtk_menu_attach(GTK_MENU(m_menu), mi, 0, 1, row, row+1);
    row++;

    mi = gtk_image_menu_item_new_with_label("Close");
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), img_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU));
    gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(mi), TRUE);
    g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(page_close_select), NULL);
    gtk_menu_attach(GTK_MENU(m_menu), mi, 0, 1, row, row+1);
    row++;

    gtk_widget_show_all(m_menu);

    return 0;
}

int page_init(GtkWidget *hub_page) 
{
    // popup menu
    menu_create();

    // notebook
    m_notebook = gtk_notebook_new();
    g_signal_connect_after(G_OBJECT(m_notebook), "switch-page", G_CALLBACK(on_notebook_switch), NULL);

    // 添加 hub 标签
    // tab = hbox + label + button
    pg_t *pg = (pg_t*) malloc(sizeof(pg_t));
    bzero(pg, sizeof(pg_t));
    pg->type = PG_TYPE_HUB;
    pg->head.box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    pg->head.image = img_from_stock(GTK_STOCK_PROPERTIES, GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(pg->head.box), pg->head.image, FALSE, FALSE, 10);
    gtk_widget_show_all(pg->head.box);

    // body
    pg->body = hub_page;
    g_object_set_data(G_OBJECT(pg->body), "pg", pg);

    // page
    gint num = gtk_notebook_append_page(GTK_NOTEBOOK(m_notebook), pg->body, pg->head.box);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(m_notebook), pg->body, TRUE);

    gtk_widget_show_all(m_notebook);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(m_notebook), num);

    return 0;
}

int page_term()
{
    page_foreach_close();

    return 0;
}

gint page_shell_create()
{
    char *tmp;

    pg_t *pg = (pg_t*) malloc(sizeof(pg_t));
    bzero(pg, sizeof(pg_t));

    // tab = hbox + label + button
    pg->type = PG_TYPE_SHELL;
    pg->head.box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    // pg->head.image = img_from_name(ICON_SHELL);
    // gtk_box_pack_start(GTK_BOX(pg->head.box), pg->head.image, FALSE, FALSE, 10);
    pg->head.label = gtk_label_new("Shell");
    gtk_box_pack_start(GTK_BOX(pg->head.box), pg->head.label, FALSE, FALSE, 10);
    pg->head.button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(pg->head.button), GTK_RELIEF_NONE);
    tmp = get_res_path(ICON_CLOSE);
    gtk_button_set_image(GTK_BUTTON(pg->head.button), gtk_image_new_from_file(tmp));
    free(tmp);
    gtk_box_pack_start(GTK_BOX(pg->head.box), pg->head.button, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(pg->head.button), "clicked", G_CALLBACK(on_close_clicked), pg);
    gtk_widget_show_all(pg->head.box);

    // pty + vte
    pg->body = vte_terminal_new();
    pg->shell.vte = pg->body;

    pg->shell.pty = vte_pty_new(VTE_PTY_DEFAULT, NULL); 
    vte_terminal_set_pty_object((VteTerminal*)pg->shell.vte, pg->ssh.pty);

    vte_terminal_set_font_from_string((VteTerminal*)pg->shell.vte, "WenQuanYi Micro Hei Mono 11");
    vte_terminal_set_scrollback_lines((VteTerminal*)pg->shell.vte, 1024);
    vte_terminal_set_scroll_on_keystroke((VteTerminal*)pg->shell.vte, 1);
    g_object_set_data(G_OBJECT(pg->shell.vte), "pg", pg);
    g_signal_connect(G_OBJECT(pg->shell.vte), "button-press-event", G_CALLBACK(on_vte_button_press), NULL);

    // page
    gint num = gtk_notebook_append_page(GTK_NOTEBOOK(m_notebook), pg->body, pg->head.box);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(m_notebook), pg->body, TRUE);

    gtk_widget_show_all(m_notebook);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(m_notebook), num);

    pthread_t tid;
    pthread_create(&tid, NULL, work, pg);

    return num;
}

gint page_ssh_create(cfg_t *cfg)
{
    char *tmp;

    if (NULL == cfg) {
        return -1;
    }

    if (strlen(cfg->host) == 0 || strlen(cfg->port) == 0 ||
        strlen(cfg->user) == 0 || strlen(cfg->pass) == 0) {
        return -1;
    }

    pg_t *pg = (pg_t*) malloc(sizeof(pg_t));
    bzero(pg, sizeof(pg_t));

    // cfg
    memcpy(&pg->ssh.cfg, cfg, sizeof(cfg_t));

    // tab = hbox + label + button
    pg->type = PG_TYPE_SSH;
    pg->head.box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    pg->head.image = img_from_name(ICON_SITE);
    gtk_box_pack_start(GTK_BOX(pg->head.box), pg->head.image, FALSE, FALSE, 10);
    char title[256];
    sprintf(title, "%s", cfg->name);
    pg->head.label = gtk_label_new(title);
    gtk_box_pack_start(GTK_BOX(pg->head.box), pg->head.label, FALSE, FALSE, 10);
    pg->head.button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(pg->head.button), GTK_RELIEF_NONE);
    tmp = get_res_path(ICON_CLOSE);
    gtk_button_set_image(GTK_BUTTON(pg->head.button), gtk_image_new_from_file(tmp));
    free(tmp);
    gtk_box_pack_start(GTK_BOX(pg->head.box), pg->head.button, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(pg->head.button), "clicked", G_CALLBACK(on_close_clicked), pg);
    gtk_widget_show_all(pg->head.box);

    // body container
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    pg->body = vbox;
    g_object_set_data(G_OBJECT(pg->body), "pg", pg);

    // toolbar
    GtkWidget *toolbar = gtk_toolbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_BOTH);
        GtkToolItem *item;
        int i = 0;
        for (i=0; i<BTN_MAX_COUNT && cfg->btn[i].name[0] != '\0'; i++) {
            //item = gtk_menu_tool_button_new(NULL, cfg->btn[i].name);
            item = gtk_tool_button_new(NULL, cfg->btn[i].name);
            gtk_toolbar_insert(GTK_TOOLBAR(toolbar), item, -1);

            GtkWidget *menu = gtk_menu_new();
            g_signal_connect(G_OBJECT(item), "clicked", G_CALLBACK(on_btn_clicked), menu);
            //gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(item), menu);

            int j = 0;
            for (j=0; j<CMD_MAX_COUNT && cfg->btn[i].cmd[j].name[0] != '\0'; j++) {
                GtkWidget *cmd = gtk_menu_item_new_with_label(cfg->btn[i].cmd[j].name);
                gtk_menu_attach(GTK_MENU(menu), cmd, 0, 1, j, j+1);
                g_signal_connect(G_OBJECT(cmd), "activate", G_CALLBACK(on_cmd_clicked), cfg->btn[i].cmd[j].str);
            }
            gtk_widget_show_all(menu);


            // |
            item = gtk_separator_tool_item_new();
            gtk_toolbar_insert(GTK_TOOLBAR(toolbar), item, -1);
        }

    // pty + vte
    GtkWidget *vte = vte_terminal_new();
    pg->ssh.vte = vte;
    vte_terminal_set_emulation((VteTerminal*) vte, "xterm");
    gtk_box_pack_start(GTK_BOX(vbox), vte, TRUE, TRUE, 0);
    pg->ssh.pty = vte_pty_new(VTE_PTY_DEFAULT, NULL); 
    vte_terminal_set_pty_object((VteTerminal*)vte, pg->ssh.pty);
    vte_terminal_set_font_from_string((VteTerminal*)vte, "WenQuanYi Micro Hei Mono 11");
    vte_terminal_set_scrollback_lines((VteTerminal*)vte, 1024);
    vte_terminal_set_scroll_on_keystroke((VteTerminal*)vte, 1);
    g_signal_connect(G_OBJECT(vte), "button-press-event", G_CALLBACK(on_vte_button_press), NULL);

    // page
    gint num = gtk_notebook_append_page(GTK_NOTEBOOK(m_notebook), pg->body, pg->head.box);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(m_notebook), pg->body, TRUE);

    gtk_widget_show_all(m_notebook);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(m_notebook), num);

    pthread_t tid;
    pthread_create(&tid, NULL, work, pg);

    gtk_widget_grab_focus(vte);

    return num;
}

int page_foreach_close()
{
    int i;
    int count = page_get_count();
    for (i = 0; i<count; i++) {
        page_close(i);
    }

    return 0;
}

int page_foreach_send_char(char c)
{
    int i;

    int count = page_get_count();
    for (i = 0; i<count; i++) {
        GtkWidget *p = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), i);
        pg_t *pg = (pg_t*) g_object_get_data(G_OBJECT(p), "pg");
        if (pg->type == PG_TYPE_SSH) {
            int mine_master_fd = vte_pty_get_fd(pg->ssh.pty);
            write(mine_master_fd, &c, 1);
        }
    }

    return 0;
}

int page_foreach_send_string(char *str)
{
    int i;

    int count = page_get_count();
    for (i = 0; i<count; i++) {
        GtkWidget *p = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), i);
        pg_t *pg = (pg_t*) g_object_get_data(G_OBJECT(p), "pg");
        if (pg->type == PG_TYPE_SSH) {
            int mine_master_fd = vte_pty_get_fd(pg->ssh.pty);
            write(mine_master_fd, str, strlen(str));
        }
    }

    return 0;
}

int page_send_string(int i, char *str)
{
    GtkWidget *p = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), i);
    pg_t *pg = (pg_t*) g_object_get_data(G_OBJECT(p), "pg");
    if (pg->type == PG_TYPE_SSH) {
        int mine_master_fd = vte_pty_get_fd(pg->ssh.pty);
        write(mine_master_fd, str, strlen(str));
    }

    return 0;
}

int page_send_string_crlf(int i, char *str)
{
    page_send_string(i, str);
    page_send_string(i, "\n");
    return 0;
}

GtkWidget *page_get_notebook()
{
    return m_notebook;
}

int page_get_count()
{
    return gtk_notebook_get_n_pages(GTK_NOTEBOOK(m_notebook));
}

int page_get_select_num()
{
    return gtk_notebook_get_current_page(GTK_NOTEBOOK(m_notebook));
}

void page_set_select_num(int i)
{
    gtk_notebook_set_current_page(GTK_NOTEBOOK(m_notebook), i);
}

void page_set_auto_focus(int b)
{
    m_auto_focus = (b!=0);
}

int page_close(int n)
{
    GtkWidget *p = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), n);
    pg_t *pg = (pg_t*) g_object_get_data(G_OBJECT(p), "pg");
    if (pg->type == PG_TYPE_SSH) {
        kill(pg->ssh.child, SIGKILL);
    }
    if (pg->type == PG_TYPE_SHELL) {
        kill(pg->shell.child, SIGKILL);
    }

    return 0;
}

int page_close_select()
{
    return page_close(page_get_select_num());
}

int page_set_title(int i, char *str)
{
    return -1;
}
