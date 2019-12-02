#include "memory.hpp"
#include "logging.hpp"
#include "helpers.hpp"

memory::memory(uint8_t* rom)
{
	cartrom = rom;
	iwram = new uint8_t[32768];
	ewram = new uint8_t[262144];
	memset(iwram, 0, sizeof(iwram));
	memset(ewram, 0, sizeof(ewram));
}

memory::~memory()
{
	delete[] iwram;
	delete[] ewram;
}

uint8_t memory::get8(uint32_t addr)
{
	if (addr < 0x4000)
	{
		logging::error("Tried to access BIOS area: " + helpers::intToHex(addr), "memory");
		return 0;
	}
	else if (addr < 0x02000000)
	{
		//Unused area
		logging::warning("Tried to read from unused area: " + helpers::intToHex(addr), "memory");
		return 0;
	}
	else if (addr < 0x02040000)
	{
		return ewram[addr - 0x02000000];
	}
	else if (addr < 0x03000000)
	{
		//Unused area
		logging::warning("Tried to read from unused area: " + helpers::intToHex(addr), "memory");
		return 0;
	}
	else if (addr < 0x03008000)
	{
		return iwram[addr - 0x03000000];
	}
	else if (addr < 0x04000000)
	{
		//Unused area
		logging::warning("Tried to read from unused area: " + helpers::intToHex(addr), "memory");
		return 0;
	}
	else if (addr < 0x04000400)
	{
		logging::error("Tried to read from I/O area: " + helpers::intToHex(addr), "memory");
		return 0;
	}
	else if (addr < 0x08000000)
	{
		logging::error("Tried to read from VRAM area: " + helpers::intToHex(addr), "memory");
		return 0;
	}
	else if (addr < 0x0A000000)
	{
		//ROM Wait State 0
		return cartrom[addr - 0x08000000];
	}
	else if (addr < 0x0C000000)
	{
		//ROM Wait State 1
		return cartrom[addr - 0x0A000000];
	}
	else if (addr < 0x0E000000)
	{
		//ROM Wait State 2
		return cartrom[addr - 0x0C000000];
	}
	else if (addr < 0E010000)
	{
		//Cart SRAM
		logging::warning("Tried to read from Cart SRAM: " + helpers::intToHex(addr), "memory");
		return 0;
	}
	else if (addr < 0xFFFFFFFF)
	{
		//Unused area
		logging::warning("Tried to read from unused area: " + helpers::intToHex(addr), "memory");
		return 0;
	}
}

uint16_t memory::get16(uint32_t addr)
{
	return (((uint16_t)get8(addr + 1)) << 8) | get8(addr);
}

uint32_t memory::get32(uint32_t addr)
{
	return ((uint32_t)(get8(addr + 3) << 24) | ((uint32_t)get8(addr + 2) << 16) | ((uint32_t)get8(addr + 1) << 8) | get8(addr));
}