#include "action.h"
#include <ctype.h>
#include <stdio.h>

static bool timestamp(const char *s) {
    if (!valid_text(s, 64) || strlen(s) < 20)
        return false;
    const char *pattern = "dddd-dd-ddTdd:dd:dd";
    for (size_t i = 0; pattern[i]; i++)
        if (pattern[i] == 'd' ? !isdigit((unsigned char)s[i]) : s[i] != pattern[i])
            return false;
    int year, month, day, h, m, sec;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &h, &m, &sec) != 6)
        return false;
    int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    days[2] += year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (month < 1 || month > 12 || day < 1 || day > days[month] || h > 23 || m > 59 || sec > 59)
        return false;
    s += 19;
    if (*s == '.') {
        if (!isdigit((unsigned char)*++s))
            return false;
        while (isdigit((unsigned char)*s))
            s++;
    }
    if (strcmp(s, "Z") == 0)
        return true;
    return strlen(s) == 6 && (*s == '+' || *s == '-') && s[3] == ':' &&
           isdigit((unsigned char)s[1]) && isdigit((unsigned char)s[2]) &&
           isdigit((unsigned char)s[4]) && isdigit((unsigned char)s[5]) &&
           sscanf(s + 1, "%2d:%2d", &h, &m) == 2 && h < 24 && m < 60;
}

static Result insert_event(Action *a) {
    if (!timestamp(field(a->request, "starts_at")) ||
        !valid_text(field(a->request, "location"), 512) ||
        !valid_text(field(a->request, "description"), 4096))
        return BAD;
    sqlite3_stmt *s = prepare(a, "INSERT INTO events(owner,starts_at,location,description) "
                                 "SELECT :actor,:starts_at,:location,:description "
                                 "WHERE COALESCE((SELECT MAX(id) FROM events), 0) < ?");
    if (!s)
        return INTERNAL;
    if (sqlite3_bind_int64(s, 5, MAX_JSON_ID) != SQLITE_OK) {
        sqlite3_finalize(s);
        return INTERNAL;
    }
    Result result = finish_write(s);
    if (result == OK) {
        if (!sqlite3_changes(a->db))
            return INTERNAL;
        cJSON_AddNumberToObject(a->data, "event_id", (double)sqlite3_last_insert_rowid(a->db));
    }
    return result;
}

Result participation(Action *a) {
    bool invite = is_op(a, "INVITE"), approve = is_op(a, "APPROVE_JOIN");
    if (!a->event || ((invite || approve) && !valid_text(a->target, USER_LIMIT)))
        return BAD;
    sqlite3_stmt *s = prepare(a, "SELECT owner FROM events WHERE id=:event");
    if (!s)
        return INTERNAL;
    int rc = sqlite3_step(s);
    char owner[USER_LIMIT + 1];
    if (rc == SQLITE_ROW)
        snprintf(owner, sizeof owner, "%s", sqlite3_column_text(s, 0));
    sqlite3_finalize(s);
    if (rc != SQLITE_ROW)
        return rc == SQLITE_DONE ? MISSING : INTERNAL;
    bool creator = strcmp(a->session->username, owner) == 0;
    if ((invite || approve) && !creator)
        return DENIED;
    if ((invite || approve) ? strcmp(a->target, owner) == 0 : creator)
        return CONFLICT;
    const char *sql;
    if (invite)
        sql = "INSERT INTO participation SELECT :event,name,'invited' FROM users WHERE "
              "name=:username";
    else if (approve)
        sql = "UPDATE participation SET state='joined' WHERE event=:event AND member=:username AND "
              "state='requested'";
    else if (is_op(a, "JOIN_REQUEST"))
        sql = "INSERT INTO participation VALUES(:event,:actor,'requested')";
    else
        sql = "UPDATE participation SET state='joined' WHERE event=:event AND member=:actor AND "
              "state='invited'";
    s = prepare(a, sql);
    if (!s)
        return INTERNAL;
    Result result = finish_write(s);
    if (result != OK)
        return result;
    if (!sqlite3_changes(a->db))
        return MISSING;
    if (invite || is_op(a, "JOIN_REQUEST")) {
        cJSON *note = cJSON_CreateObject();
        cJSON_AddStringToObject(note, "type", "notification");
        cJSON_AddStringToObject(note, "op", invite ? "INVITE_RECEIVED" : "JOIN_REQUEST_RECEIVED");
        cJSON_AddNumberToObject(note, "event_id", (double)a->event);
        cJSON_AddStringToObject(note, "from", a->session->username);
        a->notify(invite ? a->target : owner, note);
        cJSON_Delete(note);
    }
    return OK;
}


Result create_event(Action *a) {
    const cJSON *key = cJSON_GetObjectItemCaseSensitive(a->request, "creation_key");
    if (!key)
        return insert_event(a);
    if (!valid_text(cJSON_GetStringValue(key), 128))
        return BAD;
    if (sqlite3_exec(a->db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK)
        return INTERNAL;
    Result r = INTERNAL;
    sqlite3_stmt *s = prepare(a, "SELECT id, starts_at IS :starts_at AND location IS :location "
                                 "AND description IS :description FROM creations JOIN events ON id=event "
                                 "WHERE creations.actor=:actor AND token=:creation_key");
    int rc = s ? sqlite3_step(s) : SQLITE_ERROR;
    if (rc == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(s, 0);
        r = sqlite3_column_int(s, 1) ? OK : CONFLICT;
        if (id > MAX_JSON_ID)
            r = INTERNAL;
        if (r == OK)
            cJSON_AddNumberToObject(a->data, "event_id", (double)id);
    }
    sqlite3_finalize(s);
    if (rc == SQLITE_DONE && (r = insert_event(a)) == OK) {
        a->event = positive_id(a->data, "event_id");
        s = prepare(a, "INSERT INTO creations VALUES(:actor,:creation_key,:event)");
        r = s ? finish_write(s) : INTERNAL;
    }
    if (r == OK && sqlite3_exec(a->db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK)
        return OK;
    sqlite3_exec(a->db, "ROLLBACK", NULL, NULL, NULL);
    return r == OK ? INTERNAL : r;
}
