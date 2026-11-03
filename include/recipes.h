#ifndef RECIPES_H
#define RECIPES_H
#include <stdint.h>
#include "gr2_fmt.h"
#include "look.h"

/* Film recipes: text files in H:/cfw/recipes (rom: A:/cfw/recipes first; src/recipes.c), listed
 * by name, then "Stock" (always the last entry, not a file). CFW task only, except the two early
 * functions, which the flashed loader's boot-task hooks call (src/main.c). */
#define RECIPES_MAX 24                    /* recipe files; the list holds one more: Stock */
#define RECIPE_NAME_LEN 24                /* name= up to 23 characters */
#define RECIPE_FILE_MAX 4094              /* bytes a recipe file may have */

void recipes_init(void);                  /* scan the folder (again), hand the names + Stock to the carousel */
int recipes_count(void);                  /* recipe files listed (Stock not counted) */
void recipes_poll(void);                  /* ~200 ms: show the slot's or previewed recipe look */
/* First loader hook, after cfg_load: read the MY slots' recipe files (no folder scan; a missing or
 * invalid file is skipped quietly and reported later by recipes_init). */
void recipes_early_load(void);
/* Every hook until it returns 1: show the active slot's look once the mode properties are
 * final (GR2_MODE_PROPS_FINAL) and the ISP holds IQ.bin. 0 = not yet, retry. */
int recipes_early_look(void);
/* Live preview: show recipe i's look while the carousel is open (in the
 * current colour mode if it is Standard / Vivid / Setting1 / Setting2);
 * -1 ends the preview. */
void recipes_preview(int i);
/* Recipe i's look (flags 0 = none, also Stock), *mono = 1 for B&W effect bases; 0 if no recipe i. */
const look_t *recipes_look(int i, int *mono);
/* Write recipe i into My Settings slot k (0..2) and make its look the slot's. Stock instead puts
 * every property a recipe can set and the slot name back to their factory values and clears the
 * slot's recipe (stock IQ). Returns 0 or a negative RECIPE_E_* error. */
int recipes_apply(int i, int k);

#define RECIPE_E_FILE   -1   /* file unreadable or invalid */
#define RECIPE_E_BUSY   -2   /* stock My Settings edit in progress */
#define RECIPE_E_SLOT   -3   /* bad slot */

/* ---- export (src/export.c): a preset's property values as recipe keys ----------------------
 * get(src, id) returns the stored (raw) value of property id. recipes_format writes `name=`, `base=`
 * and every key that holds the preset's value exactly (applying the file sets those properties to
 * these values), and `#` comments for values a recipe cannot hold. mode < 0: a whole preset (slot,
 * box, live) with the base from EFFECT / IMAGE_SETTINGS; slot 0..2 also writes that MY slot's CFW
 * look keys when its recipe's look shows with this base. mode >= 0: the image preset of that colour
 * mode (base and its sliders only). Returns 0, or -1 if the base is none a recipe has. */
typedef uint32_t (*recipe_get_t)(const void *src, uint32_t id);
int recipes_format(text_t *t, recipe_get_t get, const void *src, const char *name, int slot, int mode);
/* Image preset i (Setting1, Setting2, then the effects with sliders): its colour mode and base key,
 * *changed = a slider in src differs from the factory value; -1 past the last one. */
int recipes_preset(int i, recipe_get_t get, const void *src, const char **key, int *changed);

#endif /* RECIPES_H */
