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

#include "page.h"
#include "site.h"

static GtkWidget *m_debug;

int debug_create_show(GtkWidget *parent)
{
    // window
    m_debug = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(m_debug), "Debug Window");
    gtk_window_set_transient_for(GTK_WINDOW(m_debug), GTK_WINDOW(parent));
    gtk_widget_show_all(m_debug);

    return 0;
}
