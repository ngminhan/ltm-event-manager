#include "action.h"

Result list_page(Action *a) {
    bool events = is_op(a, "LIST_EVENTS"), pending = is_op(a, "LIST_PENDING");
    int64_t after = positive_id(a->request, "after");
    if (!after && cJSON_GetObjectItemCaseSensitive(a->request, "after"))
        return BAD;
    if (!events && !pending) {
        if (!a->event)
            return BAD;
        sqlite3_stmt *owner = prepare(a, "SELECT owner=:actor FROM events WHERE id=:event");
        if (!owner)
            return INTERNAL;
        int rc = sqlite3_step(owner);
        bool allowed = rc == SQLITE_ROW && sqlite3_column_int(owner, 0);
        sqlite3_finalize(owner);
        if (!allowed)
            return rc == SQLITE_DONE ? MISSING : rc == SQLITE_ROW ? DENIED : INTERNAL;
    }
    const char *sql = events ?
        "SELECT id,id AS event_id,owner,starts_at,location,description FROM events WHERE id>? ORDER BY id" :
        pending ?
        "SELECT p.rowid,p.event AS event_id,p.state,e.owner,p.member FROM participation p "
        "JOIN events e ON e.id=p.event WHERE p.rowid>? AND ((p.member=:actor AND state='invited') "
        "OR (e.owner=:actor AND state='requested')) ORDER BY p.rowid" :
        "SELECT rowid,member AS username FROM participation WHERE rowid>? AND event=:event "
        "AND state='joined' ORDER BY rowid";
    sqlite3_stmt *s = prepare(a, sql);
    if (!s)
        return INTERNAL;
    sqlite3_bind_int64(s, 1, after);
    cJSON *rows = cJSON_AddArrayToObject(a->data, events ? "events" : pending ? "invites" : "participants");
    cJSON *requests = pending ? cJSON_AddArrayToObject(a->data, "join_requests") : NULL;
    size_t used = 256; /* Reserve space for response keys, cursor, id and array delimiters. */
    int rc;
    int64_t last = 0;
    while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
        int64_t cursor = sqlite3_column_int64(s, 0);
        if (cursor > MAX_JSON_ID || (pending && sqlite3_column_int64(s, 1) > MAX_JSON_ID))
            break;
        cJSON *row = cJSON_CreateObject(), *dest = rows;
        for (int i = 1; i < (pending ? 2 : sqlite3_column_count(s)); i++) {
            const char *name = sqlite3_column_name(s, i);
            if (sqlite3_column_type(s, i) == SQLITE_INTEGER)
                cJSON_AddNumberToObject(row, name, (double)sqlite3_column_int64(s, i));
            else
                cJSON_AddStringToObject(row, name, (const char *)sqlite3_column_text(s, i));
        }
        if (pending) {
            bool invite = !strcmp((const char *)sqlite3_column_text(s, 2), "invited");
            dest = invite ? rows : requests;
            cJSON_AddStringToObject(row, invite ? "from" : "username",
                                    (const char *)sqlite3_column_text(s, invite ? 3 : 4));
        }
        char *json = cJSON_PrintUnformatted(row);
        if (!json) {
            cJSON_Delete(row);
            break;
        }
        used += strlen(json) + 1;
        cJSON_free(json);
        if (used > FRAME_LIMIT) {
            cJSON_Delete(row);
            if (last && cJSON_AddNumberToObject(a->data, "next_cursor", (double)last))
                rc = SQLITE_DONE;
            break;
        }
        cJSON_AddItemToArray(dest, row);
        last = cursor;
    }
    sqlite3_finalize(s);
    return rc == SQLITE_DONE ? OK : INTERNAL;
}
