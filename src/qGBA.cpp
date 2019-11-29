#include "logging.hpp"

int main()
{
	logging::info("Hello, text!", "CPU");
	logging::important("Cool, text!", "CPU");
	logging::warning("Yellow text!", "GPU");
	logging::error("Red text!", "PPU");
	logging::fatal("BAD!", "PPU");
	return 0;
}