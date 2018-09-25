DECODER_OBJS = decoder.o h264_reader.o
ENCODER_OBJS = encoder.o yuv_reader.o h264_encoder_mpp.o
CFLAGS = -g
LFLAGS = -lrockchip_mpp

all: encoder decoder

decoder: $(DECODER_OBJS)
	$(CC) -o decoder $(DECODER_OBJS) $(LFLAGS)

encoder: $(ENCODER_OBJS)
	$(CC) -o encoder $(ENCODER_OBJS) $(LFLAGS)

clean:
	rm -f encoder decoder $(DECODER_OBJS) $(ENCODER_OBJS)
