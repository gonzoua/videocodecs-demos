#include <stdio.h>
#include <stdlib.h>

#include "h264_reader.h"

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

    if (argc != 3)
        usage(argv[0]);

    h264 = h264_reader_open(argv[1]);
    if (h264 == NULL) {
        fprintf(stderr, "failed to open input file %s\n", argv[1]);
    }

    while (h264_read_nal(h264, &nal) == 0) {
        printf("NAL size: %zd\n", nal->size);
        h264_free_nal(nal);
    }

    return 0;
}
