#include "cpmint.h"

static cpm_byte fcb[36], other[36], saved[36], dma[128];
static char workdir[] = "/tmp/zxcc-testredir-XXXXXX";
static char original_dir[CPM_MAXPATH + 1];
static int failures;

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
		++failures; \
	} \
} while (0)

static void require(int ok, const char* operation)
{
	if (!ok) {
		fprintf(stderr, "%s failed: %s\n", operation, strerror(errno));
		exit(1);
	}
}

static void cleanup(void)
{
	releaseFile("./alpha.dat");
	releaseFile("./beta.dat");
	if (unlink("alpha.dat") || unlink("beta.dat") ||
		chdir(original_dir) || rmdir(workdir)) {
		fprintf(stderr, "Cannot clean up redirector test files: %s\n", strerror(errno));
		exit(1);
	}
}

static void setup(cpm_byte* target, const char* name)
{
	memset(target, 0, 36);
	memset(target + 1, ' ', 11);
	memcpy(target + 1, name, strlen(name));
	memcpy(target + 9, "DAT", 3);
}

static void fixture(const char* name, int value)
{
	int fd = open(name, O_RDWR | O_CREAT | O_EXCL | O_BINARY, 0600);
	require(fd >= 0, "create fixture");
	memset(dma, value, sizeof(dma));
	require(write(fd, dma, sizeof(dma)) == sizeof(dma), "write fixture");
	require(close(fd) == 0, "close fixture");
}

static void open_file(cpm_byte* target, const char* name)
{
	setup(target, name);
	require(fcb_open(target, dma) == 0, "open FCB");
}

static int is_open(int fd)
{
	return fcntl(fd, F_GETFD) >= 0;
}

static void test_close(void)
{
	int fd;
	open_file(fcb, "ALPHA");
	fd = redir_verify_fcb(fcb);
	fcb[5] |= 0x80;
	CHECK(fcb_close(fcb) == 0);
	CHECK(is_open(fd));
	fcb[5] &= 0x7F;
	CHECK(fcb_close(fcb) == 0);
	CHECK(!is_open(fd));
	CHECK(fcb_close(fcb) != 0);
}

static void test_stale_close(void)
{
	int fd;
	open_file(fcb, "ALPHA");
	fd = redir_verify_fcb(fcb);
	memcpy(saved, fcb, sizeof(saved));
	require(fcb_close(fcb) == 0, "close first FCB");
	open_file(other, "BETA");
	CHECK(redir_verify_fcb(other) == fd);
	CHECK(fcb_close(fcb) != 0);
#ifdef FILETRACKER
	CHECK(fcb_close(saved) != 0);
	CHECK(fcb_read(saved, dma) == 9);
	CHECK(fcb_write(saved, dma) == 9);
#endif
	CHECK(is_open(fd));
	CHECK(fcb_read(other, dma) == 0);
	CHECK(dma[0] == 'B');
	CHECK(fcb_close(other) == 0);
}

#ifdef FILETRACKER
static void test_reuse(void)
{
	int oldfd, newfd, n;
	open_file(fcb, "ALPHA");
	for (n = 0; n < 32; ++n) {
		oldfd = redir_verify_fcb(fcb);
		CHECK(fcb_open(fcb, dma) == 0);
		newfd = redir_verify_fcb(fcb);
		CHECK(oldfd == newfd || !is_open(oldfd));
	}
	oldfd = redir_verify_fcb(fcb);
	setup(fcb, "BETA");
	CHECK(fcb_open(fcb, dma) == 0);
	newfd = redir_verify_fcb(fcb);
	CHECK(oldfd == newfd || !is_open(oldfd));
	CHECK(fcb_read(fcb, dma) == 0);
	CHECK(dma[0] == 'B');
	CHECK(fcb_close(fcb) == 0);

	open_file(fcb, "ALPHA");
	oldfd = redir_verify_fcb(fcb);
	setup(fcb, "MISSING");
	CHECK(fcb_open(fcb, dma) != 0);
	CHECK(!is_open(oldfd));
}

static void test_copied_fcb(void)
{
	int fd;
	open_file(fcb, "ALPHA");
	fd = redir_verify_fcb(fcb);
	memcpy(saved, fcb, sizeof(saved));
	CHECK(fcb_read(saved, dma) == 0);
	CHECK(dma[0] == 'A');
	CHECK(fcb_close(saved) == 0);
	CHECK(!is_open(fd));
	CHECK(fcb_close(fcb) != 0);
}

static void test_released_file(void)
{
	int fd;
	open_file(fcb, "ALPHA");
	fd = redir_verify_fcb(fcb);
	memcpy(saved, fcb, sizeof(saved));
	releaseFile("./alpha.dat");
	CHECK(!is_open(fd));
	open_file(other, "BETA");
	CHECK(redir_verify_fcb(other) == fd);
	CHECK(fcb_close(fcb) != 0);
	CHECK(fcb_close(saved) != 0);
	CHECK(is_open(fd));
	CHECK(fcb_close(other) == 0);
}
#endif

static void test_parent(void)
{
	static const struct {
		const char* path;
		const char* parent;
	} cases[] = {
		{ "/aa/bbb/", "/aa/" },
		{ "/aa/bb/", "/aa/" },
		{ "/aa/bbb", "/aa/" },
		{ "/aa/bbb//", "/aa/" },
		{ "/aa/", "/" },
		{ "/", "/" },
		{ "../foo/", "../" },
		{ "./foo/", "./" },
		{ "", "" }
	};
	size_t n;
	for (n = 0; n < sizeof(cases) / sizeof(cases[0]); ++n) {
		strcpy(redir_drive_prefix[3], cases[n].path);
		memset(fcb, 0, sizeof(fcb));
		memset(fcb + 1, ' ', 11);
		memcpy(fcb + 1, "..", 2);
		fcb[0] = 0x84;
		CHECK(fcb_open(fcb, dma) == 0);
		CHECK(strcmp(redir_drive_prefix[3], cases[n].parent) == 0);
	}
	CHECK(fcb_close(fcb) == 0);
	CHECK(strcmp(redir_drive_prefix[3], "/") == 0);
}

int main(void)
{
	require(getcwd(original_dir, sizeof(original_dir)) != NULL, "getcwd");
	require(mkdtemp(workdir) != NULL, "create test directory");
	require(chdir(workdir) == 0, "enter test directory");
	require(fcb_init(), "initialize redirector");
	fixture("alpha.dat", 'A');
	fixture("beta.dat", 'B');
	require(atexit(cleanup) == 0, "register cleanup");

	test_close();
	test_stale_close();
#ifdef FILETRACKER
	test_reuse();
	test_copied_fcb();
	test_released_file();
#endif
	test_parent();
	printf("Redirector regression tests: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
