#ifndef EVENT_AUTH_H
#define EVENT_AUTH_H
#include "action.h"

typedef struct AuthWork {
    cJSON *request;
    unsigned char salt[16], expected[32], hash[32];
    Result result;
    int slot;
} AuthWork;

Result prepare_auth(AuthWork *work, sqlite3 *db, const Session *session, const cJSON *request);
void hash_password(AuthWork *work);
Result finish_auth(Action *a, const AuthWork *work);
void free_auth(AuthWork *work);
#endif
