#ifndef IAP_STREAM_H
#define IAP_STREAM_H
#include "iap_proto.h"
#include <string.h>
typedef struct
{
    uint16_t used;
    uint8_t data[IAP_MAX_FRAME_LEN];
} IapStream_t;
/* Feed one byte; callback sees a complete CRC-checked frame. Retain incomplete
 * prefix, discard one byte on bad header/length/CRC, then resynchronize. */
static __inline void IapStream_Push(IapStream_t *s, uint8_t byte,
                                    void (*handle)(const IapFrame_t *))
{
    IapFrame_t f;
    int n;
    if (s->used == sizeof(s->data))
        s->used = 0;
    s->data[s->used++] = byte;
    while (s->used)
    {
        if (s->data[0] != IAP_FRAME_HEAD0 || (s->used > 1 && s->data[1] != IAP_FRAME_HEAD1))
            n = -1;
        else
            n = IapProto_ParseFrame(s->data, s->used, &f);
        if (n == IAP_PARSE_NEED_MORE)
            return;
        if (n > 0)
        {
            handle(&f);
            s->used = (uint16_t)(s->used - n);
            memmove(s->data, s->data + n, s->used);
        }
        else
        {
            --s->used;
            memmove(s->data, s->data + 1, s->used);
        }
    }
}
#endif
