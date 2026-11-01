#ifndef CONFIG_H
#define CONFIG_H
#include <stdint.h>

/* Persistent CFW settings in H:/cfw/config.txt (CFW task only). */
void cfg_load(void);
int cfg_has(const char *key);
uint32_t cfg_get(const char *key, uint32_t def);
void cfg_set(const char *key, uint32_t value);   /* marks dirty if changed */
void cfg_save(void);                             /* writes only when dirty */

#endif /* CONFIG_H */
