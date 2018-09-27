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

#define H264_DECODER_ALIGNMENT 32
#define ALIGN_TO(ptr, alignment) (((intptr_t)(ptr) + (alignment) - 1) & ~((alignment) - 1))

struct h264_decoder_mpp {
    /*
     * Per-frame callback and its argument
     */
    decoder_callback_t  callback;
    void                *arg;

    MppCtx              ctx;
    MppApi              *mpi;

    MppBufferGroup      frame_group;
    /* Original pointer saved for free(3) */
    char                *buf;
    /* Aligned pointer (buf) for packet initialization */
    char                *packet_buf;
    size_t              packet_size;
    MppPacket           packet;
};

/*
 * Create decoder context
 */
h264_decoder_mpp_t
mpp_h264_create_decoder(decoder_callback_t callback, void *arg)
{
    struct h264_decoder_mpp *decoder;
    decoder = malloc(sizeof(struct h264_decoder_mpp));

    MPP_RET ret = MPP_OK;
    ret = mpp_create(&decoder->ctx, &decoder->mpi);
    if (MPP_OK != ret) {
        fprintf(stderr, "mpp_create failed\n");
        free(decoder);
        return NULL;
    }

    decoder->callback = callback;
    decoder->arg = arg;

    /*
     * Setup "Split standard mode". Don't know what it means yet
     * but should be set before calling mpp_init
     */
    int need_split = 1;
    ret = decoder->mpi->control(decoder->ctx, MPP_DEC_SET_PARSER_SPLIT_MODE, &need_split);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpi->control(MPP_DEC_SET_PARSER_SPLIT_MODE) failed\n");
        mpp_destroy(decoder->ctx);
        free(decoder);
        return NULL;
    }

    ret = mpp_init(decoder->ctx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC);
    if (MPP_OK != ret) {
        fprintf(stderr, "mpp_init failed\n");
        mpp_destroy(decoder->ctx);
        free(decoder);
        return NULL;
    }


    /*
     * Prepare packet object to write bitstream to and submit to decoder
     * packet buffer should be aligned at 32 bytes boundary
     */

    /* Keep original pointer to call with free(3) later */
    decoder->packet_size = SZ_4K;
    decoder->buf = malloc(decoder->packet_size + H264_DECODER_ALIGNMENT);
    if (decoder->buf == NULL) {
        fprintf(stderr, "malloc failed\n");
        mpp_destroy(decoder->ctx);
        free(decoder);
        return NULL;
    }
    decoder->packet_buf = (char*)ALIGN_TO(decoder->buf, H264_DECODER_ALIGNMENT);
    fprintf(stderr, "buf=%p packet_buf=%p\n", decoder->buf, decoder->packet_buf);
    ret = mpp_packet_init(&decoder->packet, decoder->packet_buf, decoder->packet_size);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_packet_init failed\n");
        free(decoder->buf);
        mpp_destroy(decoder->ctx);
        free(decoder);
        return NULL;
    }

    /*
     * Initialized later, when there is information about frame dimensions
     */
    decoder->frame_group = NULL;

    return (decoder);
}

/*
 * Cleanup decoder context
 */
int
mpp_h264_destroy_decoder(h264_decoder_mpp_t decoder)
{
    MPP_RET ret;
    int result;

    ret = decoder->mpi->reset(decoder->ctx);
    if (ret)
        fprintf(stderr, "reset failed ret %d\n", ret);

    if (decoder->frame_group) {
        mpp_buffer_group_clear(decoder->frame_group);
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

/*
 * Submit chunk of H264 bitstream to the decoder
 * returns:
 *   0 if data was submitted
 *   EAGAIN if the decoder buffer is full
 *   -1 if there is an error 
 */
int
mpp_h264_decoder_submit_data(h264_decoder_mpp_t decoder, char *data, ssize_t len)
{
    MPP_RET ret;

    /* Copy data to the internal buffer */
    mpp_packet_write(decoder->packet, 0, data, len);
    /* Reset position to the start of the buffer */
    mpp_packet_set_pos(decoder->packet, decoder->packet_buf);
    /* Set packet length */
    mpp_packet_set_length(decoder->packet, len);

    /*
     * For files it's possible to pass EOS flag by calling mpp_packet_set_eos
     * EOS will propogate along with the decoded frame where it can be checked 
     * using mpp_frame_get_eos
     */
    ret = decoder->mpi->decode_put_packet(decoder->ctx, decoder->packet);
    if (ret != MPP_OK) {
        if (ret == MPP_ERR_BUFFER_FULL) {
            /* 
             * Buffer is full at the moment. Caller should wait, check
             * available decoded frames and re-submit data later.
             */
            return (EAGAIN);
        }
        else {
            fprintf(stderr, "decode_put_packet failed: %d\n", ret);
            return (-1);
        }
    }

    return (0);
}


int
mpp_h264_get_frame(h264_decoder_mpp_t decoder)
{
    MPP_RET ret;
    MppFrame frame;

    ret = decoder->mpi->decode_get_frame(decoder->ctx, &frame);
    if (ret == MPP_ERR_TIMEOUT)
        return (EAGAIN);

    if (ret != MPP_OK){
        fprintf(stderr, "decode_get_frame failed ret %d\n", ret);
        return (-1);
    }

    if (!frame)
        return (0);

    if (mpp_frame_get_info_change(frame)) {
        unsigned int width = mpp_frame_get_width(frame);
        unsigned int height = mpp_frame_get_height(frame);
        unsigned int hor_stride = mpp_frame_get_hor_stride(frame);
        unsigned int ver_stride = mpp_frame_get_ver_stride(frame);

        /* NV12 is W*H*3/2, but to keep it safe use larger buffer */
        unsigned int buffer_size = hor_stride * ver_stride * 2;

        fprintf(stderr, "decode_get_frame get info changed found\n");
        fprintf(stderr, "decoder require buffer w:h [%d:%d] stride [%d:%d] buffer_size %d\n",
                width, height, hor_stride, ver_stride, buffer_size);

        if (decoder->frame_group == NULL) {
            ret = mpp_buffer_group_get_internal(&decoder->frame_group, MPP_BUFFER_TYPE_DRM);
            if (ret) {
                fprintf(stderr, "mpp_buffer_group_get_internal failed ret %d\n", ret);
                mpp_frame_deinit(&frame);
                return (-1);
            }

            ret = decoder->mpi->control(decoder->ctx, MPP_DEC_SET_EXT_BUF_GROUP, decoder->frame_group);
            if (ret) {
                fprintf(stderr, "MPP_DEC_SET_EXT_BUF_GROUP failed ret %d\n", ret);
                mpp_frame_deinit(&frame);
                return (-1);
            }
        }
        else {
            /*
             * This is probably due to PPS/SPS in H264, can't handle resultion change for now
             */
            fprintf(stderr, "frame group is initialized, can't handle resolution change\n");
            mpp_frame_deinit(&frame);
            return (-1);
        }

        /* Configure group memory limit: 24 buffers */
        ret = mpp_buffer_group_limit_config(decoder->frame_group, buffer_size, 24);
        if (ret) {
            fprintf(stderr, "mpp_buffer_group_limit_config failed ret %d\n", ret);
            mpp_frame_deinit(&frame);
            return (-1);
        }

        /* Submit the change */
        decoder->mpi->control(decoder->ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
    } else {
        /* Is it erroneous frame? */
        int err_info = mpp_frame_get_errinfo(frame) | mpp_frame_get_discard(frame);
        if (err_info) {
            /* Yes, just drop this frame */
            fprintf(stderr, "decoder_get_frame get err info:%d discard:%d.\n",
                    mpp_frame_get_errinfo(frame), mpp_frame_get_discard(frame));
        }
        else {
            /* valid frame, submit to callback */
            int width = mpp_frame_get_width(frame);
            int height = mpp_frame_get_height(frame);
            int h_stride = mpp_frame_get_hor_stride(frame);
            int v_stride = mpp_frame_get_ver_stride(frame);

            MppBuffer mpp_buf = mpp_frame_get_buffer(frame);
            MppFrameFormat fmt = mpp_frame_get_fmt(frame);
            if (fmt == MPP_FMT_YUV420SP) {
                char *yplane = mpp_buffer_get_ptr(mpp_buf);
                char *uvplane = yplane + h_stride*v_stride;

                decoder->callback(decoder->arg, yplane, uvplane, width, height,
                    h_stride, v_stride);
            }
            else {
                fprintf(stderr, "decoder_get_frame get err info:%d discard:%d.\n",
                    mpp_frame_get_errinfo(frame), mpp_frame_get_discard(frame));
            }
        }
    }

    /*
     * Here mpp_frame_get_eos can be used to check if it's the last
     * frame in the decoded stream. Also see mpp_packet_set_eos
     */

    /* release frame */
    mpp_frame_deinit(&frame);

    return (0);
}
