#ifndef NEO_EXERCISE_H
#define NEO_EXERCISE_H
#include "platform.h"
/* Hosted acceptance harness. The directory must already exist. No host path
   is exposed to the image; its package receives one scalar document grant. */
int neo_image_exercise(const char *, const char *, const char *, bool, const neo_code_platform *);
int neo_exercise_checkpoint(const char *);
#endif
