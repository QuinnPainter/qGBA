#include "memory.hpp"
#include "logging.hpp"
#include "helpers.hpp"

memory::memory(uint8_t* rom, uint32_t romSize, uint8_t* bios, gpu* GPU, input* Input, interrupt* Interrupt)
{
	cartrom = rom;
	this->romSize = romSize;
	this->bios = bios;
	this->GPU = GPU;
	this->Input = Input;
	this->Interrupt = Interrupt;
	iwram = new uint8_t[32768];
	ewram = new uint8_t[262144];
	memset(iwram, 0, 32768);
	memset(ewram, 0, 262144);
}

memory::~memory()
{
	delete[] iwram;
	delete[] ewram;
}

uint8_t memory::get8Cart(uint32_t addr)
{
	if (addr < romSize)
	{
		return cartrom[addr];
	}
	else
	{
		//logging::warning("Read outside cart ROM", "memory");
		return 0; // Open bus?
	}
}

uint8_t memory::get8(uint32_t addr)
{
	if (addr < 0x4000)
	{
		if (bios != nullptr)
		{
			return bios[addr];
		}
		else
		{
			logging::error("BIOS Read, but it's not loaded: " + helpers::intToHex(addr), "memory");
		}
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
		//EWRAM mirrors
		return ewram[(addr - 0x02000000) % 0x40000];
	}
	else if (addr < 0x03008000)
	{
		return iwram[addr - 0x03000000];
	}
	else if (addr < 0x04000000)
	{
		//IWRAM mirrors
		return iwram[(addr - 0x03000000) % 0x8000];
	}
	else if (addr < 0x04000400)
	{
		// IO area
		if (addr < 0x04000060)
		{
			return GPU->getRegister(addr);
		}
		else if (addr < 0x40000B0)
		{
			logging::error("Tried to read from sound register: " + helpers::intToHex(addr), "memory");
			return 0;
		}
		else if (addr < 0x4000100)
		{
			logging::error("Tried to read from DMA register: " + helpers::intToHex(addr), "memory");
			return 0;
		}
		else if (addr < 0x4000120)
		{
			logging::error("Tried to read from timer register: " + helpers::intToHex(addr), "memory");
			return 0;
		}
		else if (addr < 0x4000130)
		{
			logging::error("Tried to read from serial area 1: " + helpers::intToHex(addr), "memory");
			return 0;
		}
		else if (addr < 0x4000134)
		{
			return Input->getRegister(addr);
		}
		else if (addr < 0x4000200)
		{
			logging::error("Tried to read from serial area 2: " + helpers::intToHex(addr), "memory");
			return 0;
		}
		else if (addr < 0x4000804)
		{
			return Interrupt->getRegister(addr);
		}
		else
		{
			logging::error("Tried to read from unused I/O area: " + helpers::intToHex(addr), "memory");
			return 0;
		}
	}
	else if (addr < 0x05000000)
	{
		//Unused area
		logging::warning("Tried to read from unused area: " + helpers::intToHex(addr), "memory");
		return 0;
	}
	else if (addr < 0x08000000)
	{
		return GPU->getVRAM(addr);
	}
	else if (addr < 0x0A000000)
	{
		//ROM Wait State 0
		return get8Cart(addr - 0x08000000);
	}
	else if (addr < 0x0C000000)
	{
		//ROM Wait State 1
		return get8Cart(addr - 0x0A000000);
	}
	else if (addr < 0x0E000000)
	{
		//ROM Wait State 2
		return get8Cart(addr - 0x0C000000);
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
	logging::error("How did this happen", "memory");
	return 0;
}

uint16_t memory::get16(uint32_t addr)
{
	return (((uint16_t)get8(addr + 1)) << 8) | get8(addr);
}

uint32_t memory::get32(uint32_t addr)
{
	return ((uint32_t)(get8(addr + 3) << 24) | ((uint32_t)get8(addr + 2) << 16) | ((uint32_t)get8(addr + 1) << 8) | get8(addr));
}

void memory::set8(uint32_t addr, uint8_t value)
{
	if (addr < 0x4000)
	{
		logging::error("Tried to write BIOS area: " + helpers::intToHex(addr), "memory");
	}
	else if (addr < 0x02000000)
	{
		//Unused area
		logging::warning("Tried to write to unused area: " + helpers::intToHex(addr), "memory");
	}
	else if (addr < 0x02040000)
	{
		ewram[addr - 0x02000000] = value;
	}
	else if (addr < 0x03000000)
	{
		//EWRAM mirrors
		ewram[(addr - 0x02000000) % 0x40000] = value;
	}
	else if (addr < 0x03008000)
	{
		iwram[addr - 0x03000000] = value;
	}
	else if (addr < 0x04000000)
	{
		//IWRAM mirrors
		iwram[(addr - 0x03000000) % 0x8000] = value;
	}
	else if (addr < 0x04000400)
	{
		// IO area
		if (addr < 0x04000060)
		{
			GPU->setRegister(addr, value);
		}
		else if (addr < 0x40000B0)
		{
			logging::error("Tried to set sound register: " + helpers::intToHex(addr), "memory");
		}
		else if (addr < 0x4000100)
		{
			logging::error("Tried to set DMA register: " + helpers::intToHex(addr), "memory");
		}
		else if (addr < 0x4000120)
		{
			logging::error("Tried to set timer register: " + helpers::intToHex(addr), "memory");
		}
		else if (addr < 0x4000130)
		{
			logging::error("Tried to set serial area 1: " + helpers::intToHex(addr), "memory");
		}
		else if (addr < 0x4000134)
		{
			Input->setRegister(addr, value);
		}
		else if (addr < 0x4000200)
		{
			logging::error("Tried to set serial area 2: " + helpers::intToHex(addr), "memory");
		}
		else if (addr < 0x4000804)
		{
			Interrupt->setRegister(addr, value);
		}
		else
		{
			logging::error("Tried to set unused I/O area: " + helpers::intToHex(addr), "memory");
		}
	}
	else if (addr < 0x05000000)
	{
		//Unused area
		logging::warning("Tried to write to unused area: " + helpers::intToHex(addr), "memory");
	}
	else if (addr < 0x08000000)
	{
		GPU->setVRAM(addr, value);
	}
	else if (addr < 0x0A000000)
	{
		//ROM Wait State 0
		logging::error("Tried to write to Cart ROM: " + helpers::intToHex(addr), "memory");
	}
	else if (addr < 0x0C000000)
	{
		//ROM Wait State 1
		logging::error("Tried to write to Cart ROM: " + helpers::intToHex(addr), "memory");
	}
	else if (addr < 0x0E000000)
	{
		//ROM Wait State 2
		logging::error("Tried to write to Cart ROM: " + helpers::intToHex(addr), "memory");
	}
	else if (addr < 0E010000)
	{
		//Cart SRAM
		logging::warning("Tried to write to Cart SRAM: " + helpers::intToHex(addr), "memory");
	}
	else if (addr < 0xFFFFFFFF)
	{
		//Unused area
		logging::warning("Tried to write to unused area: " + helpers::intToHex(addr), "memory");
	}
}

void memory::set16(uint32_t addr, uint16_t value)
{
	set8(addr, value & 0xFF);
	set8(addr + 1, value >> 8);
}

void memory::set32(uint32_t addr, uint32_t value)
{
	set8(addr, value & 0xFF);
	set8(addr + 1, (value >> 8) & 0xFF);
	set8(addr + 2, (value >> 16) & 0xFF);
	set8(addr + 3, (value >> 24) & 0xFF);
}