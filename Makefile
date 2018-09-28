# h264_reader.c is not required
DECODER_OBJS = decoder.o h264_decoder_mpp.o
ENCODER_OBJS = encoder.o yuv_reader.o h264_encoder_mpp.o
CFLAGS += -g -Wall
LFLAGS = -lrockchip_mpp

all: encoder decoder

decoder: $(DECODER_OBJS)
	$(CC) -o decoder $(DECODER_OBJS) $(LFLAGS)

encoder: $(ENCODER_OBJS)
	$(CC) -o encoder $(ENCODER_OBJS) $(LFLAGS)

clean:
	rm -f encoder decoder $(DECODER_OBJS) $(ENCODER_OBJS)
