#pragma once
#include <cstdint>

class gpu
{
	private:
		uint8_t* paletteRAM;
		uint8_t* vram;
		uint8_t* objectRAM;
	public:
		gpu();
		~gpu();
		void setVRAM(uint32_t addr, uint8_t value);
		uint8_t getVRAM(uint32_t addr);
		void setRegister(uint32_t addr, uint8_t value);
		uint8_t getRegister(uint32_t addr);
};