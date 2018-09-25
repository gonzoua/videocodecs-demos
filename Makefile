DECODER_OBJS = decoder.o h264_reader.o
ENCODER_OBJS = encoder.o yuv_reader.o

all: encoder decoder

decoder: $(DECODER_OBJS)
	$(CC) -o decoder $(DECODER_OBJS)

encoder: $(ENCODER_OBJS)
	$(CC) -o encoder $(ENCODER_OBJS)

clean:
	rm -f encoder decoder $(DECODER_OBJS) $(ENCODER_OBJS)
