/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Modified for ND 2015 Asaf Amrami 
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <io.h>
#include <errno.h>
#include <fcntl.h>

extern "C" 
{
#include "../common/lib_archive/include/archive.h"
#include "../common/lib_archive/include/archive_entry.h"
}

typedef size_t ssize_t;
typedef _int64 int64_t;

struct read_fd_data {
	int	 fd;
	size_t	 block_size;
	char	 use_lseek;
	void	*buffer;
	size_t startOffset;
	size_t endOffset;
};


static int	file_close(struct archive *, void *);
static ssize_t	file_read(struct archive *, void *, const void **buff);
static int64_t	file_skip(struct archive *, void *, int64_t request);
static size_t   file_seek(struct archive *,	void *_client_data, int64_t offset, int whence);

int
duck_read_open_fd(struct archive *a, int fd, size_t block_size,size_t startOffset,size_t endOffset)
{
	struct stat st;
	struct read_fd_data *mine;
	void *b;

	archive_clear_error(a);
	if (fstat(fd, &st) != 0) {
		archive_set_error(a, errno, "Can't stat fd %d", fd);
		return (ARCHIVE_FATAL);
	}

	mine = (struct read_fd_data *)calloc(1, sizeof(*mine));
	b = malloc(block_size);
	if (mine == NULL || b == NULL) {
		archive_set_error(a, ENOMEM, "No memory");
		free(mine);
		free(b);
		return (ARCHIVE_FATAL);
	}
	mine->block_size = block_size;
	mine->buffer = b;
	mine->fd = fd;
	mine->startOffset=startOffset;
	mine->endOffset=endOffset;
	/*
	 * Skip support is a performance optimization for anything
	 * that supports lseek().  On FreeBSD, only regular files and
	 * raw disk devices support lseek() and there's no portable
	 * way to determine if a device is a raw disk device, so we
	 * only enable this optimization for regular files.
	 */
	_lseek(fd,startOffset,SEEK_SET);
	_setmode(mine->fd, O_BINARY);
	archive_read_extract_set_skip_file(a, st.st_dev, st.st_ino);
	mine->use_lseek = 1;
	archive_read_set_read_callback(a, (archive_read_callback*)file_read);
	archive_read_set_skip_callback(a, file_skip);
	archive_read_set_seek_callback(a, (archive_seek_callback*)file_seek);
	archive_read_set_close_callback(a, file_close);
	archive_read_set_callback_data(a, mine);
	return (archive_read_open1(a));
}

//main change to libarchive code is the addition of this seek function
static size_t
file_seek(struct archive *,
	void *client_data, int64_t offset, int whence)
{
	struct read_fd_data *mine = (struct read_fd_data *)client_data;
	int64_t totalSize=mine->endOffset-mine->startOffset;
	//
	if(whence==SEEK_SET)
	{
		if(offset>totalSize)
		{
			return ARCHIVE_FATAL;
		}
		else
		{
			return _lseek(mine->fd,mine->startOffset +offset ,SEEK_SET)-mine->startOffset;
		}
	}
	else if (whence==SEEK_CUR)
	{
		int64_t currentOffset=_tell(mine->fd);
		if(currentOffset+offset >mine->endOffset)
		{
			return ARCHIVE_FATAL;
		}
		else
		{
			return _lseek(mine->fd,offset ,SEEK_CUR)-mine->startOffset;
		}
	}
	else if (whence==SEEK_END)
	{
		if(offset>0 || -offset>totalSize )
		{
			return ARCHIVE_FATAL;
		}
		else
		{
			return _lseek(mine->fd,(int64_t)mine->endOffset+(int64_t)offset ,SEEK_SET) - mine->startOffset;
		}
	}
}

static size_t
file_read(struct archive *a, void *client_data, const void **buff)
{
	struct read_fd_data *mine = (struct read_fd_data *)client_data;
	size_t bytes_read;
	size_t currentOffset=_tell(mine->fd);
	int64_t bytesToRead=(int64_t)mine->endOffset-(int64_t)currentOffset;
	if(bytesToRead<=0)
	{
		return 0;
	}
	*buff = mine->buffer;
	for (;;) {
		bytes_read = _read(mine->fd, mine->buffer, min(mine->block_size,bytesToRead));
		if (bytes_read < 0) {
			if (errno == EINTR)
				continue;
			archive_set_error(a, errno, "Error reading fd %d",
			    mine->fd);
		}
		return (bytes_read);
	}
}

static int64_t
file_skip(struct archive *a, void *client_data, int64_t request)
{
	struct read_fd_data *mine = (struct read_fd_data *)client_data;
	int64_t skip = request;
	int64_t old_offset, new_offset;
	int skip_bits = sizeof(skip) * 8 - 1;  /* off_t is a signed type. */

	if (!mine->use_lseek)
		return (0);

	/* Reduce a request that would overflow the 'skip' variable. */
	if (sizeof(request) > sizeof(skip)) {
		int64_t max_skip =
		    (((int64_t)1 << (skip_bits - 1)) - 1) * 2 + 1;
		if (request > max_skip)
			skip = max_skip;
	}

	/* Reduce request to the next smallest multiple of block_size */
	request = (request / mine->block_size) * mine->block_size;
	if (request == 0)
		return (0);

	if (((old_offset = _lseek(mine->fd, 0, SEEK_CUR)) >= 0) &&
	    ((new_offset = _lseek(mine->fd, skip, SEEK_CUR)) >= 0))
		return (new_offset - old_offset);

	/* If seek failed once, it will probably fail again. */
	mine->use_lseek = 0;

	/* Let libarchive recover with read+discard. */
	if (errno == ESPIPE)
		return (0);

	/*
	 * There's been an error other than ESPIPE. This is most
	 * likely caused by a programmer error (too large request)
	 * or a corrupted archive file.
	 */
	archive_set_error(a, errno, "Error seeking");
	return (-1);
}

static int
file_close(struct archive *a, void *client_data)
{
	struct read_fd_data *mine = (struct read_fd_data *)client_data;

	(void)a; /* UNUSED */
	free(mine->buffer);
	free(mine);
	return (ARCHIVE_OK);
}
