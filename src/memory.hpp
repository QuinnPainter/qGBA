#pragma once
#include <cstdint>

class memory
{
	private:
		uint8_t* iwram;
		uint8_t* ewram;
		uint8_t* cartrom;
		uint32_t romSize;
		uint8_t get8Cart(uint32_t addr);
	public:
		memory(uint8_t* rom, uint32_t romSize);
		~memory();
		uint8_t get8(uint32_t addr);
		uint16_t get16(uint32_t addr);
		uint32_t get32(uint32_t addr);
		void set8(uint32_t addr, uint8_t value);
		void set16(uint32_t addr, uint16_t value);
		void set32(uint32_t addr, uint32_t value);
};