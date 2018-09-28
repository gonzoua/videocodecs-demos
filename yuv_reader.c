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
#include <string.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdint.h>

#include "yuv_reader.h"

#define DEFAULT_PLANE_ALIGNMENT	16
#define ALIGN_TO(ptr, alignment) (((intptr_t)(ptr) + (alignment) - 1) & ~((alignment) - 1))

yuv_reader_t
yuv_reader_open(const char *path, int width, int height)
{
    yuv_reader_t reader = malloc(sizeof(struct yuv_reader));

    reader->fd = open(path, O_RDONLY);
    if (reader->fd < 0) {
        free(reader);
        return (NULL);
    }

    reader->width = width;
    reader->height = height;

    return (reader);
}

int
yuv_read_frame(yuv_reader_t reader, yuv_frame_t frame)
{
    ssize_t bytes;

    /*
     * This is not very robust but good enough for demo
     */
    bytes = read(reader->fd, frame->Y, frame->Ysize);
    if (bytes <= 0)
        return (-1);
    bytes = read(reader->fd, frame->U, frame->Usize);
    if (bytes <= 0)
        return (-1);
    bytes = read(reader->fd, frame->V, frame->Vsize);
    if (bytes <= 0)
        return (-1);

    return (0);
}

yuv_frame_t
yuv_alloc_frame(yuv_reader_t reader)
{
	yuv_frame_t frame = malloc(sizeof(struct yuv_frame));
	size_t Ysize = reader->width*reader->height;
	size_t UVsize = reader->width*reader->height/4;

	/*
	 * Encoder might have alignment requirements for memory addresses
	 * use default one for now
	 */
	frame->Yptr = (uint8_t*)malloc(Ysize + DEFAULT_PLANE_ALIGNMENT);
	if (!frame->Yptr) {
        free(frame);
        return (NULL);
    }
	frame->Y = (uint8_t*)ALIGN_TO(frame->Yptr, DEFAULT_PLANE_ALIGNMENT);
	frame->Ysize = Ysize;

	frame->Uptr = (uint8_t*)malloc(UVsize + DEFAULT_PLANE_ALIGNMENT);
	if (!frame->Uptr) {
        free(frame->Yptr);
        free(frame);
        return (NULL);
    }
	frame->Usize = UVsize;
	frame->U = (uint8_t*)ALIGN_TO(frame->Uptr, DEFAULT_PLANE_ALIGNMENT);

	frame->Vptr = (uint8_t*)malloc(UVsize + DEFAULT_PLANE_ALIGNMENT);
	if (!frame->Vptr) {
        free(frame->Yptr);
        free(frame->Vptr);
        free(frame);
        return (NULL);
    }
	frame->Vsize = UVsize;
	frame->V = (uint8_t*)ALIGN_TO(frame->Vptr, DEFAULT_PLANE_ALIGNMENT);

	return (frame);
}

void yuv_free_frame(yuv_frame_t frame)
{
	free(frame->Yptr);
	free(frame->Uptr);
	free(frame->Vptr);
	free(frame);
}
