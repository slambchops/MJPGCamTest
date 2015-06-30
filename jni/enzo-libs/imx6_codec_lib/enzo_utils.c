#include "enzo_utils.h"

#include <unistd.h>
#include <stdio.h>

/* Read n bytes from a file descriptor */
extern int freadn(int fd, void *vptr, size_t n)
{
	int nleft = 0;
	int nread = 0;
	char  *ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = read(fd, ptr, nleft)) <= 0) {
			if (nread == 0)
				return (n - nleft);

			perror("read");
			return (-1);			/* error EINTR XXX */
		}

		nleft -= nread;
		ptr   += nread;
	}

	return (n - nleft);
}

/* write n bytes to a file descriptor */
extern int fwriten(int fd, void *vptr, size_t n)
{
	int nleft;
	int nwrite;
	char  *ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nwrite = write(fd, ptr, nleft)) <= 0) {
			perror("fwrite: ");
			return (-1);			/* error */
		}

		nleft -= nwrite;
		ptr   += nwrite;
	}

	return (n);
} /* end fwriten */

int mediaBufferInit(struct mediaBuffer *medBuf, int size)
{
	int err;
	if (medBuf == NULL) {
		err_msg("Media Buffer: cannot init\n");
		return -1;
	}

	medBuf->desc.size = size;
	err = IOGetPhyMem(&medBuf->desc);
	if (err) {
		err_msg("Media buffer: phys allocation failure\n");
		return -1;
	}

	medBuf->desc.virt_uaddr = IOGetVirtMem(&(medBuf->desc));
	if (medBuf->desc.virt_uaddr <= 0) {
		IOFreePhyMem(&medBuf->desc);
		err_msg("Media buffer: IOGetVirtMem failed\n");
		return -1;
	}

	info_msg("Media buffer: allocated new buffer with size of %d\n", size);

	medBuf->vBufOut = (unsigned char *)medBuf->desc.virt_uaddr;
	medBuf->pBufOut = (unsigned char *)medBuf->desc.phy_addr;
	return 0;
}

int mediaBufferDeinit(struct mediaBuffer *medBuf)
{
	if (medBuf == NULL) {
		err_msg("Media Buffer: cannot deinit\n");
		return -1;
	}

	IOFreePhyMem(&medBuf->desc);

	return 0;
}
