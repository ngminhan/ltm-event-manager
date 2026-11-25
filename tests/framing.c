#include "net.h"
#include <assert.h>
#include <string.h>

static bool count(void *context, const cJSON *message) {
    assert(cJSON_IsObject(message));
    ++*(int *)context;
    return true;
}

int main(void) {
    Channel c = {0};
    int messages = 0;
    const unsigned char frame[] = {0, 0, 0, 2, '{', '}'};
    for (size_t i = 0; i < sizeof frame; i++) {
        c.input[c.received++] = frame[i];
        assert(decode_frames(&c, count, &messages));
        assert(messages == (i + 1 == sizeof frame));
    }
    memcpy(c.input, frame, sizeof frame);
    memcpy(c.input + sizeof frame, frame, sizeof frame);
    c.received = 2 * sizeof frame;
    assert(decode_frames(&c, count, &messages));
    assert(messages == 3 && c.received == 0);
    return 0;
}
