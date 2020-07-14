#pragma once
#include <cstdint>

class gpu
{
	private:
		int cycleCounter;
		uint8_t currentScanline;
		uint8_t* paletteRAM;
		uint8_t* vram;
		uint8_t* objectRAM;
		uint8_t videoMode;
		bool vblank;
	public:
		gpu();
		~gpu();
		void step(int cycles);
		void setVRAM(uint32_t addr, uint8_t value);
		uint8_t getVRAM(uint32_t addr);
		void setRegister(uint32_t addr, uint8_t value);
		uint8_t getRegister(uint32_t addr);
};