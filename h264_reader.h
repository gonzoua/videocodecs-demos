#ifndef __H264_READER_H__
#define __H264_READER_H__

struct h264_reader {
    int             fd;
    ssize_t         size;
    ssize_t         pos;
    ssize_t         end;
    int             eof;
    unsigned char   *buffer;
};

struct h264_nal {
    ssize_t         size;
    unsigned char   *data;
};

typedef struct h264_reader* h264_reader_t;
typedef struct h264_nal* h264_nal_t;

h264_reader_t h264_reader_open(const char *path);
int h264_read_nal(h264_reader_t reader, h264_nal_t *pnal);
void h264_free_nal(h264_nal_t nal);

#endif /* __H264_READER_H__ */
