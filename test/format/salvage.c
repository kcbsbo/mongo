/*-
 * Public Domain 2008-2014 WiredTiger, Inc.
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "format.h"

/*
 * salvage --
 *	A single salvage.
 */
static void
salvage(void)
{
	WT_CONNECTION *conn;
	WT_SESSION *session;
	int ret;

	conn = g.wts_conn;
	track("salvage", 0ULL, NULL);

	if ((ret = conn->open_session(conn, NULL, NULL, &session)) != 0)
		die(ret, "connection.open_session");
	if ((ret = session->salvage(session, g.uri, NULL)) != 0)
		die(ret, "session.salvage: %s", g.uri);
	if ((ret = session->close(session, NULL)) != 0)
		die(ret, "session.close");
}

/*
 * corrupt --
 *	Corrupt the file in a random way.
 */
static int
corrupt(void)
{
	FILE *fp;
	struct stat sb;
	off_t len, offset;
	size_t nw;
	int fd;
	char buf[2 * 1024];

	/*
	 * If it's a single Btree file (not LSM), open the file, and corrupt
	 * roughly 2% of the file at a random spot, including the beginning
	 * of the file and overlapping the end.
	 */
	(void)snprintf(buf, sizeof(buf), "%s/%s", g.home, WT_NAME);
	if ((fd = open(buf, O_RDWR)) == -1)
		return (0);
	if (fstat(fd, &sb) == -1)
		die(errno, "salvage-corrupt: fstat");

	offset = MMRAND(0, sb.st_size);
	len = 20 + (sb.st_size / 100) * 2;
	(void)snprintf(buf, sizeof(buf), "%s/slvg.corrupt", g.home);
	if ((fp = fopen(buf, "w")) == NULL)
		die(errno, "salvage-corrupt: open: %s", buf);
	(void)fprintf(fp,
	    "salvage-corrupt: offset %" PRIuMAX ", length %" PRIuMAX "\n",
	    (uintmax_t)offset, (uintmax_t)len);
	(void)fclose(fp);

	if (lseek(fd, offset, SEEK_SET) == -1)
		die(errno, "salvage-corrupt: lseek");

	memset(buf, 'z', sizeof(buf));
	for (; len > 0; len -= nw) {
		nw = (size_t)(len > sizeof(buf) ? sizeof(buf) : len);
		if (write(fd, buf, nw) == -1)
			die(errno, "salvage-corrupt: write");
	}

	if (close(fd) == -1)
		die(errno, "salvage-corrupt: close");
	return (1);
}

/*
 * wts_salvage --
 *	Salvage testing.
 */
void
wts_salvage(void)
{
	int ret;

	/* Some data-sources don't support salvage. */
	if (DATASOURCE("helium") || DATASOURCE("kvsbdb"))
		return;

	/*
	 * Save a copy of the interesting files so we can replay the salvage
	 * step as necessary.
	 */
	if ((ret = system(g.home_salvage_copy)) != 0)
		die(ret, "salvage copy step failed");

	/* Salvage, then verify. */
	wts_open(g.home, 1, &g.wts_conn);
	salvage();
	wts_verify("post-salvage verify");
	wts_close();

	/*
	 * If no records were deleted, dump and compare against Berkeley DB.
	 * (The problem with deleting records is salvage restores deleted
	 * records if a page splits leaving a deleted record on one side of
	 * the split, so we cannot depend on correctness in that case.)
	 */
	if (g.c_delete_pct == 0)
		wts_dump("salvage", SINGLETHREADED);

	/* Corrupt the file randomly, salvage, then verify. */
	if (corrupt()) {
		wts_open(g.home, 1, &g.wts_conn);
		salvage();
		wts_verify("post-corrupt-salvage verify");
		wts_close();
	}
}
