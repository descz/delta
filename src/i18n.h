/* i18n.h - idioma da interface */
#ifndef DELTA_I18N_H
#define DELTA_I18N_H

void        i18n_set(int lang);
int         i18n_get(void);
int         i18n_count(void);
const char *i18n_code(int lang);
const char *i18n_name(int lang);
int         i18n_from_code(const char *code);
const char *tr(const char *key);

#endif
