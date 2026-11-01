#include "cpmint.h"
#include <signal.h>
#include <sys/resource.h>

static cpm_byte fcb[36], other[36], saved[36], dma[128];
static char workdir[] = "/tmp/zxcc-testredir-XXXXXX";
static char* original_dir;
static int directory_created, entered_directory;
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
	static const char* files[] = { "alpha.dat", "beta.dat", "gamma.dat", "link" };
	static const char* directories[] = { "foo/bar", "foo" };
	size_t n;
	int failed = 0;
	if (entered_directory) {
		releaseFile("./alpha.dat");
		releaseFile("./beta.dat");
		releaseFile("./gamma.dat");
		for (n = 0; n < sizeof(files) / sizeof(files[0]); ++n)
			if (unlink(files[n]) && errno != ENOENT) {
				fprintf(stderr, "Cannot remove %s: %s\n", files[n], strerror(errno));
				failed = 1;
			}
		for (n = 0; n < sizeof(directories) / sizeof(directories[0]); ++n)
			if (rmdir(directories[n]) && errno != ENOENT) {
				fprintf(stderr, "Cannot remove %s: %s\n", directories[n], strerror(errno));
				failed = 1;
			}
		if (chdir(original_dir)) {
			fprintf(stderr, "Cannot leave test directory: %s\n", strerror(errno));
			failed = 1;
		}
	}
	if (directory_created && rmdir(workdir)) {
		fprintf(stderr, "Cannot remove test directory: %s\n", strerror(errno));
		failed = 1;
	}
	free(original_dir);
	if (failed) _Exit(1);
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

static void test_handle_index(void)
{
	static cpm_byte files[64][36];
	size_t n;
	for (n = 0; n < sizeof(files) / sizeof(files[0]); ++n)
		open_file(files[n], "ALPHA");
	for (n = 0; n < sizeof(files) / sizeof(files[0]); ++n) {
		CHECK(fcb_read(files[n], dma) == 0);
		CHECK(dma[0] == 'A');
	}
	for (n = 0; n < sizeof(files) / sizeof(files[0]); ++n)
		CHECK(fcb_close(files[n]) == 0);
}
#endif

static void parent_case(const char* path, const char* expected)
{
	struct stat actual, parent;
	strcpy(redir_drive_prefix[3], path);
	memset(fcb, 0, sizeof(fcb));
	memset(fcb + 1, ' ', 11);
	memcpy(fcb + 1, "..", 2);
	fcb[0] = 0x84;
	CHECK(fcb_open(fcb, dma) == 0);
	require(stat(expected, &parent) == 0, "stat expected parent");
	require(stat(redir_drive_prefix[3], &actual) == 0, "stat actual parent");
	CHECK(actual.st_dev == parent.st_dev && actual.st_ino == parent.st_ino);
}

static void test_parent(void)
{
	static const struct {
		const char* path;
		const char* parent;
	} cases[] = {
		{ "foo/", "." },
		{ "foo", "." },
		{ "foo/bar/", "foo" },
		{ "foo/bar", "foo" },
		{ "foo/bar//", "foo" },
		{ "./foo/bar/", "foo" },
		{ "./", ".." },
		{ "../", "../.." },
		{ "/", "/" },
		{ "link/", "foo" }
	};
	char absolute[CPM_MAXPATH + 1];
	size_t n;
	for (n = 0; n < sizeof(cases) / sizeof(cases[0]); ++n)
		parent_case(cases[n].path, cases[n].parent);
	snprintf(absolute, sizeof(absolute), "%s/foo/bar/", workdir);
	parent_case(absolute, "foo");
	CHECK(fcb_close(fcb) == 0);
	CHECK(strcmp(redir_drive_prefix[3], "/") == 0);
	redir_drive_prefix[3][0] = 0;
	CHECK(fcb_open(fcb, dma) != 0);
	CHECK(!redir_drive_prefix[3][0]);
	strcpy(redir_drive_prefix[3], "/nonexistent-zxcc-test-directory/");
	CHECK(fcb_open(fcb, dma) != 0);
	CHECK(strcmp(redir_drive_prefix[3], "/nonexistent-zxcc-test-directory/") == 0);
}

static void test_mapping(void)
{
	char host[CPM_MAXPATH + 1], translated[CPM_MAXPATH + 1];
	int n;
	require(fcb_init(), "reset mappings");
	require(xlt_map(0, "/bin80/"), "map binaries");
	require(xlt_map(1, "/lib80/"), "map libraries");
	require(xlt_map(2, "/include80/"), "map includes");
	for (n = 0; n < 12; ++n) {
		snprintf(host, sizeof(host), "/directory%d/file.dat", n);
		CHECK(xlt_name(host, translated) == 1);
		CHECK(translated[0] == 'd' + n);
	}
	CHECK(xlt_name("/overflow/file.dat", translated) == 0);
	CHECK(!translated[0]);
	CHECK(strcmp(xlt_getcwd(15), "./") == 0);
	CHECK(xlt_umap(3) == 1);
	CHECK(xlt_name("/overflow/file.dat", translated) == 1);
	CHECK(strcmp(translated, "d:file.dat") == 0);
	CHECK(xlt_name("a:file.dat", translated) == 1);
	CHECK(strcmp(translated, "a:file.dat") == 0);
	CHECK(xlt_name("file.dat", translated) == 1);
	CHECK(strcmp(translated, "p:file.dat") == 0);
	memset(host, 'x', sizeof(host) - 1);
	host[sizeof(host) - 1] = 0;
	CHECK(xlt_name(host, translated) == 0);
	CHECK(!translated[0]);
	CHECK(xlt_map(-1, host) == 0);
	CHECK(xlt_map(16, "./") == 0);
	CHECK(xlt_umap(16) == 0);
	CHECK(strcmp(xlt_getcwd(16), "") == 0);
	host[sizeof(host) - 2] = 0;
	strcpy(redir_drive_prefix[3], host);
	setup(fcb, "ALPHA");
	fcb[0] = 4;
	CHECK(fcb_open(fcb, dma) != 0);
	fcb[0] = 17;
	CHECK(fcb_open(fcb, dma) != 0);
}

static void test_write_failures(void)
{
	struct rlimit original, limit;
	struct sigaction ignored, old_action;
	struct stat info;
	int fd;
	require(getrlimit(RLIMIT_FSIZE, &original) == 0, "get file-size limit");
	memset(&ignored, 0, sizeof(ignored));
	ignored.sa_handler = SIG_IGN;
	require(sigemptyset(&ignored.sa_mask) == 0, "initialize signal mask");
	require(sigaction(SIGXFSZ, &ignored, &old_action) == 0, "ignore file-size signal");
	open_file(fcb, "GAMMA");
	fd = redir_verify_fcb(fcb);
	redir_put_fcb_pos(fcb, 128);
	memset(dma, 'W', sizeof(dma));

	limit = original;
	limit.rlim_cur = 128;
	require(setrlimit(RLIMIT_FSIZE, &limit) == 0, "limit file size");
	CHECK(fcb_write(fcb, dma) != 0);
	require(setrlimit(RLIMIT_FSIZE, &original) == 0, "restore file-size limit");
	CHECK(redir_get_fcb_pos(fcb) == 128);
	CHECK(redir_rd32(fcb + 0x1C) == 128);
	require(fstat(fd, &info) == 0, "stat failed write");
	CHECK(info.st_size == 128);

	limit.rlim_cur = 192;
	require(setrlimit(RLIMIT_FSIZE, &limit) == 0, "limit partial write");
	CHECK(fcb_write(fcb, dma) == 1);
	require(setrlimit(RLIMIT_FSIZE, &original) == 0, "restore file-size limit");
	CHECK(redir_get_fcb_pos(fcb) == 128);
	CHECK(redir_rd32(fcb + 0x1C) == 192);
	require(fstat(fd, &info) == 0, "stat partial write");
	CHECK(info.st_size == 192);
	CHECK(fcb_write(fcb, dma) == 0);
	CHECK(redir_get_fcb_pos(fcb) == 256);
	CHECK(redir_rd32(fcb + 0x1C) == 256);
	CHECK(fcb_close(fcb) == 0);
	require(sigaction(SIGXFSZ, &old_action, NULL) == 0, "restore file-size signal");
}

static void test_flush_failure(void)
{
	int ends[2];
	require(pipe(ends) == 0, "create flush test pipe");
	setup(fcb, "PIPE");
	trackFile("./pipe.dat", fcb, ends[0]);
	fcb[16] = 0xFD;
	redir_wrhandle(fcb + 18, ends[0]);
	fcb[5] |= 0x80;
	CHECK(fcb_close(fcb) != 0);
	CHECK(is_open(ends[0]));
	fcb[5] &= 0x7F;
	CHECK(fcb_close(fcb) == 0);
	require(close(ends[1]) == 0, "close pipe writer");
}

int main(void)
{
	require(atexit(cleanup) == 0, "register cleanup");
	original_dir = getcwd(NULL, 0);
	require(original_dir != NULL, "getcwd");
	require(mkdtemp(workdir) != NULL, "create test directory");
	directory_created = 1;
	require(chdir(workdir) == 0, "enter test directory");
	entered_directory = 1;
	require(fcb_init(), "initialize redirector");
	fixture("alpha.dat", 'A');
	fixture("beta.dat", 'B');
	fixture("gamma.dat", 'G');
	require(mkdir("foo", 0700) == 0, "create test directory foo");
	require(mkdir("foo/bar", 0700) == 0, "create test directory bar");
	require(symlink("foo/bar", "link") == 0, "create directory symlink");

	test_close();
	test_stale_close();
#ifdef FILETRACKER
	test_reuse();
	test_copied_fcb();
	test_released_file();
	test_handle_index();
#endif
	test_parent();
	test_mapping();
	test_write_failures();
	test_flush_failure();
	printf("Redirector regression tests: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
