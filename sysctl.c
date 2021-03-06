#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "asm/types.h"
#include "sysctl.h"
#include "util.h"

#define __SYSCTL_OP(__ret, __fd, __req, __type, __nr, __op)		\
do {									\
	if (__op == CTL_READ)						\
		__ret = sysctl_read_##__type(__fd, __req,		\
					     (__type *)(__req)->arg,	\
					     __nr);			\
	else if (__op == CTL_WRITE)					\
		__ret = sysctl_write_##__type(__fd, __req,		\
					      (__type *)(__req)->arg,	\
					      __nr);			\
	else								\
		__ret = -1;						\
} while (0)

#define GEN_SYSCTL_READ_FUNC(__type, __conv)				\
static int sysctl_read_##__type(int fd,					\
				struct sysctl_req *req,			\
				__type *arg,				\
				int nr)					\
{									\
	char buf[1024] = {0};						\
	int i, ret = -1;						\
	char *p = buf;							\
									\
	ret = read(fd, buf, sizeof(buf));				\
	if (ret < 0) {							\
		pr_perror("Can't read %s", req->name);			\
		ret = -1;						\
		goto err;						\
	}								\
									\
	for (i = 0; i < nr && p < buf + sizeof(buf); p++, i++)		\
		((__type *)arg)[i] = __conv(p, &p, 10);			\
									\
	if (i != nr) {							\
		pr_err("Not enough params for %s (%d != %d)\n",		\
			req->name, i, nr);				\
		goto err;						\
	}								\
									\
	ret = 0;							\
									\
err:									\
	return ret;							\
}

#define GEN_SYSCTL_WRITE_FUNC(__type, __fmt)				\
static int sysctl_write_##__type(int fd,				\
				 struct sysctl_req *req,		\
				 __type *arg,				\
				 int nr)				\
{									\
	char buf[1024];							\
	int i, ret = -1;						\
	int off = 0;							\
									\
	for (i = 0; i < nr && off < sizeof(buf) - 1; i++) {		\
		snprintf(&buf[off], sizeof(buf) - off, __fmt, arg[i]);	\
		off += strlen(&buf[off]);				\
	}								\
									\
	if (i != nr) {							\
		pr_err("Not enough space for %s (%d != %d)\n",		\
			req->name, i, nr);				\
		goto err;						\
	}								\
									\
	/* trailing spaces in format */					\
	while (off > 0 && isspace(buf[off - 1]))			\
		off--;							\
	buf[off + 0] = '\n';						\
	ret = write(fd, buf, off + 1);					\
	if (ret < 0) {							\
		pr_perror("Can't write %s", req->name);			\
		ret = -1;						\
		goto err;						\
	}								\
									\
	ret = 0;							\
err:									\
	return ret;							\
}

GEN_SYSCTL_READ_FUNC(u32, strtoul);
GEN_SYSCTL_READ_FUNC(u64, strtoull);
GEN_SYSCTL_READ_FUNC(s32, strtol);

GEN_SYSCTL_WRITE_FUNC(u32, "%u ");
GEN_SYSCTL_WRITE_FUNC(u64, "%"PRIu64" ");
GEN_SYSCTL_WRITE_FUNC(s32, "%d ");

static int
sysctl_write_char(int fd, struct sysctl_req *req, char *arg, int nr)
{
	pr_debug("%s nr %d\n", req->name, nr);
	if (dprintf(fd, "%s\n", arg) < 0)
		return -1;

	return 0;
}

static int
sysctl_read_char(int fd, struct sysctl_req *req, char *arg, int nr)
{
	int ret = -1;

	pr_debug("%s nr %d\n", req->name, nr);
	ret = read(fd, arg, nr);
	if (ret < 0) {
		pr_perror("Can't read %s", req->name);
		goto err;
	}
	ret = 0;

err:
	return ret;
}

static int __sysctl_op(int dir, struct sysctl_req *req, int op)
{
	int fd, ret = -1, nr = 1, flags;

	if (op == CTL_READ)
		flags = O_RDONLY;
	else
		flags = O_WRONLY;

	fd = openat(dir, req->name, flags);
	if (fd < 0) {
		if (errno == ENOENT && (req->flags & CTL_FLAGS_OPTIONAL))
			return 0;
		pr_perror("Can't open sysctl %s", req->name);
		return -1;
	}

	switch (CTL_TYPE(req->type)) {
	case __CTL_U32A:
		nr = CTL_LEN(req->type);
	case CTL_U32:
		__SYSCTL_OP(ret, fd, req, u32, nr, op);
		break;
	case CTL_32:
		__SYSCTL_OP(ret, fd, req, s32, nr, op);
		break;
	case __CTL_U64A:
		nr = CTL_LEN(req->type);
	case CTL_U64:
		__SYSCTL_OP(ret, fd, req, u64, nr, op);
		break;
	case __CTL_STR:
		nr = CTL_LEN(req->type);
		__SYSCTL_OP(ret, fd, req, char, nr, op);
		break;
	}

	close_safe(&fd);

	return ret;
}

int sysctl_op(struct sysctl_req *req, size_t nr_req, int op)
{
	int ret = 0;
	int dir = -1;

	dir = open("/proc/sys", O_RDONLY);
	if (dir < 0) {
		pr_perror("Can't open sysctl dir");
		return -1;
	}

	while (nr_req--) {
		ret = __sysctl_op(dir, req, op);
		if (ret < 0)
			break;
		req++;
	}

	close_safe(&dir);

	return ret;
}
