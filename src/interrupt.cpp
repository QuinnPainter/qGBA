#include "interrupt.hpp"
#include "logging.hpp"
#include "helpers.hpp"

interrupt::interrupt(bool* requestIRQ, bool* CPUHalt)
{
	this->requestIRQ = requestIRQ;
	this->CPUHalt = CPUHalt;
	interruptEnable = 0;
	interruptFlags = 0;
	interruptMasterEnable = false;
}

void interrupt::requestInterrupt(interruptType type)
{
	interruptFlags |= (1 << ((int)type));
	updateIRQRequest();
}

void interrupt::updateIRQRequest()
{
	bool interruptRequested = (interruptFlags & interruptEnable) != 0;
	if (interruptRequested)
	{
		(*CPUHalt) = false;
	}
	(*requestIRQ) = interruptRequested && interruptMasterEnable;
}

void interrupt::setRegister(uint32_t addr, uint8_t value)
{
	switch (addr - 0x4000000)
	{
		case 0x200: // Interrupt Enable byte 1
			interruptEnable = (interruptEnable & ~0xFF) | value;
			break;
		case 0x201: // Interrupt Enable byte 2
			interruptEnable = (interruptEnable & 0xFF) | (((uint16_t)value) << 8);
			break;
		case 0x202: // Interrupt Flag byte 1
			interruptFlags = (interruptFlags & ~0xFF) | ((interruptFlags & 0xFF) & (~value));
			break;
		case 0x203: // Interrupt Flag byte 2
			interruptFlags = (interruptFlags & 0xFF) | ((interruptFlags & ~0xFF) & (~((uint16_t)value << 8)));
			break;
		case 0x208: // Interrupt Master Enable byte 1
			interruptMasterEnable = value & 0x1;
			break;
		case 0x209: // Interrupt Master Enable byte 2
			break;
		case 0x301: // Low Power Mode Control
			if (value & 0x80)
			{
				logging::warning("Tried to enter STOP mode", "interrupt");
			}
			else
			{
				(*CPUHalt) = true;
			}
			break;
		default:
			logging::error("Write unimplemented control register: " + helpers::intToHex(addr), "interrupt");
			break;
	}
	updateIRQRequest();
}

uint8_t interrupt::getRegister(uint32_t addr)
{
	switch (addr - 0x4000000)
	{
		case 0x200: // Interrupt Enable byte 1
			return interruptEnable & 0xFF;
		case 0x201: // Interrupt Enable byte 2
			return interruptEnable >> 8;
		case 0x202: // Interrupt Flag byte 1
			return interruptFlags & 0xFF;
		case 0x203: // Interrupt Flag byte 2
			return interruptFlags >> 8;
		case 0x208: // Interrupt Master Enable byte 1
			return interruptMasterEnable;
		case 0x209: // Interrupt Master Enable byte 2
			return 0;
		case 0x301: // Low Power Mode Control - write only
			return 0;
		default:
			logging::error("Read unimplemented control register: " + helpers::intToHex(addr), "interrupt");
			return 0;
	}
}