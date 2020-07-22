#include "dma.hpp"
#include "logging.hpp"
#include "memory.hpp"

dma::dma(interrupt* Interrupt)
{
	this->Interrupt = Interrupt;
	channel0.init(0, Interrupt);
	channel1.init(1, Interrupt);
	channel2.init(2, Interrupt);
	channel3.init(3, Interrupt);
}

void dma::setMemory(memory* Memory)
{
	this->Memory = Memory;
	channel0.Memory = Memory;
	channel1.Memory = Memory;
	channel2.Memory = Memory;
	channel3.Memory = Memory;
}

void dma::setRegister(uint32_t addr, uint8_t value)
{
	if (addr < 0x40000BC)
	{
		channel0.setRegister(addr - 0x4000000, value);
	}
	else if (addr < 0x40000C8)
	{
		channel1.setRegister(addr - 0x400000C, value);
	}
	else if (addr < 0x40000D4)
	{
		channel2.setRegister(addr - 0x4000018, value);
	}
	else if (addr < 0x40000E0)
	{
		channel3.setRegister(addr - 0x4000024, value);
	}
	else
	{
		logging::error("Write invalid DMA register", "dma");
	}
}

uint8_t dma::getRegister(uint32_t addr)
{
	if (addr < 0x40000BC)
	{
		return channel0.getRegister(addr - 0x4000000);
	}
	else if (addr < 0x40000C8)
	{
		return channel1.getRegister(addr - 0x400000C);
	}
	else if (addr < 0x40000D4)
	{
		return channel2.getRegister(addr - 0x4000018);
	}
	else if (addr < 0x40000E0)
	{
		return channel3.getRegister(addr - 0x4000024);
	}
	else
	{
		logging::error("Read invalid DMA register", "dma");
		return 0;
	}
}

void dma::videoBlank(bool vblank)
{
	channel0.videoBlank(vblank);
	channel1.videoBlank(vblank);
	channel2.videoBlank(vblank);
	channel3.videoBlank(vblank);
}

void dma::step(int cycles)
{
	channel0.step(cycles);
	channel1.step(cycles);
	channel2.step(cycles);
	channel3.step(cycles);
}

void dmaChannel::init(int channelNum, interrupt* Interrupt)
{
	this->channelNum = channelNum;
	this->Interrupt = Interrupt;
	switch (channelNum)
	{
		case 0: srcAddrMask = 0x7FFFFFF; dstAddrMask = 0x7FFFFFF; wordCountMask = 0x3FFF; break;
		case 1: srcAddrMask = 0xFFFFFFF; dstAddrMask = 0x7FFFFFF; wordCountMask = 0x3FFF; break;
		case 2: srcAddrMask = 0xFFFFFFF; dstAddrMask = 0x7FFFFFF; wordCountMask = 0x3FFF; break;
		case 3: srcAddrMask = 0xFFFFFFF; dstAddrMask = 0xFFFFFFF; wordCountMask = 0xFFFF; break;
	}
	srcAddr = 0;
	dstAddr = 0;
	enabled = false;
	dmaWaitCounter = 0;
}

void dmaChannel::setRegister(uint8_t addr, uint8_t value)
{
	switch (addr)
	{
		case 0xB0: setAddrByte(value, 0, true); break;
		case 0xB1: setAddrByte(value, 1, true);	break;
		case 0xB2: setAddrByte(value, 2, true);	break;
		case 0xB3: setAddrByte(value, 3, true);	break;
		case 0xB4: setAddrByte(value, 0, false); break;
		case 0xB5: setAddrByte(value, 1, false); break;
		case 0xB6: setAddrByte(value, 2, false); break;
		case 0xB7: setAddrByte(value, 3, false); break;
		case 0xB8: setWordCount(value, true); break;
		case 0xB9: setWordCount(value, false); break;
		case 0xBA: setControl(value, true); break;
		case 0xBB: setControl(value, false); break;
		default:
			logging::error("Invalid dmaChannel reg write. This shouldn't happen.", "dma");
			break;
	}
}

uint8_t dmaChannel::getRegister(uint8_t addr)
{
	switch (addr)
	{
		case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4:
		case 0xB5: case 0xB6: case 0xB7: case 0xB8: case 0xB9:
			return 0; // Write only
		case 0xBA: return getControl(true);
		case 0xBB: return getControl(false);
		default:
			logging::error("Invalid dmaChannel reg read. This shouldn't happen.", "dma");
			return 0;
	}
}

void dmaChannel::setAddrByte(uint8_t value, int byteNum, bool isSrcAddr)
{
	if (isSrcAddr) // Source Address
	{
		switch (byteNum)
		{
			case 0: srcAddr = (srcAddr & 0xFFFFFF00) | value; break;
			case 1: srcAddr = (srcAddr & 0xFFFF00FF) | ((uint32_t)value << 8); break;
			case 2: srcAddr = (srcAddr & 0xFF00FFFF) | ((uint32_t)value << 16); break;
			case 3: srcAddr = (srcAddr & 0x00FFFFFF) | ((uint32_t)value << 24); break;
		}
		srcAddr &= srcAddrMask;
	}
	else // Destination Address
	{
		switch (byteNum)
		{
			case 0: dstAddr = (dstAddr & 0xFFFFFF00) | value; break;
			case 1: dstAddr = (dstAddr & 0xFFFF00FF) | ((uint32_t)value << 8); break;
			case 2: dstAddr = (dstAddr & 0xFF00FFFF) | ((uint32_t)value << 16); break;
			case 3: dstAddr = (dstAddr & 0x00FFFFFF) | ((uint32_t)value << 24); break;
		}
		dstAddr &= dstAddrMask;
	}
}

void dmaChannel::setWordCount(uint8_t value, bool low)
{
	if (low) // Low Byte
	{
		wordCount = (wordCount & 0xFF00) | value;
	}
	else // High Byte
	{
		wordCount = (wordCount & 0x00FF) | ((uint16_t)value << 8);
	}
	wordCount &= wordCountMask;
}

void dmaChannel::setControl(uint8_t value, bool low)
{
	if (low) // Low Byte
	{
		dstAddrCtrl = (value >> 5) & 0x3;
		srcAddrCtrl = (srcAddrCtrl & 0x2) | ((value >> 7) & 0x1);
	}
	else // High Byte
	{
		bool oldEnable = enabled;
		srcAddrCtrl = (srcAddrCtrl & 0x1) | ((value << 1) & 0x2);
		repeat = value & 0x2;
		transferType = value & 0x4;
		startTiming = (value >> 4) & 0x3;
		irqOnFinish = value & 0x40;
		enabled = value & 0x80;

		if (!oldEnable && enabled)
		{
			srcAddrCounter = srcAddr;
			dstAddrCounter = dstAddr;
			wordCounter = wordCount;
			if (wordCount == 0)
			{
				wordCounter = wordCountMask + 1;
			}
		}
		if (srcAddrCtrl == 3)
		{
			logging::error("Invalid source address control", "dma");
		}
		if (startTiming == 1)
		{
			logging::important("Attempt to do VBlank DMA: not implemented", "dma");
		}
		if (startTiming == 2)
		{
			logging::important("Attempt to do HBlank DMA: not implemented", "dma");
		}
		if (startTiming == 3)
		{
			if (channelNum == 1 || channelNum == 2)
			{
				logging::important("Attempt to do sound FIFO DMA: not implemented", "dma");
			}
			else if (channelNum == 3)
			{
				logging::important("Attempt to do video capture DMA: not implemented", "dma");
			}
		}
		if (enabled && (startTiming == 0))
		{
			dmaWaitCounter = 2; // Wait 2 cycles before doing DMA
		}
	}
}

uint8_t dmaChannel::getControl(bool low)
{
	if (low) // Low Byte
	{
		uint8_t ret = (dstAddrCtrl << 5)
			| ((srcAddrCtrl & 0x1) << 7);
		return ret;
	}
	else // High Byte
	{
		uint8_t ret = ((srcAddrCtrl >> 1) & 0x1)
			| ((uint8_t)repeat << 1)
			| ((uint8_t)transferType << 2)
			| (startTiming << 4)
			| ((uint8_t)irqOnFinish << 6)
			| ((uint8_t)enabled << 7);
		return ret;
	}
}

void dmaChannel::doDMA()
{
	logging::important("DMA " + std::to_string(channelNum), "dma");
	int incrementAmount = transferType ? 4 : 2;
	for (; wordCounter > 0; wordCounter--)
	{
		if (transferType) // 32 bit
		{
			Memory->set32(dstAddrCounter, Memory->get32(srcAddrCounter));
		}
		else // 16 bit
		{
			Memory->set16(dstAddrCounter, Memory->get16(srcAddrCounter));
		}
		switch (dstAddrCtrl)
		{
			case 0: case 3: dstAddrCounter += incrementAmount; break;
			case 1: dstAddrCounter -= incrementAmount; break;
		}
		switch (srcAddrCtrl)
		{
			case 0: srcAddrCounter += incrementAmount; break;
			case 1: srcAddrCounter -= incrementAmount; break;
		}
	}
	if (irqOnFinish)
	{
		switch (channelNum)
		{
			case 0: Interrupt->requestInterrupt(interruptType::DMA0); break;
			case 1: Interrupt->requestInterrupt(interruptType::DMA1); break;
			case 2: Interrupt->requestInterrupt(interruptType::DMA2); break;
			case 3: Interrupt->requestInterrupt(interruptType::DMA3); break;
		}
	}
	if ((!repeat) || (startTiming == 0))
	{
		enabled = false;
	}
}

void dmaChannel::videoBlank(bool vblank)
{

}

void dmaChannel::step(int cycles)
{
	if (dmaWaitCounter > 0)
	{
		dmaWaitCounter -= cycles;
		if (dmaWaitCounter <= 0)
		{
			dmaWaitCounter = 0;
			doDMA();
		}
	}
}