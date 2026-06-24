#ifndef NTL_PANEL_H
#define NTL_PANEL_H

#include <thermite.h>
#include <stdbool.h>

void ui_ntl_panel_init(void);
void ui_ntl_panel_open(void);
void ui_ntl_panel(tenv* env);
bool ui_ntl_panel_is_open(void);   /* title_screen.c uses this to hide content */

#endif /* NTL_PANEL_H */
