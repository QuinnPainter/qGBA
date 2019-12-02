#pragma once
#include <cstdint>
#include "memory.hpp"

struct cpuState
{
	uint32_t R[16];
	uint32_t CPSR;
	uint32_t R13_svc;
	uint32_t R14_svc;
	uint32_t SPSR_svc;
	uint32_t R13_irq;
	uint32_t R14_irq;
	uint32_t SPSR_irq;
};

struct pipeline
{
	uint32_t fetchInstr = 0;
	uint32_t decodeInstr = 0;
	uint32_t executeInstr = 0;
	bool valid = false;
};

class arm7tdmi
{
	public:
		arm7tdmi(memory* mem, bool bios);
		void step();
	private:
		cpuState state;
		pipeline Pipeline;
		memory* Memory;
		bool checkCondCode(uint32_t instr);
		uint32_t getReg(int index);
		void setReg(int index, uint32_t value);

		//ARM instructions
		void ARM_Branch();
};