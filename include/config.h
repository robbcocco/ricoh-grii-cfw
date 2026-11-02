#ifndef CONFIG_H
#define CONFIG_H
#include <stdint.h>

/* Persistent CFW settings on D:/CFW (src/config.c; imported once from H:/cfw/config.txt). CFW task only;
 * the loader's first hook may cfg_load. */
void cfg_load(void);
int cfg_has(const char *key);
uint32_t cfg_get(const char *key, uint32_t def);
void cfg_set(const char *key, uint32_t value);   /* marks dirty if changed */
void cfg_save(void);                             /* writes only when dirty */

#endif /* CONFIG_H */
