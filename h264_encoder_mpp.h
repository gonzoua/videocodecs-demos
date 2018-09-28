#ifndef __H264_ENCODER_MPP_H__
#define __H264_ENCODER_MPP_H__

typedef struct h264_encoder_mpp * h264_encoder_mpp_t;
typedef void (*encoder_callback_t)(void *arg, char *data, ssize_t len);

h264_encoder_mpp_t h264_mpp_encoder_create(int width, int height, encoder_callback_t callback, void *arg);
int h264_mpp_encoder_destroy(h264_encoder_mpp_t encoder);
int h264_mpp_encoder_submit_frame(h264_encoder_mpp_t encoder, yuv_frame_t frame, int eos);

#endif /* __H264_ENCODER_MPP_H__ */
