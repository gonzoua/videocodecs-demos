/*-
 * Copyright (c) 2018 Oleksandr Tymoshenko <gonzo@bluezbox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __H264_DECODER_MPP_H__
#define __H264_DECODER_MPP_H__

typedef void (*decoder_callback_t)(void *arg, uint8_t *yplane, uint8_t *uvplane,
    int width, int height, int h_stride, int v_stride);

struct h264_decoder_mpp * h264_mpp_decoder_create(decoder_callback_t callback, void *arg);
int h264_decoder_mpp_destroy(struct h264_decoder_mpp * decoder);
int h264_decoder_mpp_submit_packet(struct h264_decoder_mpp * decoder, uint8_t *packet, ssize_t len);
int h264_decoder_mpp_get_frame(struct h264_decoder_mpp * decoder);

#endif /* __H264_DECODER_MPP_H__ */
