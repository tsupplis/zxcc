#include "wrapper.h"

int main(int argc, char **argv)
{
	static const struct zxcc_wrapper_config config =
		{ "c.com", "efim", 1, 1, 0 };
	return zxcc_wrapper_main(argc, argv, &config);
}
