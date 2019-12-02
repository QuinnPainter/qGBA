#include "arm7tdmi.hpp"
#include "logging.hpp"
#include "helpers.hpp"

constexpr uint32_t Nflag = 0x80000000;
constexpr uint32_t Zflag = 0x40000000;
constexpr uint32_t Cflag = 0x20000000;
constexpr uint32_t Vflag = 0x10000000;

arm7tdmi::arm7tdmi(memory* mem, bool bios)
{
	Memory = mem;
	if (bios)
	{
		state = {
			{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, //R
			0x13, //CPSR - Start in Supervisor mode
			0, //R13_svc
			0, //R14_svc
			0, //SPSR_svc
			0, //R13_irq
			0, //R14_irq
			0, //SPSR_irq
		};
	}
	else
	{
		//https://problemkaputt.de/gbatek.htm#biosramusage
		state = {
			{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3007F00, 0, 0x08000000}, //R
			0x1F, //CPSR - Start in System mode (should this be User? hmm...)
			0x3007FE0, //R13_svc
			0, //R14_svc
			0, //SPSR_svc
			0x3007FA0, //R13_irq
			0, //R14_irq
			0, //SPSR_irq
		};
	}
}

bool arm7tdmi::checkCondCode(uint32_t instr)
{
	switch (instr >> 28)
	{
		case 0b0000: //EQ
			return state.CPSR & Zflag;
		case 0b0001: //NE
			return !(state.CPSR & Zflag);
		case 0b0010: //CS
			return state.CPSR & Cflag;
		case 0b0011: //CC
			return !(state.CPSR & Cflag);
		case 0b0100: //MI
			return state.CPSR & Nflag;
		case 0b0101: //PL
			return !(state.CPSR & Nflag);
		case 0b0110: //VS
			return state.CPSR & Vflag;
		case 0b0111: //VC
			return !(state.CPSR & Vflag);
		case 0b1000: //HI
			return (state.CPSR & Cflag) && !(state.CPSR & Zflag);
		case 0b1001: //LS
			return !(state.CPSR & Cflag) || (state.CPSR & Zflag);
		case 0b1010: //GE
			return (bool)(state.CPSR & Nflag) == (bool)(state.CPSR & Vflag);
		case 0b1011: //LT
			return (bool)(state.CPSR & Nflag) != (bool)(state.CPSR & Vflag);
		case 0b1100: //GT
			return !(bool)(state.CPSR & Zflag) && ((bool)(state.CPSR & Nflag) == (bool)(state.CPSR & Vflag));
		case 0b1101: //LE
			return (bool)(state.CPSR & Zflag) || ((bool)(state.CPSR & Nflag) != (bool)(state.CPSR & Vflag));
		case 0b1110: //AL
			return true;
		case 0b1111: //invalid
			logging::error("Invalid condition code at " + helpers::intToHex(state.R[15] - 8), "arm7tdmi");
			return true;
	}
}

uint32_t arm7tdmi::getReg(int index)
{
	if (index == 13)
	{
		switch (state.CPSR & 0xF)
		{
			case 0b0010: return state.R13_irq;
			case 0b0011: return state.R13_svc;
			default: return state.R[13];
		}
	}
	else if (index == 14)
	{
		switch (state.CPSR & 0xF)
		{
			case 0b0010: return state.R14_irq;
			case 0b0011: return state.R14_svc;
			default: return state.R[14];
		}
	}
	else
	{
		return state.R[index];
	}
}

void arm7tdmi::setReg(int index, uint32_t value)
{
	if (index == 13)
	{
		switch (state.CPSR & 0xF)
		{
			case 0b0010: state.R13_irq = value; break;
			case 0b0011: state.R13_svc = value; break;
			default: state.R[13] = value; break;
		}
	}
	else if (index == 14)
	{
		switch (state.CPSR & 0xF)
		{
			case 0b0010: state.R14_irq = value; break;
			case 0b0011: state.R14_svc = value; break;
			default: state.R[14] = value; break;
		}
	}
	else
	{
		state.R[index] = value;
		if (index == 15)
		{
			Pipeline.valid = false;
		}
	}
}

void arm7tdmi::step()
{
	if (Pipeline.valid)
	{
		//Progress the pipeline
		state.R[15] += 4;
		Pipeline.executeInstr = Pipeline.decodeInstr;
		Pipeline.decodeInstr = Pipeline.fetchInstr;
		Pipeline.fetchInstr = Memory->get32(state.R[15]);
	}
	else
	{
		//Rebuild the pipeline
		Pipeline.executeInstr = Memory->get32(state.R[15]);
		Pipeline.decodeInstr = Memory->get32(state.R[15] + 4);
		Pipeline.fetchInstr = Memory->get32(state.R[15] + 8);
		state.R[15] += 8;
		Pipeline.valid = true;
	}

	if (checkCondCode(Pipeline.executeInstr))
	{
		logging::info("Execute instruction: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
		if ((Pipeline.executeInstr & 0xA000000) == 0xA000000)
		{
			ARM_Branch();
		}
	}
}

// ARM Instructions

void arm7tdmi::ARM_Branch()
{
	int32_t offset = helpers::signExtend(Pipeline.executeInstr & 0xFFFFFF, 24) << 2;
	if (Pipeline.executeInstr & 0x1000000)
	{
		//Branch with Link
		setReg(14, getReg(15) - 4);
		setReg(15, getReg(15) + offset);
		logging::info("Branched and linked to " + helpers::intToHex(getReg(15)), "arm7tdmi");
	}
	else
	{
		//Branch
		setReg(15, getReg(15) + offset);
		logging::info("Branched to " + helpers::intToHex(getReg(15)), "arm7tdmi");
	}
}