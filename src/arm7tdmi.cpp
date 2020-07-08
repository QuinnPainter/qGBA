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
		if (state.CPSR & 0x20)
		{
			//THUMB
			state.R[15] += 2;
			Pipeline.executeInstr = Pipeline.decodeInstr;
			Pipeline.decodeInstr = Pipeline.fetchInstr;
			Pipeline.fetchInstr = Memory->get16(state.R[15]);
		}
		else
		{
			//ARM
			state.R[15] += 4;
			Pipeline.executeInstr = Pipeline.decodeInstr;
			Pipeline.decodeInstr = Pipeline.fetchInstr;
			Pipeline.fetchInstr = Memory->get32(state.R[15]);
		}
	}
	else
	{
		//Rebuild the pipeline
		if (state.CPSR & 0x20)
		{
			//THUMB
			Pipeline.executeInstr = Memory->get16(state.R[15]);
			Pipeline.decodeInstr = Memory->get16(state.R[15] + 2);
			Pipeline.fetchInstr = Memory->get16(state.R[15] + 4);
			state.R[15] += 4;
		}
		else
		{
			//ARM
			Pipeline.executeInstr = Memory->get32(state.R[15]);
			Pipeline.decodeInstr = Memory->get32(state.R[15] + 4);
			Pipeline.fetchInstr = Memory->get32(state.R[15] + 8);
			state.R[15] += 8;
		}
		Pipeline.valid = true;
	}

	logging::info(helpers::intToHex(getReg(15)) + " " + helpers::intToHex(Pipeline.executeInstr));

	if (state.CPSR & 0x20)
	{
		//THUMB
		switch ((Pipeline.executeInstr >> 13) & 0b111)
		{
			case 0b000:
			{
				if ((Pipeline.executeInstr & 0x1800) == 0x1800)
				{
					THUMB_AddSubtract();
				}
				else
				{
					THUMB_MoveShiftedRegister();
				}
				break;
			}
			case 0b001:
			{
				THUMB_MvCmpAddSubImmediate();
				break;
			}
			case 0b010:
			{
				uint8_t checkBits = (Pipeline.executeInstr >> 10) & 0b111;
				if (checkBits == 0b000)
				{
					THUMB_ALUOps();
				}
				else if (checkBits == 0b001)
				{
					THUMB_HiRegOps_BranchExchange();
				}
				else if ((checkBits & 0b110) == 0b010)
				{
					THUMB_LoadPCRelative();
				}
				else if ((checkBits & 0b100) == 0b100)
				{
					if (Pipeline.executeInstr & 0x200)
					{
						logging::error("Unimplemented THUMB instruction: Load/Store Sign-Extended: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
					}
					else
					{
						logging::error("Unimplemented THUMB instruction: Load/Store Reg Offset: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
					}
				}
				break;
			}
			case 0b011:
			{
				logging::error("Unimplemented THUMB instruction: Load/Store Immediate Offset: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
				break;
			}
			case 0b100:
			{
				if (Pipeline.executeInstr & 0x1000)
				{
					logging::error("Unimplemented THUMB instruction: SP Relative Load/Store: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
				}
				else
				{
					logging::error("Unimplemented THUMB instruction: Load/Store Halfword: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
				}
				break;
			}
			case 0b101:
			{
				if (Pipeline.executeInstr & 0x1000)
				{
					if (Pipeline.executeInstr & 0x400)
					{
						logging::error("Unimplemented THUMB instruction: Push/Pop Registers: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
					}
					else
					{
						logging::error("Unimplemented THUMB instruction: Add Offset to SP: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
					}
				}
				else
				{
					logging::error("Unimplemented THUMB instruction: Load Address: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
				}
				break;
			}
			case 0b110:
			{
				if (Pipeline.executeInstr & 0x1000)
				{
					if ((Pipeline.executeInstr & 0x0F00) == 0x0F00)
					{
						logging::error("Unimplemented THUMB instruction: Software Interrupt: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
					}
					else
					{
						THUMB_ConditionalBranch();
					}
				}
				else
				{
					THUMB_MultipleLoadStore();
				}
				break;
			}
			case 0b111:
			{
				if (Pipeline.executeInstr & 0x1000)
				{
					THUMB_LongBranchLink();
				}
				else
				{
					logging::error("Unimplemented THUMB instruction: Unconditional Branch: " + helpers::intToHex(Pipeline.executeInstr), "arm7tdmi");
				}
				break;
			}
		}
	}
	else
	{
		//ARM
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
	setReg(15, addr & (~0x1));
	if (addr & 0x1)
	{
		state.CPSR |= 0x20;
		logging::info("Switched to THUMB", "arm7tdmi");
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

	if (offsetImmediate == false)
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

// Thumb instructions

void arm7tdmi::THUMB_MoveShiftedRegister()
{
	logging::info("Thumb Move Shifted Register", "arm7tdmi");
	uint8_t dest_reg = (Pipeline.executeInstr & 0x7);
	uint8_t src_reg = ((Pipeline.executeInstr >> 3) & 0x7);
	uint8_t offset = ((Pipeline.executeInstr >> 6) & 0x1F);
	uint8_t op = ((Pipeline.executeInstr >> 11) & 0x3);

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

void arm7tdmi::THUMB_AddSubtract()
{
	logging::info("Thumb Add/Subtract", "arm7tdmi");
	uint8_t dest_reg = (Pipeline.executeInstr & 0x7);
	uint8_t src_reg = ((Pipeline.executeInstr >> 3) & 0x7);
	uint8_t op = ((Pipeline.executeInstr >> 9) & 0x3);

	uint32_t input = getReg(src_reg);
	uint32_t result = 0;
	uint32_t operand = 0;
	uint8_t imm_reg = ((Pipeline.executeInstr >> 6) & 0x7);

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

void arm7tdmi::THUMB_MvCmpAddSubImmediate()
{
	logging::info("Thumb Mv/Cmp/Add/Sub Immediate", "arm7tdmi");
	uint8_t dest_reg = ((Pipeline.executeInstr >> 8) & 0x7);
	uint8_t op = ((Pipeline.executeInstr >> 11) & 0x3);

	uint32_t input = getReg(dest_reg);
	uint32_t result = 0;

	uint32_t operand = (Pipeline.executeInstr & 0xFF);

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

void arm7tdmi::THUMB_ALUOps()
{
	logging::info("Thumb ALU Ops", "arm7tdmi");
	uint8_t dest_reg = (Pipeline.executeInstr & 0x7);
	uint8_t src_reg = ((Pipeline.executeInstr >> 3) & 0x7);
	uint8_t op = ((Pipeline.executeInstr >> 6) & 0xF);

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
			
			setFlagsLogical(result, shift_out);
			setReg(dest_reg, result);
			break;
		case 0x3: //LSR
			operand &= 0xFF;
			if (operand != 0) { shift_out = logicalShiftRight(&input, operand); }
			result = input;
	
			setFlagsLogical(result, shift_out);
			setReg(dest_reg, result);
			break;
		case 0x4: //ASR
			operand &= 0xFF;
			if (operand != 0) { shift_out = arithmeticShiftRight(&input, operand); }
			result = input;
	
			setFlagsLogical(result, shift_out);
			setReg(dest_reg, result);
			break;
		case 0x5: //ADC
			result = (input + operand + carry_out);
			setFlagsArithmetic(input, operand, result, true);
			setReg(dest_reg, result);
			break;
		case 0x6: //SBC
			carry_out ^= 0x1; //Invert carry
	
			result = (input - operand - carry_out);
			setFlagsArithmetic(input, operand, result, false);
			setReg(dest_reg, result);
			break;
		case 0x7: //ROR
			operand &= 0xFF;
			if (operand != 0) { shift_out = rotateRight(&input, operand); }
			result = input;
	
			setFlagsLogical(result, shift_out);
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

void arm7tdmi::THUMB_HiRegOps_BranchExchange()
{
	logging::info("Thumb Hi Reg Ops / Branch Exchange", "arm7tdmi");
	uint8_t dest_reg = (Pipeline.executeInstr & 0x7);
	uint8_t src_reg = ((Pipeline.executeInstr >> 3) & 0x7);
	uint8_t sr_msb = (Pipeline.executeInstr & 0x40) >> 6;
	uint8_t dr_msb = (Pipeline.executeInstr & 0x80) >> 7;

	src_reg |= sr_msb << 3;
	dest_reg |= dr_msb << 3;
	//if (sr_msb) { src_reg |= 0x8; }
	//if (dr_msb) { dest_reg |= 0x8; }

	uint8_t op = ((Pipeline.executeInstr >> 8) & 0x3);

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
				logging::info("Switching to ARM", "arm7tdmi");
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

void arm7tdmi::THUMB_LoadPCRelative()
{
	uint16_t offset = (Pipeline.executeInstr & 0xFF) * 4;
	uint8_t dest_reg = ((Pipeline.executeInstr >> 8) & 0x7);

	uint32_t load_addr = ((getReg(15) + 4) & ~0x2) + offset;

	uint32_t value = Memory->get32(load_addr);
	setReg(dest_reg, value);
	//logging::info("addr " + helpers::intToHex(load_addr));
	logging::info("Thumb Load PC Relative (load R" + helpers::intToHex(dest_reg) + " with " + helpers::intToHex(value) + ")", "arm7tdmi");
}

void arm7tdmi::THUMB_MultipleLoadStore()
{
	logging::info("Thumb Multiple Load Store", "arm7tdmi");
	uint8_t r_list = (Pipeline.executeInstr & 0xFF);
	uint8_t base_reg = ((Pipeline.executeInstr >> 8) & 0x7);
	uint8_t op = (Pipeline.executeInstr & 0x800) ? 1 : 0;

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

void arm7tdmi::THUMB_ConditionalBranch()
{
	uint8_t offset = (Pipeline.executeInstr & 0xFF);
	uint8_t op = ((Pipeline.executeInstr >> 8) & 0xF);
	logging::info("Thumb Conditional Branch (condition " + helpers::intToHex(op) + ")", "arm7tdmi");

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

	logging::info(doBranch ? "condition true" : "condition false");
	if (doBranch)
	{
		logging::info("branched to " + helpers::intToHex(getReg(15) + jump_addr + 4));
		setReg(15, getReg(15) + 4 + jump_addr);
	}
}

void arm7tdmi::THUMB_LongBranchLink()
{
	logging::info("Thumb Long Branch and Link", "arm7tdmi");
	//Determine if this is the first or second instruction executed
	bool first_op = !(((Pipeline.executeInstr >> 11) & 0x1F) == 0x1F);

	uint32_t lbl_addr = 0;

	//Perform 1st 16-bit operation
	if (first_op)
	{
		uint32_t r15 = getReg(15);
		uint8_t pre_bit = (r15 & 0x800000) ? 1 : 0;

		//Grab upper 11-bits of destination address
		lbl_addr = ((Pipeline.executeInstr & 0x7FF) << 12);

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
		lbl_addr += ((Pipeline.executeInstr & 0x7FF) << 1);

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