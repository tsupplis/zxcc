#include "wrapper.h"

int main(int argc, char **argv)
{
	static const struct zxcc_wrapper_config config =
		{ "linq.com", "omd", 0, 0, 0 };
	return zxcc_wrapper_main(argc, argv, &config);
}
