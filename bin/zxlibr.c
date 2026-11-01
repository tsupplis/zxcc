#include "wrapper.h"

int main(int argc, char **argv)
{
	static const struct zxcc_wrapper_config config =
		{ "libr.com", "", 0, 0, 1 };
	return zxcc_wrapper_main(argc, argv, &config);
}
