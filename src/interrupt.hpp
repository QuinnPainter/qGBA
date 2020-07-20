#pragma once
#include <cstdint>

enum class interruptType: int
{
	VBlank = 0,
	HBlank,
	VCounter,
	Timer0,
	Timer1,
	Timer2,
	Timer3,
	Serial,
	DMA0,
	DMA1,
	DMA2,
	DMA3,
	Keypad,
	GamePak
};

class interrupt
{
	public:
		interrupt(bool* requestIRQ, bool* CPUHalt);
		void requestInterrupt(interruptType type);
		void setRegister(uint32_t addr, uint8_t value);
		uint8_t getRegister(uint32_t addr);
	private:
		bool* requestIRQ;
		bool* CPUHalt;
		uint16_t interruptEnable;
		uint16_t interruptFlags;
		bool interruptMasterEnable;
		void updateIRQRequest();
};