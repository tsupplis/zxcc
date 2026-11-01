/* Harmless argv recorder used both for expected fixtures and as fake zxcc. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	const char *path, *status;
	FILE *output;
	int n = 1, record = argc > 2 && strcmp(argv[1], "--record") == 0;
	if (record)
	{
		path = argv[2];
		n = 3;
	}
	else path = getenv("ZXCC_TEST_OUTPUT");
	if (!path || !(output = fopen(path, "wb"))) return 99;
	for (; n < argc; ++n)
	{
		size_t length = strlen(argv[n]);
		if (fprintf(output, "%lu:", (unsigned long)length) < 0 ||
		    fwrite(argv[n], 1, length, output) != length ||
		    fputc('\n', output) == EOF)
		{
			fclose(output);
			return 99;
		}
	}
	if (fclose(output) != 0) return 99;
	if (record) return 0;
	if (getenv("ZXCC_TEST_SIGNAL"))
	{
		raise(SIGTERM);
		return 99;
	}
	status = getenv("ZXCC_TEST_EXIT");
	return status ? atoi(status) : 0;
}
