#ifndef ZXCC_WRAPPER_H
#define ZXCC_WRAPPER_H

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Header-only so the existing single-source Windows projects need no changes. */
struct zxcc_wrapper_config
{
	const char *program;
	const char *filename_options;
	int cross_reference;
	int default_include;
	int first_is_option;
};

struct zxcc_wrapper_args
{
	char **items;
	size_t count;
	size_t capacity;
};

static void zxcc_wrapper_free(struct zxcc_wrapper_args *args)
{
	size_t i;
	for (i = 0; i < args->count; ++i) free(args->items[i]);
	free(args->items);
}

/* Each input produces at most two arguments; every string is owned. */
static int zxcc_wrapper_add(struct zxcc_wrapper_args *args,
	const char *prefix, const char *value)
{
	size_t p = strlen(prefix), v = strlen(value);
	char *item;
	if (args->count >= args->capacity - 1 || p == (size_t)-1 ||
	    v > (size_t)-1 - p - 1)
	{
		errno = E2BIG;
		return 0;
	}
	item = (char *)malloc(p + v + 1);
	if (!item) return 0;
	memcpy(item, prefix, p);
	memcpy(item + p, value, v + 1);
	args->items[args->count++] = item;
	args->items[args->count] = NULL;
	return 1;
}

#ifdef _WIN32
/*
 * The Windows CRT spawn functions join argv with spaces, not shell parsing.
 * Quote every argument using the CRT backslash/quote rules, including trailing
 * backslashes. The checked worst-case size is twice the input plus quotes/NUL.
 */
static char *zxcc_wrapper_quote(const char *value)
{
	size_t length = strlen(value), slashes, i;
	char *quoted, *out;
	if (length > ((size_t)-1 - 3) / 2)
	{
		errno = E2BIG;
		return NULL;
	}
	quoted = (char *)malloc(length * 2 + 3);
	if (!quoted) return NULL;
	out = quoted;
	*out++ = '"';
	for (;;)
	{
		slashes = 0;
		while (*value == '\\') { ++slashes; ++value; }
		if (*value == '"' || !*value)
		{
			for (i = 0; i < slashes; ++i) *out++ = '\\';
		}
		for (i = 0; i < slashes; ++i) *out++ = '\\';
		if (!*value) break;
		if (*value == '"') *out++ = '\\';
		*out++ = *value++;
	}
	*out++ = '"';
	*out = 0;
	return quoted;
}
#endif

static int zxcc_wrapper_invoke(struct zxcc_wrapper_args *args)
{
#ifdef _WIN32
	const char **quoted;
	size_t i;
	int saved_errno;
	intptr_t status;
	quoted = (const char **)calloc(args->capacity, sizeof(*quoted));
	if (!quoted) return -1;
	for (i = 0; i < args->count; ++i)
	{
		quoted[i] = zxcc_wrapper_quote(args->items[i]);
		if (!quoted[i]) break;
	}
	if (i == args->count)
		status = _spawnvp(_P_WAIT, "zxcc", quoted);
	else
		status = -1;
	saved_errno = errno;
	for (i = 0; i < args->count; ++i) free((void *)quoted[i]);
	free(quoted);
	errno = saved_errno;
	if (status == -1) return -1;
	/* _P_WAIT returns the decoded child exit code, not a wait status. */
	if (status < 0 || status > INT_MAX)
	{
		fprintf(stderr, "zxcc wrapper: child exit code is out of range\n");
		return 1;
	}
	return (int)status;
#else
	pid_t child, waited;
	int status;
	child = fork();
	if (child == -1) return -1;
	if (child == 0)
	{
		execvp(args->items[0], args->items);
		fprintf(stderr, "zxcc wrapper: cannot execute zxcc: %s\n",
			strerror(errno));
		_exit(127);
	}
	do { waited = waitpid(child, &status, 0); }
	while (waited == -1 && errno == EINTR);
	if (waited == -1) return -1;
	if (WIFEXITED(status)) return WEXITSTATUS(status);
	if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
	fprintf(stderr, "zxcc wrapper: unexpected child wait status\n");
	return 1;
#endif
}

static int zxcc_wrapper_main(int argc, char **argv,
	const struct zxcc_wrapper_config *config)
{
	struct zxcc_wrapper_args args;
	int n, result, saved_errno;
	size_t length, inputs;
	const char *arg, *option;
	char flag[4];
	args.items = NULL;
	args.count = 0;
	inputs = argc > 1 ? (size_t)(argc - 1) : 0;
	if (inputs > ((size_t)-1 - 4) / 2)
	{
		errno = E2BIG;
		goto failed;
	}
	args.capacity = inputs * 2 + 4;
	if (args.capacity > (size_t)-1 / sizeof(*args.items))
	{
		errno = E2BIG;
		goto failed;
	}
	args.items = (char **)calloc(args.capacity, sizeof(*args.items));
	if (!args.items) goto failed;
	if (!zxcc_wrapper_add(&args, "", "zxcc") ||
	    !zxcc_wrapper_add(&args, "", config->program)) goto failed;
	if (config->default_include &&
	    !zxcc_wrapper_add(&args, "", "--IC:")) goto failed;
	for (n = 1; n < argc; ++n)
	{
		arg = argv[n];
		length = strlen(arg);
		if (!config->first_is_option && arg[0] == '-')
		{
			/* Length guards also make "-" and "-c" safe to inspect. */
			if (config->cross_reference && length > 3 &&
			    (arg[1] == 'c' || arg[1] == 'C') &&
			    (arg[2] == 'r' || arg[2] == 'R'))
			{
				if (!zxcc_wrapper_add(&args, "", "--cr") ||
				    !zxcc_wrapper_add(&args, "+", arg + 3)) goto failed;
				continue;
			}
			if (length > 2)
			{
				for (option = config->filename_options; *option; ++option)
				{
					if (arg[1] == *option ||
					    arg[1] == *option - 'a' + 'A') break;
				}
				if (*option)
				{
					flag[0] = '-'; flag[1] = '-';
					flag[2] = *option; flag[3] = 0;
					if (!zxcc_wrapper_add(&args, "", flag) ||
					    !zxcc_wrapper_add(&args, "+", arg + 2)) goto failed;
					continue;
				}
			}
		}
		if (!zxcc_wrapper_add(&args,
		    (config->first_is_option ? n == 1 : arg[0] == '-') ? "-" : "",
		    arg)) goto failed;
	}
	result = zxcc_wrapper_invoke(&args);
	if (result == -1) goto failed;
	zxcc_wrapper_free(&args);
	return result;
failed:
	saved_errno = errno;
	zxcc_wrapper_free(&args);
	fprintf(stderr, "zxcc wrapper: argument construction or invocation failed: %s\n",
		strerror(saved_errno));
	return 1;
}

#endif
