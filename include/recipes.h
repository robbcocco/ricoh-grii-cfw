#ifndef RECIPES_H
#define RECIPES_H
#include <stdint.h>
#include "look.h"

/* Film recipes: text files in H:/cfw/recipes (src/recipes.c). CFW task only, except the
 * two early functions, which the flashed loader's boot-task hooks call (src/main.c). */
#define RECIPES_MAX 24

void recipes_init(void);                  /* scan the folder (again), hand the names to the carousel */
int recipes_count(void);
void recipes_poll(void);                  /* ~200 ms: show the slot's or previewed recipe look */
/* hook(1), after cfg_load: read the MY slots' recipe files (no folder scan; a missing or
 * invalid file is skipped quietly and reported later by recipes_init). */
void recipes_early_load(void);
/* Every hook until it returns 1: show the active slot's look once the mode properties are
 * final (GR2_MODE_PROPS_FINAL) and the ISP holds IQ.bin. 0 = not yet, retry. */
int recipes_early_look(void);
/* Live preview: show recipe i's look while the carousel is open (in the
 * current colour mode if it is Standard / Vivid / Setting1 / Setting2);
 * -1 ends the preview. */
void recipes_preview(int i);
/* Recipe i's look (flags 0 = none), *mono = 1 for B&W effect bases; 0 if no recipe i. */
const look_t *recipes_look(int i, int *mono);
/* Write recipe i into My Settings slot k (0..2) and make its look the slot's.
 * Returns 0 or a negative RECIPE_E_* error. */
int recipes_apply(int i, int k);

#define RECIPE_E_FILE   -1   /* file unreadable or invalid */
#define RECIPE_E_BUSY   -2   /* stock My Settings edit in progress */
#define RECIPE_E_SLOT   -3   /* bad slot */

#endif /* RECIPES_H */
