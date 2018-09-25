#ifndef __H264_ENCODER_MPP_H__
#define __H264_ENCODER_MPP_H__

typedef struct h264_encoder_mpp * h264_encoder_mpp_t;
h264_encoder_mpp_t mpp_h264_create_encoder(int width, int height);
int mpp_h264_destroy_encoder(h264_encoder_mpp_t encoder);
int mpp_h264_encode_frame(h264_encoder_mpp_t encoder, yuv_frame_t frame, int eos);

#endif /* __H264_ENCODER_MPP_H__ */
