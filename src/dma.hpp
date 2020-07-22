#pragma once
#include <cstdint>
#include "interrupt.hpp"

class memory;
class dmaChannel
{
	private:
		interrupt* Interrupt;

		int channelNum;
		uint32_t srcAddrMask;
		uint32_t dstAddrMask;
		uint16_t wordCountMask;

		uint32_t srcAddr;
		uint32_t dstAddr;
		uint16_t wordCount;
		uint8_t dstAddrCtrl; // start of Control register
		uint8_t srcAddrCtrl;
		bool repeat;
		bool transferType; // 0 = 16 bit words, 1 = 32 bit words
		uint8_t startTiming;
		bool irqOnFinish;
		bool enabled;

		uint32_t srcAddrCounter;
		uint32_t dstAddrCounter;
		uint32_t wordCounter;

		int dmaWaitCounter;

		void setAddrByte(uint8_t value, int byteNum, bool isSrcAddr);
		void setWordCount(uint8_t value, bool low);
		void setControl(uint8_t value, bool low);
		uint8_t getControl(bool low);
		void doDMA();
	public:
		memory* Memory;
		void init(int channelNum, interrupt* Interrupt);
		void setRegister(uint8_t addr, uint8_t value);
		uint8_t getRegister(uint8_t addr);
		void videoBlank(bool vblank);
		void step(int cycles);
};

class dma
{
	private:
		interrupt* Interrupt;
		memory* Memory;
		dmaChannel channel0;
		dmaChannel channel1;
		dmaChannel channel2;
		dmaChannel channel3;
	public:
		dma(interrupt* Interrupt);
		void setMemory(memory* Memory);
		void setRegister(uint32_t addr, uint8_t value);
		uint8_t getRegister(uint32_t addr);
		void videoBlank(bool vblank);
		void step(int cycles);
};