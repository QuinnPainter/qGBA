#pragma once
#include "SDL.h"

class input
{
	private:
		uint16_t keyState;
		uint16_t keyInterruptState;
		void setStateBit(uint8_t index, bool value);
	public:
		input();
		void keyChanged(SDL_Keycode key, bool value);
		uint8_t getRegister(uint32_t addr);
		void setRegister(uint32_t addr, uint8_t value);
};