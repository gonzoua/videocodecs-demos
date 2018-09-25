#include <stdio.h>
#include <stdlib.h>

#include "yuv_reader.h"
#include "h264_encoder_mpp.h"

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
    h264_encoder_mpp_t encoder;
    int width, height;

    if (argc != 3)
        usage(argv[0]);

    width = 1980;
    height = 1072;

    yuv = yuv_reader_open(argv[1], 1920, 1072);
    if (yuv == NULL) {
        fprintf(stderr, "failed to open input file %s\n", argv[1]);
        exit(1);
    }

    frame = yuv_alloc_frame(yuv);
    encoder = mpp_h264_create_encoder(width, height);
    if (encoder == NULL) {
        fprintf(stderr, "failed to create H264 encoder\n");
        exit(1);
    }

    while (yuv_read_frame(yuv, frame) == 0) {
        mpp_h264_encode_frame(encoder, frame, 0);
    }

    /* generate EOS */
    mpp_h264_encode_frame(encoder, frame, 1);

    yuv_free_frame(frame);
    mpp_h264_destroy_encoder(encoder);

    return 0;
}
