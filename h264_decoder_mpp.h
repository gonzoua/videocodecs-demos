#ifndef __H264_DECODER_MPP_H__
#define __H264_DECODER_MPP_H__

typedef struct h264_decoder_mpp * h264_decoder_mpp_t;
typedef void (*decoder_callback_t)(void *arg, char *data, ssize_t len);

h264_decoder_mpp_t mpp_h264_create_decoder(decoder_callback_t callback, void *arg);
int mpp_h264_destroy_decoder(h264_decoder_mpp_t decoder);
int mpp_h264_decode_packet(h264_decoder_mpp_t decoder, char *packet, ssize_t len);
int mpp_h264_get_frame(h264_decoder_mpp_t decoder);

#endif /* __H264_DECODER_MPP_H__ */
