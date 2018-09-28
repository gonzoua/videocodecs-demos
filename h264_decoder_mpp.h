#ifndef __H264_DECODER_MPP_H__
#define __H264_DECODER_MPP_H__

typedef struct h264_decoder_mpp * h264_decoder_mpp_t;
typedef void (*decoder_callback_t)(void *arg, uint8_t *yplane, uint8_t *uvplane,
    int width, int height, int h_stride, int v_stride);

h264_decoder_mpp_t h264_mpp_decoder_create(decoder_callback_t callback, void *arg);
int h264_decoder_mpp_destroy(h264_decoder_mpp_t decoder);
int h264_decoder_mpp_submit_packet(h264_decoder_mpp_t decoder, uint8_t *packet, ssize_t len);
int h264_decoder_mpp_get_frame(h264_decoder_mpp_t decoder);

#endif /* __H264_DECODER_MPP_H__ */
