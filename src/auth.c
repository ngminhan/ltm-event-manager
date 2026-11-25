#include "auth.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdlib.h>

Result prepare_auth(AuthWork *work, sqlite3 *db, const Session *session, const cJSON *request) {
    bool login = !strcmp(field(request, "op"), "LOGIN");
    if (login && *session->username)
        return CONFLICT;
    const char *username = field(request, "username");
    if (!valid_text(username, USER_LIMIT) || !valid_text(field(request, "password"), 1024))
        return BAD;
    if (!login)
        return RAND_bytes(work->salt, sizeof work->salt) == 1 ? OK : INTERNAL;
    sqlite3_stmt *s = NULL;
    Result result = INTERNAL;
    if (sqlite3_prepare_v2(db, "SELECT salt,hash FROM users WHERE name=?", -1, &s, NULL) == SQLITE_OK &&
        sqlite3_bind_text(s, 1, username, -1, SQLITE_STATIC) == SQLITE_OK) {
        int rc = sqlite3_step(s);
        if (rc == SQLITE_DONE)
            result = INVALID;
        if (rc == SQLITE_ROW && sqlite3_column_bytes(s, 0) == sizeof work->salt &&
            sqlite3_column_bytes(s, 1) == sizeof work->expected) {
            memcpy(work->salt, sqlite3_column_blob(s, 0), sizeof work->salt);
            memcpy(work->expected, sqlite3_column_blob(s, 1), sizeof work->expected);
            result = OK;
        }
    }
    sqlite3_finalize(s);
    return result;
}

void hash_password(AuthWork *work) {
    const char *password = field(work->request, "password");
    if (!PKCS5_PBKDF2_HMAC(password, (int)strlen(password), work->salt, sizeof work->salt,
                           210000, EVP_sha256(), sizeof work->hash, work->hash))
        work->result = INTERNAL;
}

Result finish_auth(Action *a, const AuthWork *work) {
    if (!work || work->result != OK)
        return work ? work->result : INTERNAL;
    if (is_op(a, "LOGIN")) {
        if (CRYPTO_memcmp(work->hash, work->expected, sizeof work->hash))
            return INVALID;
        strcpy(a->session->username, a->target);
    } else {
        sqlite3_stmt *s = prepare(a, "INSERT INTO users VALUES(:username,?,?)");
        if (!s)
            return INTERNAL;
        if (sqlite3_bind_blob(s, 2, work->salt, sizeof work->salt, SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_bind_blob(s, 3, work->hash, sizeof work->hash, SQLITE_STATIC) != SQLITE_OK) {
            sqlite3_finalize(s);
            return INTERNAL;
        }
        Result r = finish_write(s);
        if (r != OK)
            return r;
    }
    cJSON_AddStringToObject(a->data, "username", a->target);
    return OK;
}

void free_auth(AuthWork *work) {
    cJSON_Delete(work->request);
    OPENSSL_cleanse(work, sizeof *work);
    free(work);
}
