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

#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/errno.h>

#include "h264_reader.h"

#define	DEFAULT_BUFFER_SIZE (64*1024*1024)

/**
 * Opens H264 file at path @path and returns opaque reader pointer
 */
h264_reader_t
h264_reader_open(const char *path)
{
    h264_reader_t reader = malloc(sizeof(struct h264_reader));
    ssize_t bytes;

    reader->fd = open(path, O_RDONLY);
    if (reader->fd < 0) {
        free(reader);
        return (NULL);
    }

    reader->size = DEFAULT_BUFFER_SIZE;
    reader->buffer = malloc(reader->size);
    if (reader->buffer == NULL) {
        free(reader);
        return (NULL);
    }

    /* Pre-fill the buffer */
    bytes = read(reader->fd, reader->buffer, reader->size);
    if (bytes < 0) {
        free(reader->buffer);
        free(reader);
        return (NULL);
    }

    reader->pos = 0;
    reader->end = bytes;
    reader->eof = (bytes == 0);

    return (reader);
}

int
h264_read_nal(h264_reader_t reader, h264_nal_t *nalp)
{
    size_t size;
    ssize_t toread, bytes;

    if (nalp == NULL)
        return (EINVAL);

    *nalp = NULL;

    h264_nal_t nal = malloc(sizeof(struct h264_nal));
    if (nal == NULL)
        return (ENOMEM);

    nal->size = 0;
    nal->data = NULL;

    off_t start = reader->pos;

    /*
     * Not enough for full NAL
     */
    if (reader->end < 4)
        return (EINVAL);

    if ((reader->buffer[start] != 0)
            || (reader->buffer[start+1] != 0)
            || (reader->buffer[start+2] != 0)
            || (reader->buffer[start+3] != 1))
    {
        free(nal);
        return (EINVAL);
    }

    do {
        /* We should be at the NAL start */
        start = reader->pos;
        off_t next_nal = -1;

        /* Check if NAL ends in this buffer */
        start += 4;
        while (start < reader->end - 4) {
            if ((reader->buffer[start] == 0)
                    && (reader->buffer[start+1] == 0)
                    && (reader->buffer[start+2] == 0)
                    && (reader->buffer[start+3] == 1))
            {
                next_nal = start;
                break;
            }
            start++;
        }

        /*
         * The temporary buffer contains whole NAL, copy it,
         * update pointers and return
         */
        if (next_nal != -1) {
            nal->size = next_nal - reader->pos;
            nal->data = malloc(nal->size);
            if (nal->data == NULL) {
                free(nal);
                return (ENOMEM);
            }
            memcpy(nal->data, reader->buffer + reader->pos, nal->size);
            reader->pos = next_nal;
            break; /* Done searching for NAL */
        }

        /* If there is nothing to read any more, use up whole buffer */
        if (reader->eof)
            start = reader->end;

        /* 
         * Only part of the NAL is in the current buffer copy it and
         * read more data from the file
         */
        off_t new_size = nal->size + start - reader->pos;
        if (nal->data) {
            nal->data = realloc(nal->data, new_size);
            if (nal->data == NULL) {
                free(nal);
                return (ENOMEM);
            }
            memcpy(nal->data + nal->size, reader->buffer + reader->pos,
                    new_size - nal->size);
        }
        else {
            nal->data = malloc(new_size);
            if (nal->data == NULL) {
                free(nal);
                return (ENOMEM);
            }
            memcpy(nal->data, reader->buffer + reader->pos, new_size);
        }
        nal->size = new_size;

        /*
         * copy last three bytes (if available) to the beginning of the
         * buffer and read more data unless it's EOF
         */
        if (reader->end - start)
            memmove(reader->buffer, reader->buffer + start, reader->end - start);

        if (!reader->eof) {
            reader->pos = 0;
            reader->end = reader->end - start;
            bytes = read(reader->fd, reader->buffer + reader->end, reader->size - reader->end);
            if (bytes > 0)
                reader->end += bytes;
            else if (bytes == 0)
                reader->eof = true;
        }
        else
            break; /* Done searching for NAL */
    } while (1);

    *nalp = nal;

    return (0);
}

void
h264_free_nal(h264_nal_t nal)
{
    if (nal == NULL)
        return;

    if (nal->data)
        free(nal->data);
    nal->data = NULL;
    nal->size = 0;
    free(nal);
}
