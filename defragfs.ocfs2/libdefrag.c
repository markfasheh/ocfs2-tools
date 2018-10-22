#include <libdefrag.h>
#include <unistd.h>

void *do_malloc(size_t size)
{
	void *buf;

	buf = calloc(size, 1);

	if (buf == NULL) {
		fprintf(stderr, "No mem\n");
		exit(-1);
	}

	return buf;
}

int do_read(int fd, void *bytes, size_t count)
{
	int total = 0;
	int ret;


	while (total < count) {
		ret = read(fd, bytes + total, count - total);
		if (ret < 0) {
			ret = -errno;
			if ((ret == -EAGAIN) || (ret == -EINTR))
				continue;
			total = ret;
			break;
		}
		if (ret == 0)
			break;
		total += ret;
	}

	return total;
}

int do_write(int fd, const void *bytes, size_t count)
{
	int total = 0;
	int ret;

	while (total < count) {
		ret = write(fd, bytes + total, count - total);
		if (ret < 0) {
			ret = -errno;
			if ((ret == -EAGAIN) || (ret == -EINTR))
				continue;
			else
				goto error;
		}

		total += ret;
	}
	return total;
error:
	return ret;
}

static inline unsigned short from32to16(unsigned int x)
{
	/* add up 16-bit and 16-bit for 16+c bit */
	x = (x & 0xffff) + (x >> 16);
	/* add up carry.. */
	x = (x & 0xffff) + (x >> 16);
	return x;
}

unsigned int do_csum(const unsigned char *buff, int len)
{
	int odd;
	unsigned int result = 0;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long) buff;
	if (odd) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		result += (*buff << 8);
#else
		result = *buff;
#endif
		len--;
		buff++;
	}
	if (len >= 2) {
		if (2 & (unsigned long) buff) {
			result += *(unsigned short *) buff;
			len -= 2;
			buff += 2;
		}
		if (len >= 4) {
			const unsigned char *end = buff + ((unsigned)len & ~3);
			unsigned int carry = 0;

			do {
				unsigned int w = *(unsigned int *) buff;

				buff += 4;
				result += carry;
				result += w;
				carry = (w > result);
			} while (buff < end);
			result += carry;
			result = (result & 0xffff) + (result >> 16);
		}
		if (len & 2) {
			result += *(unsigned short *) buff;
			buff += 2;
		}
	}
	if (len & 1)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		result += *buff;
#else
		result += (*buff << 8);
#endif
	result = from32to16(result);
	if (odd)
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
	return result;
}
