#pragma once
#include "interrupt.hpp"

class timer
{
	private:
		interrupt* Interrupt;
		timer* nextTimer;
		int timerNum;

		uint16_t reload;
		uint16_t counter;
		uint8_t prescaler;
		bool countUpTiming;
		bool irqEnable;
		bool timerStart;

		int prescalerCounter;
	public:
		void init(interrupt* Interrupt, timer* nextTimer, int timerNum);
		void step(int cycles);
		void tick();
		void setControl(uint8_t value);
		uint8_t getControl();
		void setCounterLow(uint8_t value);
		void setCounterHigh(uint8_t value);
		uint8_t getCounterLow();
		uint8_t getCounterHigh();
};

class timers
{
	private:
		interrupt* Interrupt;
		timer timer0;
		timer timer1;
		timer timer2;
		timer timer3;
	public:
		timers(interrupt* Interrupt);
		void step(int cycles);
		void setRegister(uint32_t addr, uint8_t value);
		uint8_t getRegister(uint32_t addr);
};