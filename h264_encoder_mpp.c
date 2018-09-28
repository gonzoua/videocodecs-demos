#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "rockchip/rk_mpi.h"
#include "rockchip/mpp_buffer.h"
#include "rockchip/mpp_frame.h"
#include "rockchip/mpp_packet.h"

#include "yuv_reader.h"
#include "h264_encoder_mpp.h"

/*
 * width/height has to be aligned by 16. MPP 20171218 assumes
 * that alignment by 8 is enough but iommu on my RK3399 crashes
 * for 8 but not for 16
 */
#define UP_TO_16(x) (((x) + 0xf) & ~0xf)
#define MPP_MAX_BUFFERS                 4

struct h264_encoder_mpp {
    int                 width;
    int                 height;
    int                 h_stride;
    int                 v_stride;

    encoder_callback_t  callback;
    void                *arg;

    MppCtx              ctx;
    MppApi              *mpi;

    MppBufferGroup      input_group;
    MppBufferGroup      output_group;
    MppBuffer           input_buffer[MPP_MAX_BUFFERS];
    MppBuffer           output_buffer[MPP_MAX_BUFFERS];
    MppFrame            mpp_frame;
    MppPacket           sps_packet;

    int                 current_index;
};

static int
mpp_h264_setup_format(struct h264_encoder_mpp *encoder)
{
    MppEncPrepCfg prep_cfg;
    memset (&prep_cfg, 0, sizeof (prep_cfg));
    prep_cfg.change = MPP_ENC_PREP_CFG_CHANGE_INPUT |
            MPP_ENC_PREP_CFG_CHANGE_FORMAT;
    prep_cfg.width = encoder->width;
    prep_cfg.height = encoder->height;
    prep_cfg.format = MPP_FMT_YUV420P;
    prep_cfg.hor_stride = UP_TO_16(encoder->width);
    prep_cfg.ver_stride = UP_TO_16(encoder->height);

    if (encoder->mpi->control(encoder->ctx, MPP_ENC_SET_PREP_CFG, &prep_cfg)) {
        fprintf (stderr, "Setting input format for rockchip mpp failed\n");
        return -1;
    }

    if (encoder->mpi->control(encoder->ctx, MPP_ENC_GET_EXTRA_INFO, &encoder->sps_packet))
        encoder->sps_packet = NULL;

    if (encoder->sps_packet) {
        void *sps_ptr = mpp_packet_get_pos(encoder->sps_packet);
        size_t sps_len = mpp_packet_get_length(encoder->sps_packet);
        encoder->callback(encoder->arg, sps_ptr, sps_len);
    }

    return (0);
}

static int
mpp_h264_alloc_frames(struct h264_encoder_mpp *encoder)
{
    /* Allocator buffers */
    if (mpp_buffer_group_get_internal(&encoder->input_group, MPP_BUFFER_TYPE_ION))
        goto failed;
    if (mpp_buffer_group_get_internal(&encoder->output_group, MPP_BUFFER_TYPE_ION))
        goto failed;

    for (int i = 0; i < MPP_MAX_BUFFERS; i++) {
        int frame_size = encoder->h_stride*encoder->v_stride*3/2;
        if (mpp_buffer_get(encoder->input_group, &encoder->input_buffer[i], frame_size))
            goto failed;
        /* 
         * More than enough to fit encoded frame. Should be significantly less
         */
        if (mpp_buffer_get(encoder->output_group, &encoder->output_buffer[i], encoder->width*encoder->height))
            goto failed;
    }

    encoder->current_index = 0;

    if (mpp_frame_init(&encoder->mpp_frame)) {
        fprintf (stderr, "failed to set up mpp frame\n");
        goto failed;
    }

    mpp_frame_set_width(encoder->mpp_frame, encoder->width);
    mpp_frame_set_height(encoder->mpp_frame, encoder->height);
    mpp_frame_set_hor_stride(encoder->mpp_frame, encoder->h_stride);
    mpp_frame_set_ver_stride(encoder->mpp_frame, encoder->v_stride);

    if (encoder->mpi->poll(encoder->ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK)) 
        fprintf (stderr, "mpp input poll failed");
    else
	    return 0;


failed:
	fprintf(stderr, "%s failed\n", __func__);
	return -1;
}

static void
mpp_h264_free_frames(struct h264_encoder_mpp *encoder)
{
    for (int i = 0; i < MPP_MAX_BUFFERS; i++) {
        if (encoder->input_buffer[i]) {
            mpp_buffer_put(encoder->input_buffer[i]);
            encoder->input_buffer[i] = NULL;
        }
        if (encoder->output_buffer[i]) {
            mpp_buffer_put(encoder->output_buffer[i]);
            encoder->output_buffer[i] = NULL;
        }
    }

    /* Must be destroy before input_group */
    if (encoder->mpp_frame) {
        mpp_frame_deinit(&encoder->mpp_frame);
        encoder->mpp_frame = NULL;
    }

    if (encoder->input_group) {
        mpp_buffer_group_put(encoder->input_group);
        encoder->input_group = NULL;
    }

    if (encoder->output_group) {
        mpp_buffer_group_put(encoder->output_group);
        encoder->output_group = NULL;
    }
}

h264_encoder_mpp_t
mpp_h264_create_encoder(int width, int height, encoder_callback_t callback, void *arg)
{
    struct h264_encoder_mpp *encoder;
    encoder = malloc(sizeof(struct h264_encoder_mpp));

    encoder->width = width;
    encoder->height = height;
    encoder->h_stride = UP_TO_16(width);
    encoder->v_stride = UP_TO_16(height);
    encoder->callback = callback;
    encoder->arg = arg;
    
    MPP_RET ret = MPP_OK;
    ret = mpp_create(&encoder->ctx, &encoder->mpi);
    if (MPP_OK != ret) {
        fprintf(stderr, "mpp_create failed\n");
        free(encoder);
        return NULL;
    }

    ret = mpp_init(encoder->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (MPP_OK != ret) {
        fprintf(stderr, "mpp_init failed\n");
        mpp_destroy(encoder->ctx);
        free(encoder);
        return NULL;
    }

	MppEncCodecCfg codec_cfg;
	MppEncRcCfg rc_cfg;

    memset (&rc_cfg, 0, sizeof (rc_cfg));
    memset (&codec_cfg, 0, sizeof (codec_cfg));

    rc_cfg.change = MPP_ENC_RC_CFG_CHANGE_ALL;
    /* quality control method: constan bit rate */
    rc_cfg.rc_mode = MPP_ENC_RC_MODE_CBR;
    rc_cfg.quality = MPP_ENC_RC_QUALITY_MEDIUM;

    /* 30 fps */
    rc_cfg.fps_in_flex = 0;
    rc_cfg.fps_in_num = 30;
    rc_cfg.fps_in_denorm = 1;
    rc_cfg.fps_out_flex = 0;
    rc_cfg.fps_out_num = 30;
    rc_cfg.fps_out_denorm = 1;
    rc_cfg.gop = 30;
    rc_cfg.skip_cnt = 0;

    codec_cfg.h264.qp_init = 26;

    /* CBR-specific setup */
    codec_cfg.h264.qp_max = 28;
    codec_cfg.h264.qp_min = 4;
    codec_cfg.h264.qp_max_step = 8;

    /* Bits of a GOP */
    rc_cfg.bps_target = 1024*1024; /* 1Mbit per second */
    rc_cfg.bps_max = rc_cfg.bps_target * 17 / 16;
    rc_cfg.bps_min = rc_cfg.bps_target * 15 / 16;

    if (encoder->mpi->control(encoder->ctx, MPP_ENC_SET_RC_CFG, &rc_cfg)) {
        fprintf (stderr, "Setting rate control for rockchip mpp failed\n");
        mpp_destroy(encoder->ctx);
        free(encoder);
        return NULL;
    }

    codec_cfg.coding = MPP_VIDEO_CodingAVC;
    codec_cfg.h264.change = MPP_ENC_H264_CFG_CHANGE_PROFILE |
            MPP_ENC_H264_CFG_CHANGE_ENTROPY |
            MPP_ENC_H264_CFG_CHANGE_TRANS_8x8 | MPP_ENC_H264_CFG_CHANGE_QP_LIMIT;
    codec_cfg.h264.profile = 100;
    codec_cfg.h264.level = 40;
    codec_cfg.h264.entropy_coding_mode = 1;
    codec_cfg.h264.cabac_init_idc = 0;
    codec_cfg.h264.transform8x8_mode = 1;

    if (encoder->mpi->control(encoder->ctx, MPP_ENC_SET_CODEC_CFG, &codec_cfg)) {
        fprintf (stderr, "Setting codec info for rockchip mpp failed\n");
        mpp_destroy(encoder->ctx);
        free(encoder);
        return NULL;
    }

    if (mpp_h264_setup_format(encoder) < 0) {
        mpp_destroy(encoder->ctx);
        free(encoder);
        return NULL;
    }

    if (mpp_h264_alloc_frames(encoder) < 0) {
        mpp_destroy(encoder->ctx);
        free(encoder);
        return NULL;
    }

    return (encoder);
}

int
mpp_h264_destroy_encoder(h264_encoder_mpp_t encoder)
{

    MPP_RET ret;

    mpp_h264_free_frames(encoder);

    ret = mpp_destroy(encoder->ctx);
    if (MPP_OK != ret) {
        fprintf(stderr, "mpp_destroy failed\n");
        return -1;
    }

    free(encoder);

    return 0;
}

int
mpp_h264_encode_frame(h264_encoder_mpp_t encoder, yuv_frame_t frame, int eos)
{
    MppTask task = NULL;
    MppBuffer frame_in = encoder->input_buffer[encoder->current_index];
    MppBuffer pkt_buf_out = encoder->output_buffer[encoder->current_index];
    MppPacket packet = NULL;
	void *ptr;
	int ret = 0;
    int frame_size;

    mpp_frame_set_buffer(encoder->mpp_frame, frame_in);

    /* Eos buffer */
    if (eos)
        mpp_frame_set_eos(encoder->mpp_frame, 1);
    else {
        ptr = mpp_buffer_get_ptr(frame_in);
        /* Y plane */
		memcpy(ptr, frame->Y, frame->Ysize);
        /* UV planes */
        frame_size = encoder->h_stride * encoder->v_stride;
		memcpy(ptr + frame_size, frame->U, frame->Usize);
		memcpy(ptr + frame_size + frame_size/4, frame->V, frame->Vsize);
        mpp_frame_set_eos(encoder->mpp_frame, 0);
    }

    do {
        if (encoder->mpi->dequeue(encoder->ctx, MPP_PORT_INPUT, &task)) {
            fprintf (stderr, "mpp task input dequeue failed\n");
            return -1;
        }
        if (NULL == task) {
            fprintf (stderr, "mpp input failed, try again\n");
            usleep (2);
        } else {
            break;
        }
    } while (1);
    mpp_task_meta_set_frame(task, KEY_INPUT_FRAME, encoder->mpp_frame);

    mpp_packet_init_with_buffer(&packet, pkt_buf_out);
    mpp_task_meta_set_packet(task, KEY_OUTPUT_PACKET, packet);

    if (encoder->mpi->enqueue(encoder->ctx, MPP_PORT_INPUT, task)) {
        fprintf (stderr, "mpp task input enqueu failed\n");
    }

    do {
        MppFrame packet_out = NULL;
        ret = 0;

        if (encoder->mpi->dequeue (encoder->ctx, MPP_PORT_OUTPUT, &task)) {
            usleep (2);
            continue;
        }

        if (task) {
            mpp_task_meta_get_packet(task, KEY_OUTPUT_PACKET, &packet_out);

            /* Get result */
            if (packet) {
                void *ptr = mpp_packet_get_pos(packet);
                size_t len = mpp_packet_get_length(packet);
                int intra_flag = 0;

                if (mpp_packet_get_eos(packet))
                    ret = 1;

                mpp_task_meta_get_s32(task, KEY_OUTPUT_INTRA, &intra_flag, 0);

                encoder->callback(encoder->arg, ptr, len);

                mpp_packet_deinit(&packet);
            }

            if (encoder->mpi->enqueue(encoder->ctx, MPP_PORT_OUTPUT, task)) {
                fprintf (stderr, "mpp task output enqueue failed\n");
                ret = -1;
            }
            encoder->current_index++;
            if (encoder->current_index >= MPP_MAX_BUFFERS)
                encoder->current_index = 0;
            break;
        }
    } while (1);

    return (ret);
}
