#pragma once
#include <cstdint>
#include "SDL.h"
#include "interrupt.hpp"

struct bgControl
{
	uint8_t priority;
	uint8_t charBaseBlock;
	bool mosaic;
	bool colourDepth; // 0 = 16 colours, 16 palettes. 1 = 256 colours, 1 palette.
	uint8_t screenBaseBlock;
	bool displayOverflow;
	uint8_t screenSize;
	
	void setLow(uint8_t value);
	void setHigh(uint8_t value);
	uint8_t getLow();
	uint8_t getHigh();
};

class gpu
{
	private:
		interrupt* Interrupt;
		SDL_Window* window;
		SDL_Renderer* screenRenderer;
		SDL_Texture* screenTexture;
		uint8_t* screenData;

		int cycleCounter;
		uint8_t currentScanline;
		uint8_t* paletteRAM;
		uint8_t* vram;
		uint8_t* objectRAM;
		uint8_t videoMode;
		bool bitmapFrame;
		bool enableBG0;
		bool enableBG1;
		bool enableBG2;
		bool enableBG3;
		bool enableOBJ;
		bgControl BG0Control;
		bgControl BG1Control;
		bgControl BG2Control;
		bgControl BG3Control;
		uint16_t BG0XOffset;
		uint16_t BG0YOffset;
		uint16_t BG1XOffset;
		uint16_t BG1YOffset;
		uint16_t BG2XOffset;
		uint16_t BG2YOffset;
		uint16_t BG3XOffset;
		uint16_t BG3YOffset;
		bool vblank;
		bool hblank;
		bool vcountMatch;
		uint8_t vCountSetting;
		bool vblankIRQEnable;
		bool hblankIRQEnable;
		bool vcountIRQEnable;

		void drawScanline();
		void plotPixel(uint8_t x, uint8_t y, uint16_t colour);
	public:
		gpu(interrupt* Interrupt);
		~gpu();
		void step(int cycles);
		void setVRAM(uint32_t addr, uint8_t value);
		uint8_t getVRAM(uint32_t addr);
		void setRegister(uint32_t addr, uint8_t value);
		uint8_t getRegister(uint32_t addr);
		void displayScreen();
};