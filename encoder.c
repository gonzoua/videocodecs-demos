#include <stdio.h>
#include <stdlib.h>

#include "yuv_reader.h"

void
usage(const char *exe)
{
    fprintf(stderr, "Usage: %s in.yuv out.yuv\n", exe);
    exit(1);
}

int
main(int argc, const char *argv[])
{
    yuv_reader_t yuv;
    yuv_frame_t frame;

    if (argc != 3)
        usage(argv[0]);

    yuv = yuv_reader_open(argv[1], 1920, 1072);
    if (yuv == NULL) {
        fprintf(stderr, "failed to open input file %s\n", argv[1]);
        exit(1);
    }

    frame = yuv_alloc_frame(yuv);

    while (yuv_read_frame(yuv, frame) == 0) {
    }

    yuv_free_frame(frame);

    return 0;
}
