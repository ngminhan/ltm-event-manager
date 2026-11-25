#ifndef EVENT_NET_H
#define EVENT_NET_H
#include <cJSON.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

enum { FRAME_LIMIT = 65536, QUEUE_LIMIT = 4 * FRAME_LIMIT };
typedef struct {
    unsigned char input[FRAME_LIMIT + 4];
    size_t received;
    bool paused;
    unsigned char *output;
    size_t sent, queued;
} Channel;

typedef bool (*MessageHandler)(void *, const cJSON *);
/* Stay within the 15 significant digits cJSON can serialize without rounding. */
#define MAX_JSON_ID INT64_C(999999999999999)

int tcp_socket(const char *host, const char *port, bool listener);
bool valid_json_text(const unsigned char *p, size_t len);
bool nonblocking(int fd);
bool decode_frames(Channel *channel, MessageHandler handle, void *context);
bool receive_frames(int fd, Channel *channel, MessageHandler handle, void *context);
bool queue_json(Channel *channel, const cJSON *message);
bool flush_frames(int fd, Channel *channel);
const char *field(const cJSON *object, const char *name);
int64_t positive_id(const cJSON *object, const char *name);
#endif
