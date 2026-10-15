#pragma once

/* Captcha pairing through pictocard.net: the 3DS shows a code, the player solves
 * pict.chat's hCaptcha at <server>/pictochat/<code> on a phone or computer, and
 * the token comes back here. Runs on a background thread. */

typedef enum { PAIR_IDLE, PAIR_REQUESTING, PAIR_WAITING, PAIR_DONE, PAIR_FAILED } pair_state_t;

void pair_start(const char *server);
void pair_cancel(void);
pair_state_t pair_state(void);
const char *pair_code(void);
const char *pair_url(void);
const char *pair_token(void);   /* once PAIR_DONE */
const char *pair_error(void);   /* once PAIR_FAILED */
