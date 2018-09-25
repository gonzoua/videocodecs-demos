#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/errno.h>
#include "yuv_reader.h"

#define DEFAULT_PLANE_ALIGNMENT	16
#define ALIGN_UP_TO(ptr, alignment) (((intptr_t)(ptr) + (alignment) - 1) & ~(alignment));

yuv_reader_t
yuv_reader_open(const char *path, int width, int height)
{
    yuv_reader_t reader = malloc(sizeof(struct yuv_reader));

    reader->fd = open(path, O_RDONLY);
    if (reader->fd < 0) {
        free(reader);
        return (NULL);
    }

    reader->width = width;
    reader->height = height;

    return (reader);
}

int
yuv_read_frame(yuv_reader_t reader, yuv_frame_t frame)
{
    /*
     * This is not very robust but good enough for demo
     */
    read(reader->fd, frame->Y, frame->Ysize);
    read(reader->fd, frame->U, frame->Usize);
    read(reader->fd, frame->V, frame->Vsize);

    return (0);
}

yuv_frame_t
yuv_alloc_frame(yuv_reader_t reader)
{
	yuv_frame_t frame = malloc(sizeof(struct yuv_frame));
	size_t Ysize = reader->width*reader->height;
	size_t UVsize = reader->width*reader->height/4;

	/*
	 * Encoder might have alignment requirements for memory addresses
	 * use default one for now
	 */
	frame->Yptr = (unsigned char *)malloc(Ysize + DEFAULT_PLANE_ALIGNMENT);
	if (!frame->Yptr) {
        free(frame);
        return (NULL);
    }
	frame->Y = (unsigned char*)ALIGN_UP_TO(frame->Yptr, DEFAULT_PLANE_ALIGNMENT);
	frame->Ysize = Ysize;

	frame->Uptr = (unsigned char *)malloc(UVsize + DEFAULT_PLANE_ALIGNMENT);
	if (!frame->Uptr) {
        free(frame->Yptr);
        free(frame);
        return (NULL);
    }
	frame->Usize = UVsize;
	frame->U = (unsigned char*)ALIGN_UP_TO(frame->Uptr, DEFAULT_PLANE_ALIGNMENT);

	frame->Vptr = (unsigned char *)malloc(UVsize + DEFAULT_PLANE_ALIGNMENT);
	if (!frame->Vptr) {
        free(frame->Yptr);
        free(frame->Vptr);
        free(frame);
        return (NULL);
    }
	frame->Vsize = UVsize;
	frame->V = (unsigned char*)ALIGN_UP_TO(frame->Uptr, DEFAULT_PLANE_ALIGNMENT);

	return (frame);
}

void yuv_free_frame(yuv_frame_t frame)
{
	free(frame->Yptr);
	free(frame->Uptr);
	free(frame->Vptr);
	free(frame);
}
