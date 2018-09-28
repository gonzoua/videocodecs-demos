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

#ifndef __YUV_READER_H__
#define __YUV_READER_H__

struct yuv_frame
{
    int                 width;
    int                 height;

    /* Aligned pointers to pixel data */
    uint8_t             *Y;
    uint8_t             *U;
    uint8_t             *V;

    /* Size of the plane (not allocated memory) */
    size_t              Ysize;
    size_t              Usize;
    size_t              Vsize;

    /* Original pointers to use in free() */
    uint8_t             *Yptr;
    uint8_t             *Uptr;
    uint8_t             *Vptr;
};

struct yuv_reader
{
    int                 width;
    int                 height;
    int                 fd;
};

typedef struct yuv_reader * yuv_reader_t;
typedef struct yuv_frame * yuv_frame_t;

yuv_reader_t yuv_reader_open(const char *path, int width, int height);
int yuv_read_frame(yuv_reader_t reader, yuv_frame_t framep);
yuv_frame_t yuv_alloc_frame(yuv_reader_t reader);
void yuv_free_frame(yuv_frame_t frame);

#endif /* __YUV_READER_H__ */

