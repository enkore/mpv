/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <libavcodec/avcodec.h>

#include "common/av_common.h"
#include "common/common.h"

#include "packet.h"

static void packet_destroy(void *ptr)
{
    struct demux_packet *dp = ptr;
    av_packet_unref(dp->avpacket);
}

// This actually preserves only data and side data, not PTS/DTS/pos/etc.
// It also allows avpkt->data==NULL with avpkt->size!=0 - the libavcodec API
// does not allow it, but we do it to simplify new_demux_packet().
struct demux_packet *new_demux_packet_from_avpacket(struct AVPacket *avpkt)
{
    if (avpkt->size > 1000000000)
        return NULL;
    struct demux_packet *dp = talloc(NULL, struct demux_packet);
    talloc_set_destructor(dp, packet_destroy);
    *dp = (struct demux_packet) {
        .pts = MP_NOPTS_VALUE,
        .dts = MP_NOPTS_VALUE,
        .duration = -1,
        .pos = -1,
        .stream = -1,
        .avpacket = talloc_zero(dp, AVPacket),
    };
    av_init_packet(dp->avpacket);
    int r = -1;
    if (avpkt->data) {
        // We hope that this function won't need/access AVPacket input padding,
        // because otherwise new_demux_packet_from() wouldn't work.
        r = av_packet_ref(dp->avpacket, avpkt);
    } else {
        r = av_new_packet(dp->avpacket, avpkt->size);
    }
    if (r < 0) {
        *dp->avpacket = (AVPacket){0};
        talloc_free(dp);
        return NULL;
    }
    dp->buffer = dp->avpacket->data;
    dp->len = dp->avpacket->size;
    return dp;
}

// Input data doesn't need to be padded.
struct demux_packet *new_demux_packet_from(void *data, size_t len)
{
    if (len > INT_MAX)
        return NULL;
    AVPacket pkt = { .data = data, .size = len };
    return new_demux_packet_from_avpacket(&pkt);
}

struct demux_packet *new_demux_packet(size_t len)
{
    if (len > INT_MAX)
        return NULL;
    AVPacket pkt = { .data = NULL, .size = len };
    return new_demux_packet_from_avpacket(&pkt);
}

void demux_packet_shorten(struct demux_packet *dp, size_t len)
{
    assert(len <= dp->len);
    dp->len = len;
    memset(dp->buffer + dp->len, 0, FF_INPUT_BUFFER_PADDING_SIZE);
}

void free_demux_packet(struct demux_packet *dp)
{
    talloc_free(dp);
}

struct demux_packet *demux_copy_packet(struct demux_packet *dp)
{
    struct demux_packet *new = NULL;
    if (dp->avpacket) {
        new = new_demux_packet_from_avpacket(dp->avpacket);
    } else {
        // Some packets might be not created by new_demux_packet*().
        new = new_demux_packet_from(dp->buffer, dp->len);
    }
    if (!new)
        return NULL;
    new->pts = dp->pts;
    new->dts = dp->dts;
    new->duration = dp->duration;
    return new;
}
