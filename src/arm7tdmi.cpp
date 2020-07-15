#include "arm7tdmi.hpp"
#include "logging.hpp"
#include "helpers.hpp"

// good reference point for instructions:
// https://github.com/shonumi/gbe-plus/

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
			0x1F, //CPSR - Start in System mode
			0x3007FE0, //R13_svc
			0, //R14_svc
			0, //SPSR_svc
			0x3007FA0, //R13_irq
			0, //R14_irq
			0, //SPSR_irq
		};
	}
	flushPipeline();
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
	logging::error("what is this condition code", "armtdmi");
	return false;
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
			Pipeline.pendingFlush = true;
		}
	}
}

uint32_t arm7tdmi::getSPSR()
{
	switch (state.CPSR & 0xF)
	{
		case 0b0010: return state.SPSR_irq;
		case 0b0011: return state.SPSR_svc;
		default: logging::error("Can't get SPSR in User/System mode", "arm7tdmi"); return 0;
	}
}

void arm7tdmi::setSPSR(uint32_t value)
{
	switch (state.CPSR & 0xF)
	{
		case 0b0010: state.SPSR_irq = value; break;
		case 0b0011: state.SPSR_svc = value; break;
		default: logging::error("Can't set SPSR in User/System mode", "arm7tdmi"); break;
	}
}

void arm7tdmi::step()
{
	fetch();
	decode();
	execute();

	if (Pipeline.pendingFlush)
	{
		flushPipeline();
	}
	else
	{
		Pipeline.pipelinePtr = (Pipeline.pipelinePtr + 1) % 3;
		state.R[15] += (state.CPSR & 0x20) ? 2 : 4;
	}

	//logging::info(helpers::intToHex(getReg(15)) + " " + helpers::intToHex(currentInstr));
}

void arm7tdmi::fetch()
{
	if (state.CPSR & 0x20)
	{
		//THUMB
		Pipeline.instrPipeline[Pipeline.pipelinePtr] = Memory->get16(state.R[15]);
		Pipeline.instrOperation[Pipeline.pipelinePtr] = instruction::UNDEFINED;
	}
	else
	{
		//ARM
		Pipeline.instrPipeline[Pipeline.pipelinePtr] = Memory->get32(state.R[15]);
		Pipeline.instrOperation[Pipeline.pipelinePtr] = instruction::UNDEFINED;
	}
}

void arm7tdmi::decode()
{
	uint8_t pipelineIndex = (Pipeline.pipelinePtr + 2) % 3;
	if (Pipeline.instrOperation[pipelineIndex] == instruction::PIPELINE_FILL) { return; }
	// if it doesn't match any criteria, leave undefined
	Pipeline.instrOperation[pipelineIndex] = instruction::UNDEFINED;

	if (state.CPSR & 0x20)
	{
		//THUMB
		uint16_t currentInstr = Pipeline.instrPipeline[pipelineIndex];
		switch ((currentInstr >> 13) & 0b111)
		{
			case 0b000:
			{
				if ((currentInstr & 0x1800) == 0x1800)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_2;
				}
				else
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_1;
				}
				break;
			}
			case 0b001:
			{
				Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_3;
				break;
			}
			case 0b010:
			{
				uint8_t checkBits = (currentInstr >> 10) & 0b111;
				if (checkBits == 0b000)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_4;
				}
				else if (checkBits == 0b001)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_5;
				}
				else if ((checkBits & 0b110) == 0b010)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_6;
				}
				else if ((checkBits & 0b100) == 0b100)
				{
					if (currentInstr & 0x200)
					{
						Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_8;
					}
					else
					{
						Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_7;
					}
				}
				break;
			}
			case 0b011:
			{
				Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_9;
				break;
			}
			case 0b100:
			{
				if (currentInstr & 0x1000)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_11;
				}
				else
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_10;
				}
				break;
			}
			case 0b101:
			{
				if (currentInstr & 0x1000)
				{
					if (currentInstr & 0x400)
					{
						Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_14;
					}
					else
					{
						Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_13;
					}
				}
				else
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_12;
				}
				break;
			}
			case 0b110:
			{
				if (currentInstr & 0x1000)
				{
					if ((currentInstr & 0x0F00) == 0x0F00)
					{
						Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_17;
					}
					else
					{
						Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_16;
					}
				}
				else
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_15;
				}
				break;
			}
			case 0b111:
			{
				if (currentInstr & 0x1000)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_19;
				}
				else
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::THUMB_18;
				}
				break;
			}
		}
	}
	else
	{
		//ARM
		uint32_t currentInstr = Pipeline.instrPipeline[pipelineIndex];

		if (((currentInstr >> 8) & 0xFFFFF) == 0x12FFF)
		{
			Pipeline.instrOperation[pipelineIndex] = instruction::ARM_3;
		}
		else if (((currentInstr >> 25) & 0x7) == 0x5)
		{
			Pipeline.instrOperation[pipelineIndex] = instruction::ARM_4;
		}
		else if ((currentInstr & 0xD900000) == 0x1000000)
		{
			if ((currentInstr & 0x80) && (currentInstr & 0x10) && ((currentInstr & 0x2000000) == 0))
			{
				if (((currentInstr >> 5) & 0x3) == 0)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::ARM_12;
				}
				else
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::ARM_10;
				}
			}
			else
			{
				// This is ARM6 - PSR Transfer. It's part of ARM5 - Data Processing.
				Pipeline.instrOperation[pipelineIndex] = instruction::ARM_5;
			}

		}
		else if (((currentInstr >> 26) & 0x3) == 0x0)
		{
			if ((currentInstr & 0x80) && ((currentInstr & 0x10) == 0))
			{
				if (currentInstr & 0x2000000)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::ARM_5;
				}
				else if ((currentInstr & 0x100000) && (((currentInstr >> 23) & 0x3) == 0x2))
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::ARM_5;
				}
				else if (((currentInstr >> 23) & 0x3) != 0x2)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::ARM_5;
				}
				else
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::ARM_7;
				}
			}
			else if ((currentInstr & 0x80) && (currentInstr & 0x10))
			{
				if (((currentInstr >> 4) & 0xF) == 0x9)
				{
					if (currentInstr & 0x2000000)
					{
						Pipeline.instrOperation[pipelineIndex] = instruction::ARM_5;
					}
					else if (((currentInstr >> 23) & 0x3) == 0x2)
					{
						Pipeline.instrOperation[pipelineIndex] = instruction::ARM_12;
					}
					else
					{
						Pipeline.instrOperation[pipelineIndex] = instruction::ARM_7;
					}
				}
				else if (currentInstr & 0x2000000)
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::ARM_5;
				}
				else
				{
					Pipeline.instrOperation[pipelineIndex] = instruction::ARM_10;
				}
			}
			else
			{
				Pipeline.instrOperation[pipelineIndex] = instruction::ARM_5;
			}
		}
		else if (((currentInstr >> 26) & 0x3) == 0x1)
		{
			Pipeline.instrOperation[pipelineIndex] = instruction::ARM_9;
		}
		else if (((currentInstr >> 25) & 0x7) == 0x4)
		{
			Pipeline.instrOperation[pipelineIndex] = instruction::ARM_11;
		}
		else if (((currentInstr >> 24) & 0xF) == 0xF)
		{
			Pipeline.instrOperation[pipelineIndex] = instruction::ARM_13;
		}
	}
}

void arm7tdmi::execute()
{
	uint8_t pipelineIndex = (Pipeline.pipelinePtr + 1) % 3;
	if (Pipeline.instrOperation[pipelineIndex] == instruction::PIPELINE_FILL) { return; }

	//logging::info(helpers::intToHex(Pipeline.instrPipeline[pipelineIndex]));

	if (state.CPSR & 0x20)
	{
		//THUMB
		uint16_t currentInstruction = Pipeline.instrPipeline[pipelineIndex];
		switch (Pipeline.instrOperation[pipelineIndex])
		{
			case instruction::THUMB_1: THUMB_MoveShiftedRegister(currentInstruction); break;
			case instruction::THUMB_2: THUMB_AddSubtract(currentInstruction); break;
			case instruction::THUMB_3: THUMB_MvCmpAddSubImmediate(currentInstruction); break;
			case instruction::THUMB_4: THUMB_ALUOps(currentInstruction); break;
			case instruction::THUMB_5: THUMB_HiRegOps_BranchExchange(currentInstruction); break;
			case instruction::THUMB_6: THUMB_LoadPCRelative(currentInstruction); break;
			case instruction::THUMB_7: THUMB_LoadStoreRegOffset(currentInstruction); break;
			case instruction::THUMB_8: THUMB_LoadStoreSignExtend(currentInstruction); break;
			case instruction::THUMB_9: THUMB_LoadStoreImmediate(currentInstruction); break;
			case instruction::THUMB_10: THUMB_LoadStoreHalfword(currentInstruction); break;
			case instruction::THUMB_11: THUMB_LoadStoreSPRelative(currentInstruction); break;
			case instruction::THUMB_12: THUMB_LoadAddress(currentInstruction); break;
			case instruction::THUMB_13: THUMB_AddOffsetSP(currentInstruction); break;
			case instruction::THUMB_14: THUMB_PushPop(currentInstruction); break;
			case instruction::THUMB_15: THUMB_MultipleLoadStore(currentInstruction); break;
			case instruction::THUMB_16: THUMB_ConditionalBranch(currentInstruction); break;
			case instruction::THUMB_17: logging::error("Unimplemented THUMB instruction: Software Interrupt: " + helpers::intToHex(currentInstruction), "arm7tdmi"); break;
			case instruction::THUMB_18: THUMB_UnconditionalBranch(currentInstruction); break;
			case instruction::THUMB_19: THUMB_LongBranchLink(currentInstruction); break;
			default: logging::fatal("Invalid instruction in THUMB pipeline", "arm7tdmi"); break;
		}
	}
	else
	{
		//ARM
		uint32_t currentInstruction = Pipeline.instrPipeline[pipelineIndex];
		if (checkCondCode(currentInstruction))
		{
			switch (Pipeline.instrOperation[pipelineIndex])
			{
				case instruction::ARM_3: ARM_BranchExchange(currentInstruction); break;
				case instruction::ARM_4: ARM_Branch(currentInstruction); break;
				case instruction::ARM_5: ARM_DataProcessing(currentInstruction); break;
				case instruction::ARM_7: ARM_Multiply(currentInstruction); break;
				case instruction::ARM_9: ARM_SingleDataTransfer(currentInstruction); break;
				case instruction::ARM_10: ARM_HalfwordDataTransfer(currentInstruction); break;
				case instruction::ARM_11: ARM_BlockDataTransfer(currentInstruction); break;
				case instruction::ARM_12: ARM_SingleDataSwap(currentInstruction); break;
				case instruction::ARM_13: logging::error("Unimplemented instruction: Software Interrupt: " + helpers::intToHex(currentInstruction), "arm7tdmi"); break;
				default: logging::fatal("Invalid instruction in ARM pipeline", "arm7tdmi"); break;
			}
		}
	}
}

void arm7tdmi::flushPipeline()
{
	Pipeline.pendingFlush = false;
	Pipeline.pipelinePtr = 0;
	Pipeline.instrPipeline[0] = 0;
	Pipeline.instrPipeline[1] = 0;
	Pipeline.instrPipeline[2] = 0;
	Pipeline.instrOperation[0] = instruction::PIPELINE_FILL;
	Pipeline.instrOperation[1] = instruction::PIPELINE_FILL;
	Pipeline.instrOperation[2] = instruction::PIPELINE_FILL;
}

// ARM Instructions

void arm7tdmi::ARM_BranchExchange(uint32_t currentInstruction)
{
	uint8_t srcReg = currentInstruction & 0xF;
	if (srcReg == 15)
	{
		logging::error("ARM_BranchExchange: R15 used as input", "arm7tdmi");
	}
	uint32_t addr = getReg(srcReg);
	setReg(15, addr & (~0x1));
	if (addr & 0x1)
	{
		state.CPSR |= 0x20;
	}
}

void arm7tdmi::ARM_Branch(uint32_t currentInstruction)
{
	uint32_t offset = helpers::signExtend((currentInstruction & 0xFFFFFF) << 2, 26);
	//uint32_t offset = (currentInstruction & 0xFFFFFF) << 2;
	//if (offset & 0x2000000) { offset |= 0xFC000000; }

	if (currentInstruction & 0x1000000)
	{
		//Branch with Link
		setReg(14, getReg(15) - 4);
		setReg(15, getReg(15) + offset);
	}
	else
	{
		//Branch
		setReg(15, getReg(15) + offset);
	}
}

void arm7tdmi::ARM_DataProcessing(uint32_t currentInstruction)
{
	bool setFlag = currentInstruction & 0x100000;
	uint8_t opcode = (currentInstruction >> 21) & 0xF;
	uint8_t srcReg = (currentInstruction >> 16) & 0xF;
	uint32_t operand1 = getReg(srcReg);
	uint8_t destReg = (currentInstruction >> 12) & 0xF;
	uint32_t operand2;
	if (!setFlag && (opcode >> 2) == 0b10)
	{
		ARM_PSRTransfer(currentInstruction);
		return;
	}
	bool shiftImmediate = !((bool)(currentInstruction & 0x10));

	int shiftCarryOut = 2;
	if (currentInstruction & 0x2000000)
	{
		//Immediate Value
		uint32_t value = currentInstruction & 0xFF;
		uint8_t shift = (currentInstruction >> 8) & 0xF;

		rotateRightSpecial(&value, shift);
		operand2 = value;
	}
	else
	{
		//Register
		operand2 = getReg(currentInstruction & 0xF);
		uint8_t shiftInfo = (currentInstruction >> 5) & 0x3;

		uint8_t shiftAmount;
		if (shiftImmediate)
		{
			shiftAmount = (currentInstruction >> 7) & 0x1F;
		}
		else
		{
			if (((currentInstruction >> 8) & 0xF) == 15)
			{
				logging::error("ARM_DataProcessing: Shift amount can't be PC", "arm7tdmi");
			}

			if (srcReg == 15) { operand1 += 4; }
			if ((currentInstruction & 0xF) == 15) { operand2 += 4; }

			shiftAmount = getReg((currentInstruction >> 8) & 0xF);
		}

		if (!(!shiftImmediate && (shiftAmount == 0)))
		{
			switch (shiftInfo)
			{
				case 0b00: shiftCarryOut = logicalShiftLeft(&operand2, shiftAmount); break;
				case 0b01: shiftCarryOut = logicalShiftRight(&operand2, shiftAmount); break;
				case 0b10: shiftCarryOut = arithmeticShiftRight(&operand2, shiftAmount); break;
				case 0b11: shiftCarryOut = rotateRight(&operand2, shiftAmount); break;
			}
		}
	}

	if (setFlag && (destReg == 15))
	{
		state.CPSR = getSPSR();
		setFlag = false;
	}

	uint32_t result;
	bool carryIn = (shiftCarryOut < 2) ? shiftCarryOut : state.CPSR & Cflag;
	switch (opcode)
	{
		case 0b0000: //AND
			result = operand1 & operand2;
			setReg(destReg, result);
			if (setFlag) { setFlagsLogical(result, shiftCarryOut); }
			break;
		case 0b0001: //EOR
			result = operand1 ^ operand2;
			setReg(destReg, result);
			if (setFlag) { setFlagsLogical(result, shiftCarryOut); }
			break;
		case 0b0010: //SUB
			result = operand1 - operand2;
			setReg(destReg, result);
			if (setFlag) { setFlagsArithmetic(operand1, operand2, result, false); }
			break;
		case 0b0011: //RSB
			result = operand2 - operand1;
			setReg(destReg, result);
			if (setFlag) { setFlagsArithmetic(operand2, operand1, result, false); }
			break;
		case 0b0100: //ADD
			result = operand1 + operand2;
			setReg(destReg, result);
			if (setFlag) { setFlagsArithmetic(operand1, operand2, result, true); }
			break;
		case 0b0101: //ADC
			result = operand1 + operand2 + carryIn;
			setReg(destReg, result);
			if (setFlag) { setFlagsArithmetic(operand1, operand2 + carryIn, result, true); }
			break;
		case 0b0110: //SBC
			result = operand1 - operand2 + carryIn - 1;
			setReg(destReg, result);
			if (setFlag) { setFlagsArithmetic(operand1, operand2 + carryIn - 1, result, false); }
			break;
		case 0b0111: //RSC
			result = operand2 - operand1 + carryIn - 1;
			setReg(destReg, result);
			if (setFlag) { setFlagsArithmetic(operand2, operand1 + carryIn - 1, result, false); }
			break;
		case 0b1000: //TST
			result = operand1 & operand2;
			setFlagsLogical(result, shiftCarryOut);
			break;
		case 0b1001: //TEQ
			result = operand1 ^ operand2;
			setFlagsLogical(result, shiftCarryOut);
			break;
		case 0b1010: //CMP
			result = operand1 - operand2;
			setFlagsArithmetic(operand1, operand2, result, false);
			break;
		case 0b1011: //CMN
			result = operand1 + operand2;
			setFlagsArithmetic(operand1, operand2, result, true);
			break;
		case 0b1100: //ORR
			result = operand1 | operand2;
			setReg(destReg, result);
			if (setFlag) { setFlagsLogical(result, shiftCarryOut); }
			break;
		case 0b1101: //MOV
			setReg(destReg, operand2);
			if (setFlag) { setFlagsLogical(operand2, shiftCarryOut); }
			break;
		case 0b1110: //BIC
			result = operand1 & ~operand2;
			setReg(destReg, result);
			if (setFlag) { setFlagsLogical(result, shiftCarryOut); }
			break;
		case 0b1111: //MVN
			result = ~operand2;
			setReg(destReg, result);
			if (setFlag) { setFlagsLogical(result, shiftCarryOut); }
			break;
	}

	if (destReg == 15)
	{
		if (state.R[15] & 0x1)
		{
			state.CPSR |= 0x20;
			setReg(15, getReg(15) & ~0x1);
		}
		else
		{
			setReg(15, getReg(15) & ~0x3);
		}
	}
}

void arm7tdmi::ARM_PSRTransfer(uint32_t currentInstruction)
{
	bool PSR = currentInstruction & 0x400000; //0 = CPSR  1 = SPSR
	bool immediate = currentInstruction & 0x2000000;
	bool opcode = currentInstruction & 0x200000;

	if (opcode) // MSR
	{
		uint32_t input = 0;
		uint32_t fieldMask = 0;
		if (currentInstruction & 0x80000) { fieldMask |= 0xFF000000; }
		if (currentInstruction & 0x40000) { fieldMask |= 0x00FF0000; }
		if (currentInstruction & 0x20000) { fieldMask |= 0x0000FF00; }
		if (currentInstruction & 0x10000) { fieldMask |= 0x000000FF; }

		if (immediate)
		{
			input = currentInstruction & 0xFF;
			uint8_t shift = (currentInstruction >> 8) & 0xF;
			rotateRightSpecial(&input, shift);
		}
		else
		{
			uint8_t srcReg = currentInstruction & 0xF;
			if (srcReg == 15)
			{
				logging::error("MSR source register can't be R15", "arm7tdmi");
			}
			input = getReg(srcReg);
			input &= fieldMask;
		}

		if (PSR)
		{
			uint32_t temp = getSPSR();
			temp &= ~fieldMask;
			temp |= input;
			setSPSR(temp);
		}
		else
		{
			state.CPSR &= ~fieldMask;
			state.CPSR |= input;
			if (state.CPSR & 0x20)
			{
				// Switch to THUMB
				setReg(15, getReg(15) & ~0x1); // also flushes pipeline
			}
		}
	}
	else // MRS (transfer PSR to register)
	{
		uint8_t destReg = (currentInstruction >> 12) & 0xF;
		if (destReg == 15)
		{
			logging::error("MRS destination register can't be R15", "arm7tdmi");
		}
		if (PSR)
		{
			setReg(destReg, getSPSR());
		}
		else
		{
			setReg(destReg, state.CPSR);
		}
	}
}

void arm7tdmi::ARM_Multiply(uint32_t currentInstruction)
{
	uint8_t op_rm_reg = (currentInstruction) & 0xF;
	uint8_t op_rs_reg = ((currentInstruction >> 8) & 0xF);
	uint8_t accu_reg = ((currentInstruction >> 12) & 0xF);
	uint8_t dest_reg = ((currentInstruction >> 16) & 0xF);
	bool set_condition = (currentInstruction & 0x100000);
	uint8_t op_code = ((currentInstruction >> 21) & 0xF);

	//Make sure no operand or destination register is R15
	if (op_rm_reg == 15) { logging::error("Multiply: R15 used as Rm", "arm7tdmi"); }
	if (op_rs_reg == 15) { logging::error("Multiply: R15 used as Rs", "arm7tdmi"); }
	if (accu_reg == 15) {  logging::error("Multiply: R15 used as Rn", "arm7tdmi"); }
	if (dest_reg == 15) {  logging::error("Multiply: R15 used as Rd", "arm7tdmi"); }

	uint32_t Rm = getReg(op_rm_reg);
	uint32_t Rs = getReg(op_rs_reg);
	uint32_t Rn = getReg(accu_reg);
	uint32_t Rd = getReg(dest_reg);

	uint64_t value_64 = 1;
	uint64_t hi_lo = 0;
	int64_t value_s64 = 1;
	uint32_t value_32 = 0;

	switch (op_code)
	{
		case 0x0: //MUL
			value_32 = (Rm * Rs);
			setReg(dest_reg, value_32);

			if (set_condition)
			{
				setFlagsLogical(value_32, 2);
			}
			break;
		case 0x1: //MLA
			value_32 = (Rm * Rs) + Rn;
			setReg(dest_reg, value_32);

			if (set_condition)
			{
				setFlagsLogical(value_32, 2);
			}
			break;
		case 0x4: //UMULL
			value_64 = (value_64 * Rm * Rs);

			//Set Rn to low 32-bits, Rd to high 32-bits
			Rn = (value_64 & 0xFFFFFFFF);
			Rd = (value_64 >> 32);

			setReg(accu_reg, Rn);
			setReg(dest_reg, Rd);

			if (set_condition)
			{
				state.CPSR = ((value_64 == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
				state.CPSR = ((value_64 >> 63) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
			}
			break;
		case 0x5: //UMLAL
			hi_lo = Rd;
			hi_lo <<= 32;
			hi_lo |= Rn;

			value_64 = (value_64 * Rm * Rs) + hi_lo;

			//Set Rn to low 32-bits, Rd to high 32-bits
			Rn = (value_64 & 0xFFFFFFFF);
			Rd = (value_64 >> 32);

			setReg(accu_reg, Rn);
			setReg(dest_reg, Rd);

			if (set_condition)
			{
				state.CPSR = ((value_64 == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
				state.CPSR = ((value_64 >> 63) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
			}
			break;
		case 0x6: //SMULL
			value_s64 = (value_s64 * (int32_t)Rm * (int32_t)Rs);
			value_64 = value_s64;

			//Set Rn to low 32-bits, Rd to high 32-bits
			Rn = (value_s64 & 0xFFFFFFFF);
			Rd = (value_s64 >> 32);

			setReg(accu_reg, Rn);
			setReg(dest_reg, Rd);

			if (set_condition)
			{
				state.CPSR = ((value_s64 == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
				state.CPSR = ((value_s64 >> 63) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
			}
			break;
		case 0x7: //SMLAL
			//This looks weird, but it is a workaround for compilers that support 64-bit unsigned ints, but complain about shifts greater than 32
			hi_lo = Rd;
			hi_lo <<= 32;
			hi_lo |= Rn;

			value_s64 = (value_s64 * (int32_t)Rm * (int32_t)Rs) + hi_lo;
			value_64 = value_s64;

			//Set Rn to low 32-bits, Rd to high 32-bits
			Rn = (value_s64 & 0xFFFFFFFF);
			Rd = (value_s64 >> 32);

			setReg(accu_reg, Rn);
			setReg(dest_reg, Rd);

			if (set_condition)
			{
				state.CPSR = ((value_s64 == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
				state.CPSR = ((value_s64 >> 63) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
			}
			break;
		default: logging::error("Multiply: Invalid or unimplemented opcode: " + helpers::intToHex(op_code), "arm7tdmi"); break;
	}
}

void arm7tdmi::ARM_SingleDataTransfer(uint32_t currentInstruction)
{
	bool offsetImmediate = currentInstruction & 0x2000000;
	bool preIndexing = currentInstruction & 0x1000000;
	bool offsetUp = currentInstruction & 0x800000;
	bool byteOrWord = currentInstruction & 0x400000;
	bool writeback = currentInstruction & 0x200000;
	bool loadOrStore = currentInstruction & 0x100000;
	uint8_t baseAddrReg = (currentInstruction >> 16) & 0xF;
	uint8_t srcReg = (currentInstruction >> 12) & 0xF;
	uint32_t offset;

	if (offsetImmediate == false)
	{
		offset = currentInstruction & 0xFFF;
	}
	else
	{
		offset = getReg(currentInstruction & 0xF);
		uint8_t shiftType = ((currentInstruction >> 5) & 0x3);
		uint8_t shiftOffset = ((currentInstruction >> 7) & 0x1F);

		switch (shiftType)
		{
			case 0b00: logicalShiftLeft(&offset, shiftOffset); break;
			case 0b01: logicalShiftRight(&offset, shiftOffset); break;
			case 0b10: arithmeticShiftRight(&offset, shiftOffset); break;
			case 0b11: rotateRight(&offset, shiftOffset); break;
		}
	}

	if (!offsetUp) { offset = (~offset) + 1; }

	uint32_t addr = getReg(baseAddrReg);
	if (preIndexing) { addr += offset; }

	if (loadOrStore)
	{
		// Load
		if (byteOrWord)
		{
			setReg(srcReg, Memory->get8(addr));
		}
		else
		{
			setReg(srcReg, Memory->get32(addr));
		}
	}
	else
	{
		// Store
		uint32_t value = getReg(srcReg);
		if (srcReg == 15) { value += 4; }
		if (byteOrWord)
		{
			Memory->set8(addr, value & 0xFF);
		}
		else
		{
			Memory->set32(addr, value);
		}
	}

	if (!preIndexing) { addr += offset; }

	if (((!preIndexing) || (preIndexing && writeback)) && baseAddrReg != srcReg)
	{
		setReg(baseAddrReg, addr);
	}
}

void arm7tdmi::ARM_HalfwordDataTransfer(uint32_t currentInstruction)
{
	uint8_t pre_post = (currentInstruction & 0x1000000) ? 1 : 0;
	uint8_t up_down = (currentInstruction & 0x800000) ? 1 : 0;
	uint8_t offset_is_register = (currentInstruction & 0x400000) ? 1 : 0;
	uint8_t write_back = (currentInstruction & 0x200000) ? 1 : 0;
	uint8_t load_store = (currentInstruction & 0x100000) ? 1 : 0;
	uint8_t base_reg = ((currentInstruction >> 16) & 0xF);
	uint8_t dest_reg = ((currentInstruction >> 12) & 0xF);
	uint8_t op = ((currentInstruction >> 5) & 0x3);

	//Write-Back is always enabled for Post-Indexing
	if (pre_post == 0) { write_back = 1; }

	uint32_t base_offset = 0;
	uint32_t base_addr = getReg(base_reg);
	uint32_t value = 0;

	if (offset_is_register == 0)
	{
		//Register is Bits 0-3
		base_offset = getReg((currentInstruction & 0xF));

		if ((currentInstruction & 0xF) == 15) { logging::warning("ARM_HalfwordDataTransfer Offset Register is PC", "arm7tdmi"); }
	}
	else
	{
		base_offset = (currentInstruction >> 8) & 0xF;
		base_offset <<= 4;
		base_offset |= (currentInstruction & 0xF);
	}

	//Increment or decrement before transfer if pre-indexing
	if (pre_post == 1)
	{
		if (up_down == 1) { base_addr += base_offset; }
		else { base_addr -= base_offset; }
	}

	//Perform Load or Store ops
	switch (op)
	{
		case 0x1: //Load-Store unsigned halfword
			if (load_store == 0)
			{
				//Store halfword
				value = getReg(dest_reg);

				//If PC is the Destination Register, add 4
				if (dest_reg == 15) { value += 4; }

				value &= 0xFFFF;
				Memory->set16(base_addr, value);
			}
			else
			{
				//Load halfword
				value = Memory->get16(base_addr);
				setReg(dest_reg, value);
			}

			break;
		case 0x2: //Load signed byte (sign extended)
			value = Memory->get8(base_addr);

			if (value & 0x80) { value |= 0xFFFFFF00; }
			setReg(dest_reg, value);

			break;
		case 0x3: //Load signed halfword (sign extended)
			value = Memory->get16(base_addr);

			if (value & 0x8000) { value |= 0xFFFF0000; }
			setReg(dest_reg, value);

			break;
		default: //SWP
			logging::error("Encountered SWP in ARM_HalfwordDataTransfer. Check instruction decoding.", "arm7tdmi");
			return;
	}

	//Increment or decrement after transfer if post-indexing
	if (pre_post == 0)
	{
		if (up_down == 1) { base_addr += base_offset; }
		else { base_addr -= base_offset; }
	}

	//Write-back into base register
	if ((write_back == 1) && (base_reg != dest_reg)) { setReg(base_reg, base_addr); }
}

void arm7tdmi::ARM_BlockDataTransfer(uint32_t currentInstruction)
{
	uint8_t pre_post = (currentInstruction & 0x1000000) ? 1 : 0;
	uint8_t up_down = (currentInstruction & 0x800000) ? 1 : 0;
	uint8_t psr = (currentInstruction & 0x400000) ? 1 : 0;
	uint8_t write_back = (currentInstruction & 0x200000) ? 1 : 0;
	uint8_t load_store = (currentInstruction & 0x100000) ? 1 : 0;
	uint8_t base_reg = ((currentInstruction >> 16) & 0xF);
	uint16_t r_list = (currentInstruction & 0xFFFF);

	if (base_reg == 15) { logging::warning("ARM_BlockDataTransfer: R15 used as Base Register", "arm7tdmi"); }

	//Force USR mode if PSR bit is set
	//cpu_modes temp_mode = current_cpu_mode;
	//if (psr) { current_cpu_mode = USR; }

	uint32_t base_addr = getReg(base_reg);
	uint32_t old_base = base_addr;
	uint8_t transfer_reg = 0xFF;

	//Find out the first register in the Register List
	for (int x = 0; x < 16; x++)
	{
		if (r_list & (1 << x))
		{
			transfer_reg = x;
			x = 0xFF;
			break;
		}
	}

	//Load-Store with an ascending stack order, Up-Down = 1
	if ((up_down == 1) && (r_list != 0))
	{
		for (int x = 0; x < 16; x++)
		{
			if (r_list & (1 << x))
			{
				//Increment before transfer if pre-indexing
				if (pre_post == 1) { base_addr += 4; }

				if (load_store == 0)
				{
					//Store registers
					if ((x == transfer_reg) && (base_reg == transfer_reg)) { Memory->set32(base_addr, old_base); }
					else { Memory->set32(base_addr, getReg(x)); }
				}
				else
				{
					//Load registers
					if ((x == transfer_reg) && (base_reg == transfer_reg)) { write_back = 0; }
					setReg(x, Memory->get32(base_addr));
				}

				//Increment after transfer if post-indexing
				if (pre_post == 0) { base_addr += 4; }
			}

			//Write back the into base register
			if (write_back == 1) { setReg(base_reg, base_addr); }
		}
	}

	//Load-Store with a descending stack order, Up-Down = 0
	else if ((up_down == 0) && (r_list != 0))
	{
		for (int x = 15; x >= 0; x--)
		{
			if (r_list & (1 << x))
			{
				//Decrement before transfer if pre-indexing
				if (pre_post == 1) { base_addr -= 4; }

				//Store registers
				if (load_store == 0)
				{
					if ((x == transfer_reg) && (base_reg == transfer_reg)) { Memory->set32(base_addr, old_base); }
					else { Memory->set32(base_addr, getReg(x)); }
				}

				//Load registers
				else
				{
					if ((x == transfer_reg) && (base_reg == transfer_reg)) { write_back = 0; }
					setReg(x, Memory->get32(base_addr));
				}

				//Decrement after transfer if post-indexing
				if (pre_post == 0) { base_addr -= 4; }
			}

			//Write back the into base register
			if (write_back == 1) { setReg(base_reg, base_addr); }
		}
	}
	else //Special case, empty RList
	{
		//Load R15
		if (load_store == 0) { Memory->set32(base_addr, getReg(15)); }
		else //Store R15
		{
			setReg(15, Memory->get32(base_addr));
		}

		//Add 0x40 to base address if ascending stack, writeback into base register
		if (up_down == 1) { setReg(base_reg, (base_addr + 0x40)); }

		//Subtract 0x40 from base address if descending stack, writeback into base register
		else { setReg(base_reg, (base_addr - 0x40)); }

		logging::warning("ARM_BlockDataTransfer: Instruction uses empty register list", "arm7tdmi");
	}

	//if (psr) { current_cpu_mode = temp_mode; }
}

void arm7tdmi::ARM_SingleDataSwap(uint32_t currentInstruction)
{
	uint8_t src_reg = (currentInstruction & 0xF);
	uint8_t dest_reg = ((currentInstruction >> 12) & 0xF);
	uint8_t base_reg = ((currentInstruction >> 16) & 0xF);
	uint8_t byte_word = (currentInstruction & 0x400000) ? 1 : 0;

	uint32_t base_addr = getReg(base_reg);
	uint32_t dest_value = 0;
	uint32_t swap_value = 0;

	if (byte_word == 1) //Swap a single byte
	{
		//Grab values before swapping
		dest_value = Memory->get8(base_addr);
		swap_value = (getReg(src_reg) & 0xFF);

		//Swap the values
		Memory->set8(base_addr, swap_value);
		setReg(dest_reg, dest_value);
	}
	else //Swap a single word
	{
		//Grab values before swapping
		dest_value = Memory->get32(base_addr);
		swap_value = getReg(src_reg);

		//Swap the values
		Memory->set32(base_addr, swap_value);
		setReg(dest_reg, dest_value);
	}
}

// Thumb instructions

void arm7tdmi::THUMB_MoveShiftedRegister(uint16_t currentInstruction)
{
	uint8_t dest_reg = (currentInstruction & 0x7);
	uint8_t src_reg = ((currentInstruction >> 3) & 0x7);
	uint8_t offset = ((currentInstruction >> 6) & 0x1F);
	uint8_t op = ((currentInstruction >> 11) & 0x3);

	uint32_t result = getReg(src_reg);
	uint8_t shiftOut = 0;

	switch (op)
	{
		case 0x0: //LSL
			shiftOut = logicalShiftLeft(&result, offset);
			break;
		case 0x1: //LSR
			shiftOut = logicalShiftRight(&result, offset);
			break;
		case 0x2: //ASR
			shiftOut = arithmeticShiftRight(&result, offset);
			break;
		default: logging::error("Invalid shift in THUMB_MoveShiftedRegister", "arm7tdmi"); break;
	}

	setReg(dest_reg, result);
	setFlagsLogical(result, shiftOut);
}

void arm7tdmi::THUMB_AddSubtract(uint16_t currentInstruction)
{
	uint8_t dest_reg = (currentInstruction & 0x7);
	uint8_t src_reg = ((currentInstruction >> 3) & 0x7);
	uint8_t op = ((currentInstruction >> 9) & 0x3);

	uint32_t input = getReg(src_reg);
	uint32_t result = 0;
	uint32_t operand = 0;
	uint8_t imm_reg = ((currentInstruction >> 6) & 0x7);

	//Perform addition or subtraction
	switch (op)
	{
		case 0x0: //Add with register as operand
			operand = getReg(imm_reg);
			result = input + operand;
			break;
		case 0x1: //Subtract with register as operand
			operand = getReg(imm_reg);
			result = input - operand;
			break;
		case 0x2: //Add with 3-bit immediate as operand
			operand = imm_reg;
			result = input + operand;
			break;
		case 0x3: //Subtract with 3-bit immediate as operand
			operand = imm_reg;
			result = input - operand;
			break;
	}

	setReg(dest_reg, result);
	setFlagsArithmetic(input, operand, result, !((bool)(op & 0x1)));
}

void arm7tdmi::THUMB_MvCmpAddSubImmediate(uint16_t currentInstruction)
{
	uint8_t dest_reg = ((currentInstruction >> 8) & 0x7);
	uint8_t op = ((currentInstruction >> 11) & 0x3);

	uint32_t input = getReg(dest_reg);
	uint32_t result = 0;

	uint32_t operand = (currentInstruction & 0xFF);

	switch (op)
	{
		case 0x0: //MOV
			result = operand;
			setFlagsLogical(result, 2);
			break;
		case 0x1: //CMP
		case 0x3: //SUB
			result = (input - operand);
			setFlagsArithmetic(input, operand, result, false);
			break;
		case 0x2: //ADD
			result = (input + operand);
			setFlagsArithmetic(input, operand, result, true);
			break;
	}

	//Do not update the destination register if CMP is the operation!
	if (op != 1) { setReg(dest_reg, result); }
}

void arm7tdmi::THUMB_ALUOps(uint16_t currentInstruction)
{
	uint8_t dest_reg = (currentInstruction & 0x7);
	uint8_t src_reg = ((currentInstruction >> 3) & 0x7);
	uint8_t op = ((currentInstruction >> 6) & 0xF);

	uint32_t input = getReg(dest_reg);
	uint32_t result = 0;
	uint32_t operand = getReg(src_reg);
	uint8_t shift_out = 0;
	uint8_t carry_out = (state.CPSR & Cflag) ? 1 : 0;

	//Perform ALU operations
	switch (op)
	{
		case 0x0: //AND
			result = (input & operand);
			setFlagsLogical(result, 2);
			setReg(dest_reg, result);
			break;
		case 0x1: //XOR
			result = (input ^ operand);
			setFlagsLogical(result, 2);
			setReg(dest_reg, result);
			break;
		case 0x2: //LSL
			operand &= 0xFF;
			if (operand != 0) { shift_out = logicalShiftLeft(&input, operand); }
			result = input;
			
			//setFlagsLogical(result, shift_out);
			state.CPSR = ((result == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
			state.CPSR = ((result >> 31) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
			if (operand != 0 && shift_out < 2)
			{
				state.CPSR = (shift_out ? state.CPSR | Cflag : state.CPSR & ~Cflag);
			}

			setReg(dest_reg, result);
			break;
		case 0x3: //LSR
			operand &= 0xFF;
			if (operand != 0) { shift_out = logicalShiftRight(&input, operand); }
			result = input;
	
			//setFlagsLogical(result, shift_out);
			state.CPSR = ((result == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
			state.CPSR = ((result >> 31) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
			if (operand != 0)
			{
				state.CPSR = (shift_out ? state.CPSR | Cflag : state.CPSR & ~Cflag);
			}

			setReg(dest_reg, result);
			break;
		case 0x4: //ASR
			operand &= 0xFF;
			if (operand != 0) { shift_out = arithmeticShiftRight(&input, operand); }
			result = input;
	
			//setFlagsLogical(result, shift_out);
			state.CPSR = ((result == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
			state.CPSR = ((result >> 31) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
			if (operand != 0 && shift_out < 2)
			{
				state.CPSR = (shift_out ? state.CPSR | Cflag : state.CPSR & ~Cflag);
			}
			setReg(dest_reg, result);
			break;
		case 0x5: //ADC
			result = (input + operand + carry_out);
			// GBE-Plus doesn't include carry here, but it should be included, right?
			setFlagsArithmetic(input, operand, result, true);
			//setFlagsArithmetic(input, operand + carry_out, result, true);
			setReg(dest_reg, result);
			break;
		case 0x6: //SBC
			carry_out ^= 0x1; //Invert carry
	
			result = (input - operand - carry_out);
			//setFlagsArithmetic(input, operand + carry_out - 1, result, false);
			setFlagsArithmetic(input, operand, result, false);
			setReg(dest_reg, result);
			break;
		case 0x7: //ROR
			operand &= 0xFF;
			if (operand != 0) { shift_out = rotateRight(&input, operand); }
			result = input;
	
			//setFlagsLogical(result, shift_out);
			state.CPSR = ((result == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
			state.CPSR = ((result >> 31) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
			if (operand != 0)
			{
				state.CPSR = (shift_out ? state.CPSR | Cflag : state.CPSR & ~Cflag);
			}

			setReg(dest_reg, result);
			break;
		case 0x8: //TST
			result = (input & operand);
			setFlagsLogical(result, 2);
			break;
		case 0x9: //NEG
			input = 0;
			result = (input - operand);
			setFlagsArithmetic(input, operand, result, false);
			setReg(dest_reg, result);
			break;
		case 0xA: //CMP
			result = (input - operand);
			setFlagsArithmetic(input, operand, result, false);
			break;
		case 0xB: //CMN
			result = (input + operand);
			setFlagsArithmetic(input, operand, result, true);
			break;
		case 0xC: //ORR
			result = (input | operand);
	
			setFlagsLogical(result, 2);
			setReg(dest_reg, result);
			break;
		case 0xD: //MUL
			result = (input * operand);
	
			setFlagsLogical(result, 2);
			//TODO - Figure out what the carry flag should be for this opcode.
			setReg(dest_reg, result);
			break;
		case 0xE: //BIC
			result = (input & ~operand);
			setFlagsLogical(result, 2);
			setReg(dest_reg, result);
			break;
		case 0xF: //MVN
			result = ~operand;
			setFlagsLogical(result, 2);
			setReg(dest_reg, result);
			break;
	}
}

void arm7tdmi::THUMB_HiRegOps_BranchExchange(uint16_t currentInstruction)
{
	uint8_t dest_reg = (currentInstruction & 0x7);
	uint8_t src_reg = ((currentInstruction >> 3) & 0x7);
	uint8_t sr_msb = (currentInstruction & 0x40) >> 6;
	uint8_t dr_msb = (currentInstruction & 0x80) >> 7;

	src_reg |= sr_msb << 3;
	dest_reg |= dr_msb << 3;
	//if (sr_msb) { src_reg |= 0x8; }
	//if (dr_msb) { dest_reg |= 0x8; }

	uint8_t op = ((currentInstruction >> 8) & 0x3);

	uint32_t input = getReg(dest_reg);
	uint32_t result = 0;
	uint32_t operand = getReg(src_reg);

	if ((op == 3) && (dr_msb != 0))
	{
		logging::fatal ("Using BX but MSBd is set in THUMB_HiRegOps_BranchExchange", "arm7tdmi");
	}

	switch (op)
	{
		case 0x0: //ADD
			//When the destination register is the PC, auto-align operand to half-word
			if (dest_reg == 15) { operand &= ~0x1; }
			result = input + operand;
			setReg(dest_reg, result);
			break;
		case 0x1: //CMP
			result = (input - operand);
			setFlagsArithmetic(input, operand, result, false);
			break;
		case 0x2: //MOV
			//When the destination register is the PC, auto-align operand to half-word
			if (dest_reg == 15) { operand &= ~0x1; }
			result = operand;
			setReg(dest_reg, result);
			break;
		case 0x3: //BX
			//Switch to ARM mode if necessary
			if ((operand & 0x1) == 0)
			{
				state.CPSR &= ~0x20;
				operand &= ~0x3;
			}
			else
			{
				//Align operand to half-word
				operand &= ~0x1;
			}
	
			//Auto-align PC when using R15 as an operand
			if (src_reg == 15)
			{
				setReg(15, getReg(15) & ~0x2);
			}
			else
			{
				setReg(15, operand); 
			}
			break;
		}
}

void arm7tdmi::THUMB_LoadPCRelative(uint16_t currentInstruction)
{
	uint16_t offset = (currentInstruction & 0xFF) * 4;
	uint8_t dest_reg = ((currentInstruction >> 8) & 0x7);

	uint32_t load_addr = (getReg(15) & ~0x2) + offset;

	uint32_t value = Memory->get32(load_addr);
	setReg(dest_reg, value);
}

void arm7tdmi::THUMB_LoadStoreRegOffset(uint16_t currentInstruction)
{
	uint8_t src_dest_reg = (currentInstruction & 0x7);
	uint8_t base_reg = ((currentInstruction >> 3) & 0x7);
	uint8_t offset_reg = ((currentInstruction >> 6) & 0x7);
	uint8_t op = ((currentInstruction >> 10) & 0x3);

	uint32_t value = 0;
	uint32_t op_addr = getReg(base_reg) + getReg(offset_reg);

	switch (op)
	{
		case 0x0: //STR
			value = getReg(src_dest_reg);
			Memory->set32(op_addr, value);
			break;
		case 0x1: //STRB
			value = getReg(src_dest_reg);
			value &= 0xFF;
			Memory->set8(op_addr, value);
			break;
		case 0x2: //LDR
			value = Memory->get32(op_addr);
			setReg(src_dest_reg, value);
			break;
		case 0x3: //LDRB
			value = Memory->get8(op_addr);
			setReg(src_dest_reg, value);
			break;
	}
}

void arm7tdmi::THUMB_LoadStoreSignExtend(uint16_t currentInstruction)
{
	uint8_t src_dest_reg = (currentInstruction & 0x7);
	uint8_t base_reg = ((currentInstruction >> 3) & 0x7);
	uint8_t offset_reg = ((currentInstruction >> 6) & 0x7);
	uint8_t op = ((currentInstruction >> 10) & 0x3);

	uint32_t value = 0;
	uint32_t op_addr = getReg(base_reg) + getReg(offset_reg);

	switch (op)
	{
		case 0x0: //STRH
			value = getReg(src_dest_reg);
			value &= 0xFFFF;
			Memory->set16(op_addr, value);
			break;
		case 0x1: //LDSB
			value = Memory->get8(op_addr);
			if (value & 0x80) { value |= 0xFFFFFF00; }
			setReg(src_dest_reg, value);
			break;
		case 0x2: //LDRH
			value = Memory->get16(op_addr);
			setReg(src_dest_reg, value);
			break;
		case 0x3: //LDSH
			value = Memory->get16(op_addr);
			if (value & 0x8000) { value |= 0xFFFF0000; }
			setReg(src_dest_reg, value);
			break;
	}
}

void arm7tdmi::THUMB_LoadStoreImmediate(uint16_t currentInstruction)
{
	uint8_t src_dest_reg = (currentInstruction & 0x7);
	uint8_t base_reg = ((currentInstruction >> 3) & 0x7);
	uint16_t offset = ((currentInstruction >> 6) & 0x1F);
	uint8_t op = ((currentInstruction >> 11) & 0x3);

	uint32_t value = 0;
	uint32_t op_addr = getReg(base_reg);

	switch (op)
	{
		case 0x0: //STR
			value = getReg(src_dest_reg);
			offset <<= 2;
			op_addr += offset;

			Memory->set32(op_addr, value);
			break;
		case 0x1: //LDR
			offset <<= 2;
			op_addr += offset;

			value = Memory->get32(op_addr);
			setReg(src_dest_reg, value);
			break;
		case 0x2: //STRB
			value = getReg(src_dest_reg);
			op_addr += offset;

			Memory->set8(op_addr, value & 0xFF);
			break;
		case 0x3://LDRB
			op_addr += offset;
			value = Memory->get8(op_addr);

			setReg(src_dest_reg, value);
			break;
	}
}

void arm7tdmi::THUMB_LoadStoreHalfword(uint16_t currentInstruction)
{
	uint8_t src_dest_reg = (currentInstruction & 0x7);
	uint8_t base_reg = ((currentInstruction >> 3) & 0x7);
	uint16_t offset = ((currentInstruction >> 6) & 0x1F);
	bool op = (currentInstruction & 0x800);

	uint32_t value = 0;
	uint32_t op_addr = getReg(base_reg);

	offset <<= 1;
	op_addr += offset;

	if (op) //LDRH
	{
		value = Memory->get16(op_addr);
		setReg(src_dest_reg, value);
	}
	else //STRH
	{
		value = getReg(src_dest_reg);
		Memory->set16(op_addr, value);
	}
}

void arm7tdmi::THUMB_LoadStoreSPRelative(uint16_t currentInstruction)
{
	uint16_t offset = (currentInstruction & 0xFF);
	uint8_t src_dest_reg = ((currentInstruction >> 8) & 0x7);
	uint8_t op = (currentInstruction & 0x800) ? 1 : 0;

	uint32_t value = 0;
	uint32_t op_addr = getReg(13);

	offset <<= 2;
	op_addr += offset;

	switch (op)
	{
		case 0x0: //STR
			value = getReg(src_dest_reg);
			Memory->set32(op_addr, value);
			break;
		case 0x1: //LDR
			value = Memory->get32(op_addr);
			setReg(src_dest_reg, value);
			break;
	}
}

void arm7tdmi::THUMB_LoadAddress(uint16_t currentInstruction)
{
	uint16_t offset = (currentInstruction & 0xFF);
	uint8_t dest_reg = ((currentInstruction >> 8) & 0x7);
	bool op = (currentInstruction & 0x800);

	uint32_t value = 0;
	offset <<= 2;

	if (op) //Rd = SP + nn
	{
		value = getReg(13) + offset;
		setReg(dest_reg, value);
	}
	else //Rd = PC + nn
	{
		value = (getReg(15) & ~0x2) + offset;
		setReg(dest_reg, value);
	}
}

void arm7tdmi::THUMB_AddOffsetSP(uint16_t currentInstruction)
{
	uint16_t offset = (currentInstruction & 0x7F);
	bool op = (currentInstruction & 0x80);
	offset <<= 2;

	uint32_t r13 = getReg(13);

	if (op) //SP = SP - nn
	{
		r13 -= offset;
	}
	else //SP = SP + nn
	{
		r13 += offset;
	}
	setReg(13, r13);
}

void arm7tdmi::THUMB_PushPop(uint16_t currentInstruction)
{
	uint32_t r13 = getReg(13);
	uint32_t lr = getReg(14);
	uint8_t r_list = (currentInstruction & 0xFF);
	bool pc_lr_bit = (currentInstruction & 0x100);
	uint8_t op = (currentInstruction & 0x800) ? 1 : 0;

	uint8_t n_count = 0;

	for (int x = 0; x < 8; x++)
	{
		if ((r_list >> x) & 0x1) { n_count++; }
	}

	switch (op)
	{
		case 0x0: //PUSH
			if (pc_lr_bit)
			{
				r13 -= 4;
				Memory->set32(r13, lr);
				setReg(14, lr);
			}

			//Cycle through the register list
			for (int x = 7; x >= 0; x--)
			{
				if (r_list & (1 << x))
				{
					r13 -= 4;
					uint32_t push_value = getReg(x);
					Memory->set32(r13, push_value);

					if ((n_count - 1) != 0) { n_count--; }
					else { x = 10; break; }
				}
			}

			break;
		case 0x1: //POP
			//Cycle through the register list
			for (int x = 0; x < 8; x++)
			{
				if (r_list & 0x1)
				{
					uint32_t pop_value = Memory->get32(r13);
					setReg(x, pop_value);
					r13 += 4;
				}

				r_list >>= 1;
			}

			if (pc_lr_bit)
			{
				setReg(15, Memory->get32(r13) & ~0x1);
				r13 += 4;
			}

			break;
	}
	setReg(13, r13);
}

void arm7tdmi::THUMB_MultipleLoadStore(uint16_t currentInstruction)
{
	uint8_t r_list = (currentInstruction & 0xFF);
	uint8_t base_reg = ((currentInstruction >> 8) & 0x7);
	uint8_t op = (currentInstruction & 0x800) ? 1 : 0;

	uint32_t base_addr = getReg(base_reg);
	uint32_t reg_value = 0;
	uint8_t n_count = 0;

	uint32_t old_base = base_addr;
	uint8_t transfer_reg = 0xFF;
	bool write_back = true;

	//Find out the first register in the Register List
	for (int x = 0; x < 8; x++)
	{
		if (r_list & (1 << x))
		{
			transfer_reg = x;
			x = 0xFF;
			break;
		}
	}

	//Grab n_count
	for (int x = 0; x < 8; x++)
	{
		if ((r_list >> x) & 0x1) { n_count++; }
	}

	//Perform multi load-store ops
	switch (op)
	{
		case 0x0: //STMIA
			//If register list is not empty, store normally
			if (r_list != 0)
			{
				//Cycle through the register list
				for (int x = 0; x < 8; x++)
				{
					if (r_list & 0x1)
					{
						reg_value = getReg(x);
	
						if ((x == transfer_reg) && (base_reg == transfer_reg)) { Memory->set32(base_addr, old_base); }
						else { Memory->set32(base_addr, reg_value); }
	
						//Update base register
						base_addr += 4;
						setReg(base_reg, base_addr);
	
						if ((n_count - 1) != 0) { n_count--; }
						else { x = 10; break; }
					}
					r_list >>= 1;
				}
			}
			else //Special case with empty list
			{
				//Store PC, then add 0x40 to base register
				Memory->set32(base_addr, getReg(15));
				base_addr += 0x40;
				setReg(base_reg, base_addr);
	
				//Clock CPU and controllers - ???
				//TODO - find out what to do here...
			}
			break;
		case 0x1: //LDMIA
			//If register list is not empty, load normally
			if (r_list != 0)
			{
				//Cycle through the register list
				for (int x = 0; x < 8; x++)
				{
					if (r_list & 0x1)
					{
						if ((x == transfer_reg) && (base_reg == transfer_reg)) { write_back = false; }
						reg_value = Memory->get32(base_addr);
						setReg(x, reg_value);
	
						//Update base register
						base_addr += 4;
						if (write_back) { setReg(base_reg, base_addr); }
					}
	
					r_list >>= 1;
				}
			}
			else //Special case with empty list
			{
				//Load PC, then add 0x40 to base register
				setReg(15, Memory->get32(base_addr));
				base_addr += 0x40;
				setReg(base_reg, base_addr);
	
				//Clock CPU and controllers - ???
				//TODO - find out what to do here...
			}
			break;
	}
}

void arm7tdmi::THUMB_ConditionalBranch(uint16_t currentInstruction)
{
	uint8_t offset = (currentInstruction & 0xFF);
	uint8_t op = ((currentInstruction >> 8) & 0xF);

	int16_t jump_addr = 0;

	jump_addr = helpers::signExtend((uint16_t)offset, 8) << 1;
	/*if (offset & 0x80)
	{
		offset--;
		offset = ~offset;

		jump_addr = (offset * -2);
	}
	else { jump_addr = ((uint16_t)offset << 1); }*/
	bool doBranch = false;

	//Jump based on condition codes
	switch (op)
	{
		case 0x0: //BEQ
			doBranch = state.CPSR & Zflag;
			break;
		case 0x1: //BNE
			doBranch = (state.CPSR & Zflag) == 0;
			break;
		case 0x2: //BCS
			doBranch = state.CPSR & Cflag;
			break;
		case 0x3: //BCC
			doBranch = (state.CPSR & Cflag) == 0;
			break;
		case 0x4: //BMI
			doBranch = state.CPSR & Nflag;
			break;
		case 0x5: //BPL
			doBranch = (state.CPSR & Nflag) == 0;
			break;
		case 0x6: //BVS
			doBranch = state.CPSR & Vflag;
			break;
		case 0x7: //BVC
			doBranch = (state.CPSR & Vflag) == 0;
			break;
		case 0x8: //BHI
			doBranch = (state.CPSR & Cflag) && ((state.CPSR & Zflag) == 0);
			break;
		case 0x9: //BLS
			doBranch = (state.CPSR & Zflag) || ((state.CPSR & Cflag) == 0);
			break;
		case 0xA: //BGE
			doBranch = ((bool)(state.CPSR & Nflag) == (bool)(state.CPSR & Vflag));
			break;
		case 0xB: //BLT
			doBranch = ((bool)(state.CPSR & Nflag) != (bool)(state.CPSR & Vflag));
			break;
		case 0xC: //BGT
			doBranch = (((state.CPSR & Zflag) == 0) && ((bool)(state.CPSR & Nflag) == (bool)(state.CPSR & Vflag)));
			break;
		case 0xD: //BLE
			doBranch = (((state.CPSR & Zflag) != 0) && ((bool)(state.CPSR & Nflag) != (bool)(state.CPSR & Vflag)));
			break;
		case 0xE: //Undefined
			logging::error("Undefined condition 0xE in THUMB_ConditionalBranch", "arm7tdmi");
			break;
		case 0xF: //SWI
			logging::error("SWI in THUMB_ConditionalBranch. Shouldn't be possible. Check instruction decoding.", "arm7tdmi");
			break;
	}

	if (doBranch)
	{
		setReg(15, getReg(15) + jump_addr);
	}
}

void arm7tdmi::THUMB_UnconditionalBranch(uint16_t currentInstruction)
{
	uint16_t offset = (currentInstruction & 0x7FF);
	int16_t jump_addr = 0;
	jump_addr = helpers::signExtend((uint16_t)offset, 11) << 1;
	setReg(15, getReg(15) + jump_addr);
}

void arm7tdmi::THUMB_LongBranchLink(uint16_t currentInstruction)
{
	//Determine if this is the first or second instruction executed
	bool first_op = !(((currentInstruction >> 11) & 0x1F) == 0x1F);

	uint32_t lbl_addr = 0;

	//Perform 1st 16-bit operation
	if (first_op)
	{
		uint32_t r15 = getReg(15);
		uint8_t pre_bit = (r15 & 0x800000) ? 1 : 0;

		//Grab upper 11-bits of destination address
		lbl_addr = (((uint32_t)currentInstruction & 0x7FF) << 12);

		//Add as a 2's complement to PC
		if (lbl_addr & 0x400000) { lbl_addr |= 0xFF800000; }
		lbl_addr += r15;

		//Save label to LR
		setReg(14, lbl_addr);
	}
	else //Perform 2nd 16-bit operation
	{
		//Grab address of the "next" instruction to place in LR, set Bit 0 to 1
		uint32_t next_instr_addr = (getReg(15) - 2);
		next_instr_addr |= 1;

		//Grab lower 11-bits of destination address
		lbl_addr = getReg(14);
		lbl_addr += (((uint32_t)currentInstruction & 0x7FF) << 1);

		setReg(15, lbl_addr & ~0x1);
		setReg(14, next_instr_addr);
	}
}

// Helper functions

void arm7tdmi::setFlagsLogical(uint32_t result, int carryOut)
{
	state.CPSR = ((result == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
	state.CPSR = ((result >> 31) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
	if (carryOut < 2)
	{
		state.CPSR = (carryOut ? state.CPSR | Cflag : state.CPSR & ~Cflag);
	}
}

void arm7tdmi::setFlagsArithmetic(uint32_t op1, uint32_t op2, uint32_t result, bool addition)
{
	state.CPSR = ((result == 0) ? state.CPSR | Zflag : state.CPSR & ~Zflag);
	state.CPSR = ((result >> 31) ? state.CPSR | Nflag : state.CPSR & ~Nflag);
	//http://teaching.idallen.com/dat2343/10f/notes/040_overflow.txt
	//Carry flag
	if (addition)
	{
		state.CPSR = (((uint64_t)op1 + (uint64_t)op2 > 0xFFFFFFFF) ? state.CPSR | Cflag : state.CPSR & ~Cflag);
	}
	else
	{
		//state.CPSR = ((result > op1) ? state.CPSR | Cflag : state.CPSR & ~Cflag);
		state.CPSR = (op2 <= op1) ? state.CPSR | Cflag : state.CPSR & ~Cflag;
	}
	//Overflow flag
	bool input_msb = (op1 & 0x80000000);
	bool operand_msb = (op2 & 0x80000000);
	bool result_msb = (result & 0x80000000);

	if (!addition) { operand_msb = !operand_msb; }

	if (input_msb != operand_msb)
	{
		state.CPSR &= ~Vflag;
	}
	else
	{
		if (result_msb == input_msb) { state.CPSR &= ~Vflag; }
		else { state.CPSR |= Vflag; }
	}
}

int arm7tdmi::logicalShiftLeft(uint32_t* value, int shiftAmount)
{
	if (shiftAmount > 0)
	{
		bool carryOut = (1 << (32 - shiftAmount)) & *value;
		if (shiftAmount >= 32)
		{
			*value = 0;
		}
		else
		{
			*value <<= shiftAmount;
		}
		return carryOut;
	}
	else
	{
		//Carry isn't affected
		return 2;
	}
}

bool arm7tdmi::logicalShiftRight(uint32_t* value, int shiftAmount)
{
	if (shiftAmount > 0)
	{
		bool carryOut = (1 << (shiftAmount - 1)) & *value;

		if (shiftAmount >= 32)
		{
			*value = 0;
		}
		else
		{
			*value >>= shiftAmount;
		}
		return carryOut;
	}
	else
	{
		bool carryOut = *value & 0x80000000;
		*value = 0;
		return carryOut;
	}
}

bool arm7tdmi::arithmeticShiftRight(uint32_t* value, int shiftAmount)
{
	if (shiftAmount > 0)
	{
		bool carryOut = 0;//= (1 << (shiftAmount - 1)) & *value;
		uint32_t signBit = *value & 0x80000000;
		for (int i = 0; i < shiftAmount; i++)
		{
			carryOut = *value & 0x1;
			*value >>= 1;
			*value |= signBit;
		}
		return carryOut;
	}
	else
	{
		if (*value & 0x80000000)
		{
			*value = 0xFFFFFFFF;
			return true;
		}
		else
		{
			*value = 0;
			return false;
		}
	}
}

bool arm7tdmi::rotateRight(uint32_t* value, int shiftAmount)
{
	if (shiftAmount > 0)
	{
		//bool carryOut = (1 << (shiftAmount - 1)) & *value;
		bool carryOut = 0;
		for (int i = 0; i < shiftAmount; i++)
		{
			carryOut = (*value) & 1;
			*value >>= 1;
			*value |= (uint32_t)carryOut << 31;
		}
		return carryOut;
	}
	else
	{
		bool carryIn = state.CPSR & Cflag;
		bool carryOut = *value & 1;
		*value >>= 1;
		*value |= (uint32_t)carryIn << 31;
		return carryOut;
	}
}

void arm7tdmi::rotateRightSpecial(uint32_t* value, int shiftAmount)
{
	if (shiftAmount > 0)
	{
		for (int x = 0; x < (shiftAmount * 2); x++)
		{
			bool carry_out = *value & 0x1;
			*value >>= 1;

			if (carry_out) { *value |= 0x80000000; }
		}
	}
}