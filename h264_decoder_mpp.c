#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/errno.h>

#include "rockchip/rk_mpi.h"
#include "rockchip/mpp_buffer.h"
#include "rockchip/mpp_frame.h"
#include "rockchip/mpp_packet.h"

#include "yuv_reader.h"
#include "h264_decoder_mpp.h"

#define BUF_SIZE (1024*1024)
#define MPP_H264_DECODE_TIMEOUT 3

#define UP_TO_16(x) (((x) + 15) & ~0xf)
#define WIDTH 1920
#define HEIGHT 1080
#define SIZE (UP_TO_16(WIDTH)*UP_TO_16(HEIGHT)*3/2)

#define msleep(x) usleep((x)*1000)

#define MPP_MAX_BUFFERS                 4

struct h264_decoder_mpp {
    decoder_callback_t  callback;
    void                *arg;

    MppCodingType       type;
    MppCtx              ctx;
    MppApi              *mpi;

    MppBufferGroup      frame_group;
    char                *buf;
    char                *packet_buf;
    size_t              packet_size;
    MppPacket           packet;
};

h264_decoder_mpp_t
mpp_h264_create_decoder(decoder_callback_t callback, void *arg)
{
    struct h264_decoder_mpp *decoder;
    decoder = malloc(sizeof(struct h264_decoder_mpp));

    decoder->type = MPP_VIDEO_CodingAVC;
    MPP_RET ret = MPP_OK;
    ret = mpp_create(&decoder->ctx, &decoder->mpi);
    if (MPP_OK != ret) {
        fprintf(stderr, "mpp_create failed\n");
        free(decoder);
        return NULL;
    }

    decoder->callback = callback;
    decoder->arg = arg;

    int need_split = 1;
    ret = decoder->mpi->control(decoder->ctx, MPP_DEC_SET_PARSER_SPLIT_MODE, &need_split);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpi->control(MPP_DEC_SET_PARSER_SPLIT_MODE) failed\n");
        mpp_destroy(decoder->ctx);
        free(decoder);
        return NULL;
    }

    ret = mpp_init(decoder->ctx, MPP_CTX_DEC, decoder->type);
    if (MPP_OK != ret) {
        fprintf(stderr, "mpp_init failed\n");
        mpp_destroy(decoder->ctx);
        free(decoder);
        return NULL;
    }


    decoder->packet_size = SZ_4K;
    decoder->buf = malloc(decoder->packet_size + 32);
    if (decoder->buf == NULL) {
        fprintf(stderr, "malloc failed\n");
        mpp_destroy(decoder->ctx);
        free(decoder);
        return NULL;
    }
    decoder->packet_buf = (char*)(((intptr_t)decoder->buf + 31) & ~31);

    fprintf(stderr, "data: %p(%p), size: %d\n", decoder->packet_buf, decoder->buf, decoder->packet_size);
    ret = mpp_packet_init(&decoder->packet, decoder->packet_buf, decoder->packet_size);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_packet_init failed\n");
        free(decoder->buf);
        mpp_destroy(decoder->ctx);
        free(decoder);
        return NULL;
    }
    fprintf(stderr, "packet data: %p\n", mpp_packet_get_data(decoder->packet));

    decoder->frame_group = NULL;

    return (decoder);
}

int
mpp_h264_destroy_decoder(h264_decoder_mpp_t decoder)
{
    MPP_RET ret;
    int result;

    if (decoder->frame_group) {
        mpp_buffer_group_put(decoder->frame_group);
        decoder->frame_group = NULL;
    }

    if (decoder->packet) {
        mpp_packet_deinit(&decoder->packet);
        decoder->packet = NULL;
    }

    if (decoder->buf) {
        free(decoder->buf);
        decoder->buf = NULL;
        decoder->packet_buf = NULL;
    }

    ret = mpp_destroy(decoder->ctx);
    if (MPP_OK != ret) {
        fprintf(stderr, "mpp_destroy failed\n");
        return -1;
    }

    free(decoder);

    return 0;
}

int
mpp_h264_decode_packet(h264_decoder_mpp_t decoder, char *data, ssize_t len)
{
    MPP_RET ret;

    if (data) {
        static int pts = 0;
        mpp_packet_set_pts(decoder->packet, pts++);
        mpp_packet_write(decoder->packet, 0, data, len);
        // reset pos and set valid length
        mpp_packet_set_pos(decoder->packet, decoder->packet_buf);
        mpp_packet_set_length(decoder->packet, len);
    }
    else
        mpp_packet_set_eos(decoder->packet);
    
retry:
    ret = decoder->mpi->decode_put_packet(decoder->ctx, decoder->packet);
    if (ret != MPP_OK) {
        if (ret == MPP_ERR_BUFFER_FULL) {
            /* let caller get frames and re-submit packet */
            return (EAGAIN);
        }
        else
            fprintf(stderr, "decode_put_packet failed: %d\n", ret);
        // exit(1);
        // return (-1);
    }

    return (0);
}


int
mpp_h264_get_frame(h264_decoder_mpp_t decoder)
{
    MPP_RET ret;
    MppFrame frame;

    do {
        int get_frm = 0;
        int frm_eos = 0;
        int times = 5;

    try_again:
        ret = decoder->mpi->decode_get_frame(decoder->ctx, &frame);
        if (ret == MPP_ERR_TIMEOUT) {
            if (times > 0) {
                times--;
                usleep(3000);
                goto try_again;
            }
            fprintf(stderr, "decode_get_frame failed too much time\n");
        }

        if (ret != MPP_OK) {
            fprintf(stderr, "decode_get_frame failed ret %d\n", ret);
            break;
        }

        if (!frame)
            break;

        if (mpp_frame_get_info_change(frame)) {
            unsigned int width = mpp_frame_get_width(frame);
            unsigned int height = mpp_frame_get_height(frame);
            unsigned int hor_stride = mpp_frame_get_hor_stride(frame);
            unsigned int ver_stride = mpp_frame_get_ver_stride(frame);
            unsigned int buffer_size = hor_stride * ver_stride * 2;

            fprintf(stderr, "decode_get_frame get info changed found\n");
            fprintf(stderr, "decoder require buffer w:h [%d:%d] stride [%d:%d] buffer_size %d\n",
                    width, height, hor_stride, ver_stride, buffer_size);

            if (decoder->frame_group == NULL) {
                fprintf(stderr, "!!!! INIT FRAME GROUP!!!\n");
                ret = mpp_buffer_group_get_internal(&decoder->frame_group, MPP_BUFFER_TYPE_DRM);
                if (ret) {
                    fprintf(stderr, "get mpp buffer group  failed ret %d\n", ret);
                    break;
                }

                ret = decoder->mpi->control(decoder->ctx, MPP_DEC_SET_EXT_BUF_GROUP, decoder->frame_group);
                if (ret) {
                    fprintf(stderr, "set buffer group failed ret %d\n", ret);
                    break;
                }
            }
            else {
                ret = mpp_buffer_group_clear(decoder->frame_group);
                if (ret != MPP_OK) {
                    fprintf(stderr, "clear buffer group failed ret %d\n", ret);
                    break;
                }
            }

            /* Use limit config to limit buffer count to 24 with buffer_size */
            ret = mpp_buffer_group_limit_config(decoder->frame_group, buffer_size, 24);
            if (ret) {
                fprintf(stderr, "limit buffer group failed ret %d\n", ret);
                break;
            }

            decoder->mpi->control(decoder->ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
        } else {
            int err_info = mpp_frame_get_errinfo(frame) | mpp_frame_get_discard(frame);
            if (err_info) {
                fprintf(stderr, "decoder_get_frame get err info:%d discard:%d.\n",
                        mpp_frame_get_errinfo(frame), mpp_frame_get_discard(frame));
            }
            else {
                /** Got a frame */
                // fprintf(stderr, "frame out\n");
                unsigned int h_stride = mpp_frame_get_hor_stride(frame);
                unsigned int v_stride = mpp_frame_get_ver_stride(frame);
                MppBuffer mpp_buf = mpp_frame_get_buffer(frame);
                MppFrameFormat fmt = mpp_frame_get_fmt(frame);
                char *buf  = mpp_buffer_get_ptr(mpp_buf);
                size_t size = mpp_buffer_get_size(mpp_buf);
                decoder->callback(decoder->arg, buf, h_stride*v_stride);
                buf += h_stride*v_stride;
                decoder->callback(decoder->arg, buf, h_stride*v_stride/2);
            }
        }

        frm_eos = mpp_frame_get_eos(frame);
        mpp_frame_deinit(&frame);
        frame = NULL;
        get_frm = 1;

        // if last packet is send but last frame is not found continue
        if (!frm_eos) {
            //msleep(MPP_H264_DECODE_TIMEOUT);
            continue;
        }

        if (frm_eos) {
            fprintf(stderr, "found last frame\n");
            break;
        }

        if (!get_frm)
            break;

        usleep(3000);
    } while (1);

    return (0);
}
