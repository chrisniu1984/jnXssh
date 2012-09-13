#ifndef __SITE_H__
#define __SITE_H__

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

int site_init();
void site_term();

int site_load();

GtkWidget *site_get_object();

#ifdef __cplusplus
}
#endif

#endif // __SITE_H__
