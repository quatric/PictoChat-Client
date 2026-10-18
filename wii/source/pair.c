#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include "pair.h"
#include "cJSON.h"
#include "net.h"

static lwp_t thread = LWP_THREAD_NULL;
static volatile int cancelled;
static volatile pair_state_t state = PAIR_IDLE;
static char server[128], code[16], url[192], error[192];
static char *token;

static const char *json_error(const http_response *res, const char *fallback)
{
    static char msg[192];
    cJSON *o = res->body ? cJSON_Parse(res->body) : NULL;
    cJSON *e = cJSON_GetObjectItemCaseSensitive(o, "error");
    snprintf(msg, sizeof msg, "%s", cJSON_IsString(e) ? e->valuestring : res->error[0] ? res->error : fallback);
    cJSON_Delete(o);
    return msg;
}

static void fail(const char *text)
{
    snprintf(error, sizeof error, "%s", text);
    state = PAIR_FAILED;
}

static void *pair_thread(void *arg)
{
    (void)arg;
    char endpoint[256];
    http_response res;
    snprintf(endpoint, sizeof endpoint, "%s/api/v1/pictochat/pair", server);
    if (http_request("POST", endpoint, "application/json", "{}", 2, &res, NULL) < 0 || res.status != 200) {
        fail(json_error(&res, "Could not get a pairing code from PictoCard."));
        http_response_free(&res);
        return NULL;
    }
    cJSON *o = cJSON_Parse(res.body);
    cJSON *c = cJSON_GetObjectItemCaseSensitive(o, "code"), *u = cJSON_GetObjectItemCaseSensitive(o, "url");
    if (!cJSON_IsString(c)) {
        cJSON_Delete(o);
        http_response_free(&res);
        fail("PictoCard sent an unexpected answer.");
        return NULL;
    }
    snprintf(code, sizeof code, "%s", c->valuestring);
    snprintf(url, sizeof url, "%s", cJSON_IsString(u) ? u->valuestring : "");
    cJSON_Delete(o);
    http_response_free(&res);
    state = PAIR_WAITING;

    snprintf(endpoint, sizeof endpoint, "%s/api/v1/pictochat/pair/%s", server, code);
    int failures = 0;
    while (!cancelled) {
        for (int i = 0; i < 20 && !cancelled; i++) usleep(100 * 1000);
        if (cancelled) break;
        if (http_request("GET", endpoint, NULL, NULL, 0, &res, NULL) < 0) {
            /* keep trying through short network hiccups */
            if (++failures >= 5) {
                fail(res.error);
                return NULL;
            }
            continue;
        }
        failures = 0;
        if (res.status == 200) {
            o = cJSON_Parse(res.body);
            cJSON *t = cJSON_GetObjectItemCaseSensitive(o, "token");
            if (cJSON_IsString(t)) {
                token = strdup(t->valuestring);
                state = PAIR_DONE;
            } else {
                fail("PictoCard sent an unexpected answer.");
            }
            cJSON_Delete(o);
            http_response_free(&res);
            return NULL;
        }
        if (res.status != 202 && res.status != 429) {
            fail(json_error(&res, "The pairing code has expired."));
            http_response_free(&res);
            return NULL;
        }
        http_response_free(&res);
    }
    return NULL;
}

void pair_start(const char *srv)
{
    pair_cancel();
    snprintf(server, sizeof server, "%s", srv);
    code[0] = url[0] = error[0] = 0;
    free(token);
    token = NULL;
    cancelled = 0;
    state = PAIR_REQUESTING;
    if (LWP_CreateThread(&thread, pair_thread, NULL, NULL, 64 * 1024, 50) < 0) {
        thread = LWP_THREAD_NULL;
        fail("Could not start the network thread.");
    }
}

void pair_cancel(void)
{
    cancelled = 1;
    if (thread != LWP_THREAD_NULL) {
        LWP_JoinThread(thread, NULL);
        thread = LWP_THREAD_NULL;
    }
    state = PAIR_IDLE;
}

pair_state_t pair_state(void) { return state; }
const char *pair_code(void) { return code; }
const char *pair_url(void) { return url; }
const char *pair_token(void) { return token ? token : ""; }
const char *pair_error(void) { return error[0] ? error : "Pairing failed."; }
