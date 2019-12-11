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
		//logging::info("Execute instruction: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
		switch ((Pipeline.executeInstr >> 26) & 0b11)
		{
			case 0b00:
			{
				if ((Pipeline.executeInstr & 0x12FFF10) == 0x12FFF10)
				{
					ARM_BranchExchange();
				}
				else if ((Pipeline.executeInstr & 0x2000000) == 0x2000000 || (Pipeline.executeInstr & 0x80) != 0x80)
				{
					ARM_DataProcessing();
				}
				else if ((Pipeline.executeInstr & 0x60) == 0)
				{
					switch ((Pipeline.executeInstr >> 23) & 0b11)
					{
						case 0b00:
							logging::error("Unimplemented instruction: Multiply: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
							break;
						case 0b01:
							logging::error("Unimplemented instruction: Multiply Long: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
							break;
						case 0b10:
							logging::error("Unimplemented instruction: Single Data Swap: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
							break;
						case 0b11:
							//The datasheet doesn't say what this should be.
							logging::error("Undefined instruction: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
							break;
					}
				}
				else
				{
					if (Pipeline.executeInstr & 0x400000)
					{
						logging::error("Unimplemented instruction: HalfwordDataTransfer(ImmOffset): " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
					}
					else
					{
						logging::error("Unimplemented instruction: HalfwordDataTransfer(RegOffset): " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
					}
				}
				break;
			}
			case 0b01:
			{
				if ((Pipeline.executeInstr & 0x2000010) == 0x2000010)
				{
					logging::error("Undefined instruction: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
				}
				else
				{
					ARM_SingleDataTransfer();
				}
				break;
			}
			case 0b10:
			{
				if ((Pipeline.executeInstr >> 25) & 1)
				{
					ARM_Branch();
				}
				else
				{
					logging::error("Unimplemented instruction: Block Data Transfer: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
				}
				break;
			}
			case 0b11:
			{
				if (((Pipeline.executeInstr >> 24) & 0xF) == 0xF)
				{
					logging::error("Unimplemented instruction: Software Interrupt: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
				}
				else
				{
					logging::error("Undefined coprocessor instruction: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
				}
				break;
			}
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

void arm7tdmi::ARM_BranchExchange()
{
	uint32_t addr = state.R[(Pipeline.executeInstr & 0xF)];
	setReg(15, addr);
	if (addr & 0x1)
	{
		logging::error("Tried to switch to THUMB!", "arm7tdmi");
	}
	else
	{
		logging::info("Continue in ARM", "arm7tdmi");
	}
	logging::info("Branched and exchanged to " + helpers::intToHex(getReg(15)), "arm7tdmi");
}

void arm7tdmi::ARM_DataProcessing()
{
	bool setFlag = Pipeline.executeInstr & 0x100000;
	uint8_t opcode = (Pipeline.executeInstr >> 21) & 0xF;
	uint32_t operand1 = getReg((Pipeline.executeInstr >> 16) & 0xF);
	uint8_t destReg = (Pipeline.executeInstr >> 12) & 0xF;
	uint32_t operand2;
	if (!setFlag && (opcode >> 2) == 0b10)
	{
		ARM_PSRTransfer();
		return;
	}
	logging::info("Data Processing on R" + std::to_string(destReg), "arm7tdmi");

	int shiftCarryOut = 2;
	if (Pipeline.executeInstr & 0x2000000)
	{
		//Immediate Value
		uint32_t value = Pipeline.executeInstr & 0xFF;
		uint8_t shift = ((Pipeline.executeInstr >> 8) & 0xF) * 2;
		//ROR
		for (int i = 0; i < shift; i++)
		{
			uint32_t carry = value & 1;
			value >>= 1;
			value |= carry << 31;
		}
		operand2 = value;
	}
	else
	{
		//Register
		operand2 = getReg(Pipeline.executeInstr & 0xF);
		uint8_t shiftInfo = (Pipeline.executeInstr >> 4) & 0xFF;

		uint8_t shiftAmount;
		if (shiftInfo & 1)
		{
			if ((shiftInfo >> 4) == 15)
			{
				logging::error("DataProcessing: Shift amount can't be PC", "arm7tdmi");
			}
			shiftAmount = getReg(shiftInfo >> 4) & 0xFF;
		}
		else
		{
			shiftAmount = shiftInfo >> 3;
		}

		if (!((shiftInfo & 1) && shiftAmount == 0))
		{
			switch ((shiftInfo >> 1) & 0b11)
			{
				case 0b00: shiftCarryOut = logicalShiftLeft(&operand2, shiftAmount); break;
				case 0b01: shiftCarryOut = logicalShiftRight(&operand2, shiftAmount); break;
				case 0b10: shiftCarryOut = arithmeticShiftRight(&operand2, shiftAmount); break;
				case 0b11: shiftCarryOut = rotateRight(&operand2, shiftAmount); break;
			}
		}
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

	if (setFlag && (destReg == 15))
	{
		state.CPSR = getSPSR();
	}
}

void arm7tdmi::ARM_PSRTransfer()
{
	logging::info("PSR transfer", "arm7tdmi");
	bool PSR = Pipeline.executeInstr & 0x400000; //0 = CPSR  1 = SPSR

	switch ((Pipeline.executeInstr >> 16) & 0x3F)
	{
		case 0b001111: // MRS (transfer PSR to register)
		{
			uint8_t destReg = (Pipeline.executeInstr >> 12) & 0xF;
			if (destReg == 15)
			{
				logging::error("MRS destination register can't be R15", "arm7tdmi");
				break;
			}
			if (PSR)
			{
				setReg(destReg, getSPSR());
			}
			else
			{
				setReg(destReg, state.CPSR);
			}
			break;
		}
		case 0b101001: // MSR (transfer register to PSR)
		{
			uint8_t srcReg = Pipeline.executeInstr & 0xF;
			if (srcReg == 15)
			{
				logging::error("MSR source register can't be R15", "arm7tdmi");
				break;
			}
			if (PSR)
			{
				setSPSR(getReg(srcReg));
			}
			else
			{
				state.CPSR = srcReg; //TODO: handle mode changes here
			}
			break;
		}
		case 0b101000: // MSR (transfer value to PSR flag bits)
		{
			uint32_t src;
			if (Pipeline.executeInstr & 0x2000000)
			{
				// Source is register
				uint8_t srcReg = Pipeline.executeInstr & 0xF;
				if (srcReg == 15)
				{
					logging::error("MSR source register can't be R15", "arm7tdmi");
					break;
				}
				src = getReg(srcReg);
			}
			else
			{
				// Source is immediate
				uint32_t value = Pipeline.executeInstr & 0xFF;
				uint8_t shift = ((Pipeline.executeInstr >> 8) & 0xF) * 2;
				//ROR
				for (int i = 0; i < shift; i++)
				{
					uint32_t carry = value & 1;
					value >>= 1;
					value |= carry << 31;
				}
				src = value;
			}
			if (PSR)
			{
				setSPSR((getSPSR() & 0xFFFFFFF) | (src & 0xF0000000));
			}
			else
			{
				state.CPSR = ((state.CPSR & 0xFFFFFFF) | (src & 0xF0000000));
			}
			break;
		}
		default:
		{
			logging::error("Invalid PSR instruction: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
			break;
		}
	}
}

void arm7tdmi::ARM_SingleDataTransfer()
{
	logging::info("Single Data Transfer", "arm7tdmi");
	bool offsetImmediate = Pipeline.executeInstr & 0x2000000;
	bool preIndexing = Pipeline.executeInstr & 0x1000000;
	bool offsetUp = Pipeline.executeInstr & 0x800000;
	bool byteOrWord = Pipeline.executeInstr & 0x400000;
	bool writeback = Pipeline.executeInstr & 0x200000;
	bool loadOrStore = Pipeline.executeInstr & 0x100000;
	uint8_t baseAddrReg = (Pipeline.executeInstr >> 16) & 0xF;
	uint8_t srcReg = (Pipeline.executeInstr >> 12) & 0xF;
	uint32_t offset;

	if (offsetImmediate)
	{
		offset = Pipeline.executeInstr & 0xFFF;
	}
	else
	{
		offset = getReg(Pipeline.executeInstr & 0xF);
		uint8_t shiftInfo = (Pipeline.executeInstr >> 4) & 0xFF;

		uint8_t shiftAmount = shiftInfo >> 3;

		if (!((shiftInfo & 1) && shiftAmount == 0))
		{
			switch ((shiftInfo >> 1) & 0b11)
			{
				case 0b00: logicalShiftLeft(&offset, shiftAmount); break;
				case 0b01: logicalShiftRight(&offset, shiftAmount); break;
				case 0b10: arithmeticShiftRight(&offset, shiftAmount); break;
				case 0b11: rotateRight(&offset, shiftAmount); break;
			}
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
		if (byteOrWord)
		{
			Memory->set8(addr, getReg(srcReg) & 0xFF);
		}
		else
		{
			Memory->set32(addr, getReg(srcReg));
		}
	}

	if (!preIndexing) { addr += offset; }

	if ((!preIndexing) || (preIndexing && writeback))
	{
		setReg(baseAddrReg, addr);
	}
}

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
		state.CPSR = ((result > op1) ? state.CPSR | Cflag : state.CPSR & ~Cflag);
	}
	//Overflow flag
	uint32_t subAdjOp2;
	if (!addition)
	{
		subAdjOp2 = (~op2 + 1);
	}
	else
	{
		subAdjOp2 = op2;
	}
	bool op1msb = op1 >> 31;
	bool op2msb = subAdjOp2 >> 31;
	bool resultmsb = result >> 31;
	if ((!op1msb && !op2msb && resultmsb) || (op1msb && op2msb && !resultmsb))
	{
		state.CPSR |= Vflag;
	}
	else
	{
		state.CPSR &= ~Vflag;
	}
}

int arm7tdmi::logicalShiftLeft(uint32_t* value, int shiftAmount)
{
	if (shiftAmount > 0)
	{
		bool carryOut = (1 << (32 - shiftAmount)) & *value;
		*value <<= shiftAmount;
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
		*value >>= shiftAmount;
		return carryOut;
	}
	else
	{
		*value = 0;
		return *value & 0x80000000;
	}
}

bool arm7tdmi::arithmeticShiftRight(uint32_t* value, int shiftAmount)
{
	if (shiftAmount > 0)
	{
		bool carryOut = (1 << (shiftAmount - 1)) & *value;
		uint32_t signBit = *value & 0x80000000;
		for (int i = 0; i < shiftAmount; i++)
		{
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
		bool carryOut = (1 << (shiftAmount - 1)) & *value;
		for (int i = 0; i < shiftAmount; i++)
		{
			uint32_t carry = *value & 1;
			*value >>= 1;
			*value |= carry << 31;
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