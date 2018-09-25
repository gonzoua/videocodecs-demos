#ifndef __YUV_READER_H__
#define __YUV_READER_H__

struct yuv_frame
{
    int                 width;
    int                 height;

    /* Aligned pointers to pixel data */
    unsigned char       *Y;
    unsigned char       *U;
    unsigned char       *V;

    /* Size of the plane (not allocated memory) */
    size_t              Ysize;
    size_t              Usize;
    size_t              Vsize;

    /* Original pointers to use in free() */
    unsigned char       *Yptr;
    unsigned char       *Uptr;
    unsigned char       *Vptr;
};

struct yuv_reader
{
    int                 width;
    int                 height;
    int                 fd;
};

typedef struct yuv_reader * yuv_reader_t;
typedef struct yuv_frame * yuv_frame_t;

yuv_reader_t yuv_reader_open(const char *path, int width, int height);
int yuv_read_frame(yuv_reader_t reader, yuv_frame_t framep);
yuv_frame_t yuv_alloc_frame(yuv_reader_t reader);
void yuv_free_frame(yuv_frame_t frame);

#endif /* __YUV_READER_H__ */

