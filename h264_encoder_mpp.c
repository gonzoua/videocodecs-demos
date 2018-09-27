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

#define BUF_SIZE (1024*1024)
#define MPP_H264_DECODE_TIMEOUT 3

#define UP_TO_16(x) (((x) + 15) & ~0xf)
#define WIDTH 1920
#define HEIGHT 1080
#define SIZE (UP_TO_16(WIDTH)*UP_TO_16(HEIGHT)*3/2)

#define msleep(x) usleep((x)*1000)

#define MPP_MAX_BUFFERS                 4

struct h264_encoder_mpp {
    int                 width;
    int                 height;
    encoder_callback_t  callback;
    void                *arg;

    MppCodingType       type;
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
    prep_cfg.width = WIDTH;
    prep_cfg.height = HEIGHT;
    prep_cfg.format = MPP_FMT_YUV420P;
    prep_cfg.hor_stride = WIDTH;
    prep_cfg.ver_stride = UP_TO_16(HEIGHT);

    if (encoder->mpi->control(encoder->ctx, MPP_ENC_SET_PREP_CFG, &prep_cfg)) {
        fprintf (stderr, "Setting input format for rockchip mpp failed");
        return -1;
    }

    if (encoder->mpi->control(encoder->ctx, MPP_ENC_GET_EXTRA_INFO, &encoder->sps_packet))
        encoder->sps_packet = NULL;

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
        if (mpp_buffer_get(encoder->input_group, &encoder->input_buffer[i], SIZE))
            goto failed;
        /* Reasonable size to fit encoded frame */
        if (mpp_buffer_get(encoder->output_group, &encoder->output_buffer[i], WIDTH*HEIGHT))
            goto failed;
    }

    encoder->current_index = 0;

    if (mpp_frame_init(&encoder->mpp_frame)) {
        fprintf (stderr, "failed to set up mpp frame\n");
        goto failed;
    }

    mpp_frame_set_width(encoder->mpp_frame, WIDTH);
    mpp_frame_set_height(encoder->mpp_frame, HEIGHT);
    mpp_frame_set_hor_stride(encoder->mpp_frame, UP_TO_16(WIDTH));
    mpp_frame_set_ver_stride(encoder->mpp_frame, UP_TO_16(HEIGHT));

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
    encoder->callback = callback;
    encoder->arg = arg;
    
    encoder->type = MPP_VIDEO_CodingAVC;
    MPP_RET ret = MPP_OK;
    ret = mpp_create(&encoder->ctx, &encoder->mpi);
    if (MPP_OK != ret) {
        fprintf(stderr, "mpp_create failed\n");
        free(encoder);
        return NULL;
    }

    ret = mpp_init(encoder->ctx, MPP_CTX_ENC, encoder->type);
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
    rc_cfg.bps_target = WIDTH
            * HEIGHT
            / 8 * 30
            / 1;
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
    int result;

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

    mpp_frame_set_buffer(encoder->mpp_frame, frame_in);

    /* Eos buffer */
    if (eos)
        mpp_frame_set_eos(encoder->mpp_frame, 1);
    else {
        ptr = mpp_buffer_get_ptr(frame_in);
        /* Y plane */
		memcpy(ptr, frame->Y, frame->Ysize);
        /* UV planes */
		memcpy(ptr + WIDTH*UP_TO_16(HEIGHT), frame->U, frame->Usize);
		memcpy(ptr + WIDTH*UP_TO_16(HEIGHT) + WIDTH*UP_TO_16(HEIGHT) / 4, frame->V, frame->Vsize);
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

                /* Fill the buffer */
				static int first_sps = 1;
                if ((intra_flag || first_sps) && encoder->sps_packet) {
					first_sps = 0;
                    void *sps_ptr = mpp_packet_get_pos(encoder->sps_packet);
                    size_t sps_len = mpp_packet_get_length(encoder->sps_packet);
                    encoder->callback(encoder->arg, sps_ptr, sps_len);
                    encoder->callback(encoder->arg, ptr, len);
					// TODO: write(fd, sps_ptr, sps_len);
					// TODO: write(fd, ptr, len);
                } else {
					// TODO: write(fd, ptr, len);
                    encoder->callback(encoder->arg, ptr, len);
                }

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
}
