#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "h264_reader.h"
#include "h264_decoder_mpp.h"

struct frame_writer
{
    int fd;
};

void frame_writer_callback(void *ptr, char *data, ssize_t len)
{
    ssize_t bytes, total;

    struct frame_writer *writer = (struct frame_writer *)ptr;
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
    fprintf(stderr, "Usage: %s in.h264 out.raw\n", exe);
    exit(1);
}

int
main(int argc, const char *argv[])
{
    h264_reader_t h264;
    h264_nal_t nal;
    h264_decoder_mpp_t decoder;
    struct frame_writer *writer;
    char *buf;
    int buf_size;
    ssize_t bytes;
    int fd;

    if (argc != 3)
        usage(argv[0]);

#if 0
    h264 = h264_reader_open(argv[1]);
    if (h264 == NULL) {
        fprintf(stderr, "failed to open input file %s\n", argv[1]);
        exit(1);
    }
#endif
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "failed to open input file %s: %s\n", argv[1], strerror(errno));
        exit(1);
    }

    writer = (struct frame_writer *)malloc(sizeof(struct frame_writer));
    if (writer == NULL) {
        fprintf(stderr, "failed to allocate frame writer context\n");
        exit(1);
    }

    writer->fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (writer->fd < 0) {
        fprintf(stderr, "failed to open '%s' for writing: %s\n", argv[2], strerror(errno));
        exit(1);
    }

    buf_size = 4*1024;;
    buf = malloc(buf_size);

    decoder = mpp_h264_create_decoder(frame_writer_callback, writer);
    if (decoder == NULL) {
        fprintf(stderr, "failed to create H264 decoder\n");
        exit(1);
    }

    int new_buffer = 1;
    while (1) {
        if (new_buffer)
            bytes = read(fd, buf, buf_size);
        else
            usleep(3000);
        if (bytes == 0)
            break;

        if (mpp_h264_decode_packet(decoder, buf, bytes) == EAGAIN)
            new_buffer = 0;
        else
            new_buffer = 1;
        mpp_h264_get_frame(decoder);
    }

    fprintf(stderr, "winding down\n");


#if 0
    while (h264_read_nal(h264, &nal) == 0) {
        mpp_h264_decode_packet(decoder, nal->data, nal->size);
        int ret = mpp_h264_get_frame(decoder);
        if (ret == EAGAIN) {
            fprintf(stderr, "re-submitting packet\n");
            mpp_h264_decode_packet(decoder, nal->data, nal->size);
            mpp_h264_get_frame(decoder);
        }
        h264_free_nal(nal);
        usleep(3000);
    }
#endif

    mpp_h264_destroy_decoder(decoder);

    return 0;
}
