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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#include "h264_decoder_mpp.h"

/*
 * Context for writer callback
 */
struct frame_writer
{
    int fd;
};

/*
 * Writes @len bytes of buffer @data ensuring that all of them are written
 */
static int
write_buffer(int fd, uint8_t *data, ssize_t len)
{
    ssize_t bytes, total;

    total = bytes = 0;
    while (total < len) {
        bytes = write(fd, data + total, len - total);
        if (bytes < 0) {
            fprintf(stderr, "failed to write data in write_buffer: %s\n", strerror(errno));
            return (-1);
        }
        else
            total += bytes;
    }

    return (0);
}

/*
 * Called for every decoded frame. The frame format is NV12
 */
void
frame_writer_callback(void *ptr, uint8_t *yplane, uint8_t *uvplane,
    int width, int height, int h_stride, int v_stride)
{
    struct frame_writer *writer = (struct frame_writer *)ptr;

    /* Y plane */
    for (int i = 0; i < height; i++) {
        if (write_buffer(writer->fd, yplane + i*h_stride, width) < 0)
            return;
    }

    /* UV plane */
    for (int i = 0; i < height/2; i++) {
        if (write_buffer(writer->fd, uvplane + i*h_stride, width) < 0)
            return;
    }
}

void
usage(const char *exe)
{
    fprintf(stderr, "Usage: %s in.h264 out.nv12\n", exe);
    exit(1);
}

int
main(int argc, const char *argv[])
{
    struct h264_decoder_mpp *decoder;
    struct frame_writer *writer;
    uint8_t *buf;
    int buf_size;
    ssize_t bytes;
    int fd;

    if (argc != 3)
        usage(argv[0]);

    /*
     * Open input (h264) file
     */
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "failed to open input file %s: %s\n", argv[1], strerror(errno));
        exit(1);
    }

    /*
     * Create decoder callback context
     */
    writer = (struct frame_writer *)malloc(sizeof(struct frame_writer));
    if (writer == NULL) {
        fprintf(stderr, "failed to allocate frame writer context\n");
        exit(1);
    }

    /*
     * Open output (raw) file and 
     */
    writer->fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (writer->fd < 0) {
        fprintf(stderr, "failed to open '%s' for writing: %s\n", argv[2], strerror(errno));
        exit(1);
    }

    /*
     * Create H264 decoder
     */
    decoder = h264_mpp_decoder_create(frame_writer_callback, writer);
    if (decoder == NULL) {
        fprintf(stderr, "failed to create H264 decoder\n");
        exit(1);
    }

    /*
     * Create a buffer to read H264 bitstream into
     */
    buf_size = 4*1024;;
    buf = malloc(buf_size);

    int ready_for_new_buffer = 1;
    while (1) {
        /*
         * Load new chunk of the bitstream if the decoder is ready for it
         */
        if (ready_for_new_buffer) {
            bytes = read(fd, buf, buf_size);
            /*
             * EOF or error, stop processing
             */
            if (bytes <= 0)
                break;
        } else
            usleep(3000);

        /*
         * Feed bitstrem to decoder until it's full
         */
        if (h264_decoder_mpp_submit_packet(decoder, buf, bytes) == EAGAIN)
            ready_for_new_buffer = 0;
        else
            ready_for_new_buffer = 1;

        /*
         * Check if there are frames in output buffers. If there are any
         * they'll be handled in decoder callback
         */
        h264_decoder_mpp_get_frame(decoder);
    }

    /*
     * Clean-up after ourselves
     */
    h264_decoder_mpp_destroy(decoder);
    close(writer->fd);
    free(writer);

    return 0;
}
