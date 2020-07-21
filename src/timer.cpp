#include "timer.hpp"
#include "logging.hpp"
#include "helpers.hpp"

timers::timers(interrupt* Interrupt)
{
	this->Interrupt = Interrupt;
	timer0.init(Interrupt, &timer1, 0);
	timer1.init(Interrupt, &timer2, 1);
	timer2.init(Interrupt, &timer3, 2);
	timer3.init(Interrupt, nullptr, 3);
}

void timers::step(int cycles)
{
	timer0.step(cycles);
	timer1.step(cycles);
	timer2.step(cycles);
	timer3.step(cycles);
}

void timers::setRegister(uint32_t addr, uint8_t value)
{
	switch (addr - 0x4000000)
	{
		case 0x100: timer0.setCounterLow(value); break;
		case 0x101: timer0.setCounterHigh(value); break;
		case 0x102: timer0.setControl(value); break;
		case 0x103: break; // unused
		case 0x104: timer1.setCounterLow(value); break;
		case 0x105: timer1.setCounterHigh(value); break;
		case 0x106: timer1.setControl(value); break;
		case 0x107: break; // unused
		case 0x108: timer2.setCounterLow(value); break;
		case 0x109: timer2.setCounterHigh(value); break;
		case 0x10A: timer2.setControl(value); break;
		case 0x10B: break; // unused
		case 0x10C: timer3.setCounterLow(value); break;
		case 0x10D: timer3.setCounterHigh(value); break;
		case 0x10E: timer3.setControl(value); break;
		case 0x10F: break; // unused
		default:
			logging::error("Write invalid timer register: " + helpers::intToHex(addr), "timer");
			break;
	}
}

uint8_t timers::getRegister(uint32_t addr)
{
	switch (addr - 0x4000000)
	{
		case 0x100: return timer0.getCounterLow();
		case 0x101: return timer0.getCounterHigh();
		case 0x102: return timer0.getControl();
		case 0x103: return 0; // unused
		case 0x104: return timer1.getCounterLow();
		case 0x105: return timer1.getCounterHigh();
		case 0x106: return timer1.getControl();
		case 0x107: return 0; // unused
		case 0x108: return timer2.getCounterLow();
		case 0x109: return timer2.getCounterHigh();
		case 0x10A: return timer2.getControl();
		case 0x10B: return 0; // unused
		case 0x10C: return timer3.getCounterLow();
		case 0x10D: return timer3.getCounterHigh();
		case 0x10E: return timer3.getControl();
		case 0x10F: return 0; // unused
		default:
			logging::error("Read invalid timer register: " + helpers::intToHex(addr), "timer");
			return 0;
	}
}

void timer::init(interrupt* Interrupt, timer* nextTimer, int timerNum)
{
	this->Interrupt = Interrupt;
	this->nextTimer = nextTimer;
	this->timerNum = timerNum;
	reload = 0;
	counter = 0;
	prescaler = 0;
	countUpTiming = false;
	irqEnable = false;
	timerStart = false;
	prescalerCounter = 0;
}

void timer::step(int cycles)
{
	if (timerStart && !countUpTiming)
	{
		int prescalerSelection = 0;
		switch (prescaler)
		{
			case 0: prescalerSelection = 1; break;
			case 1: prescalerSelection = 64; break;
			case 2: prescalerSelection = 256; break;
			case 3: prescalerSelection = 1024; break;
		}
		prescalerCounter += cycles;
		while (prescalerCounter >= prescalerSelection)
		{
			prescalerCounter -= prescalerSelection;
			tick();
		}
	}
}

void timer::tick()
{
	counter++;
	if (counter == 0) // Counter overflowed
	{
		counter = reload;
		if (irqEnable)
		{
			switch (timerNum)
			{
				case 0: Interrupt->requestInterrupt(interruptType::Timer0); break;
				case 1: Interrupt->requestInterrupt(interruptType::Timer1); break;
				case 2: Interrupt->requestInterrupt(interruptType::Timer2); break;
				case 3: Interrupt->requestInterrupt(interruptType::Timer3); break;
			}
		}
		if (nextTimer != nullptr)
		{
			if ((*nextTimer).countUpTiming && (*nextTimer).timerStart)
			{
				nextTimer->tick();
			}
		}
	}
}

void timer::setControl(uint8_t value)
{
	bool oldTimerStart = timerStart;
	prescaler = value & 0x3;
	countUpTiming = value & 0x4;
	irqEnable = value & 0x40;
	timerStart = value & 0x80;
	if (!oldTimerStart && timerStart)
	{
		counter = reload;
	}
	prescalerCounter = 0; // not sure what should happen with this?
}

uint8_t timer::getControl()
{
	uint8_t ret = prescaler
		| ((uint8_t)countUpTiming << 2)
		| ((uint8_t)irqEnable << 6)
		| ((uint8_t)timerStart << 7);
	return ret;
}

void timer::setCounterLow(uint8_t value)
{
	reload = (reload & ~0xFF) | value;
}

void timer::setCounterHigh(uint8_t value)
{
	reload = (reload & 0xFF) | (((uint16_t)value) << 8);
}

uint8_t timer::getCounterLow()
{
	return counter & 0xFF;
}

uint8_t timer::getCounterHigh()
{
	return counter >> 8;
}