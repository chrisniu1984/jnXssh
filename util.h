#ifndef __UTIL_H__
#define __UTIL_H__

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "config.h"

#ifndef MIN
#define MIN(a,b) ((a)<=(b)?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) ((a)>=(b)?(a):(b))
#endif

static inline char* get_res_path(const char *res)
{
    char *path = (char*) malloc(256);
    memset(path, 0x00, 256);
    sprintf(path, "%s/%s", PATH, res);
    return path;
}


static inline int str_is_endwith(char *str, int len, char *end)
{
    char *p = strstr(str, end);
    if (p &&
        (unsigned int)(len-(p-str)) == strlen(end)) {
        return 1;
    }

    return 0;
}

static inline GtkWidget *img_from_name(const char *res)
{
    char *tmp = get_res_path(res);
    GtkWidget *img = gtk_image_new_from_file(tmp);
    free(tmp);

    return img;
}

static inline GtkWidget *img_from_stock(char *id, GtkIconSize size)
{
    return gtk_image_new_from_stock(id, size);
}


#endif // __UTIL_H__
