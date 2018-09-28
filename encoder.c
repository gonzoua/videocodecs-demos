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
#include <unistd.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>

#include "yuv_reader.h"
#include "h264_encoder_mpp.h"

/*
 * Argument for encoder callback
 */
struct h264_writer
{
    int fd;
};

/*
 * Called for every encoded packet. Writes h264 bitstream
 * to the output file
 */
void h264_writer_callback(void *ptr, uint8_t *data, ssize_t len)
{
    ssize_t bytes, total;

    struct h264_writer *writer = (struct h264_writer *)ptr;
    total = bytes = 0;
    while (total < len) {
        bytes = write(writer->fd, data + total, len - total);
        if (bytes < 0) {
            if (errno != EAGAIN)
                break;
        }
        else
            total += bytes;
    }
}

void
usage(const char *exe)
{
    fprintf(stderr, "Usage: %s [-w width] [-h height] in.yuv out.yuv\n", exe);
    exit(1);
}

int
main(int argc, char * const*argv)
{
    yuv_reader_t yuv;
    yuv_frame_t frame;
    struct h264_encoder_mpp *encoder;
    int width, height;
    struct h264_writer *writer;
    const char *exe;
    int ch;

    exe = argv[0];

    width = 1920;
    height = 1080;

    while ((ch = getopt(argc, argv, "h:w:")) != -1) {
        switch (ch) {
            case 'w':
                     width = atoi(optarg);
                     break;
            case 'h':
                     height = atoi(optarg);
                     break;
             case '?':
             default:
                     usage(exe);
             }
     }

     argc -= optind;
     argv += optind;

    if (argc != 2)
        usage(exe);

    fprintf(stderr, "Input resolution: %dx%d\n", width, height);

    writer = (struct h264_writer *)malloc(sizeof(struct h264_writer));
    if (writer == NULL) {
        fprintf(stderr, "failed to allocate H264 writer context\n");
        exit(1);
    }

    writer->fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (writer->fd < 0) {
        fprintf(stderr, "failed to open '%s' for writing: %s\n", argv[1], strerror(errno));
        exit(1);
    }

    yuv = yuv_reader_open(argv[0], width, height);
    if (yuv == NULL) {
        fprintf(stderr, "failed to open input file %s\n", argv[0]);
        exit(1);
    }

    frame = yuv_alloc_frame(yuv);
    encoder = h264_mpp_encoder_create(width, height, h264_writer_callback, writer);
    if (encoder == NULL) {
        fprintf(stderr, "failed to create H264 encoder\n");
        exit(1);
    }

    while (yuv_read_frame(yuv, frame) == 0) {
        h264_mpp_encoder_submit_frame(encoder, frame, 0);
    }

    /* Generate EOS packet */
    h264_mpp_encoder_submit_frame(encoder, frame, 1);

    /* Cleanup encoder things */
    yuv_free_frame(frame);
    h264_mpp_encoder_destroy(encoder);

    close(writer->fd);
    free(writer);

    return 0;
}
