#pragma once
#include <cstdint>
#include "memory.hpp"

enum class instruction
{
	UNDEFINED,
	PIPELINE_FILL,
	ARM_3, // No 1 and 2 (numbers are based off of chapter nums in ARM Manual)
	ARM_4,
	ARM_5, // PSR Transfer (6) is called as part of Data Processing (5)
	ARM_7, // Multiply (7) and Multiply Long (8) are combined.
	ARM_9,
	ARM_10,
	ARM_11,
	ARM_12,
	ARM_13,
	THUMB_1,
	THUMB_2,
	THUMB_3,
	THUMB_4,
	THUMB_5,
	THUMB_6,
	THUMB_7,
	THUMB_8,
	THUMB_9,
	THUMB_10,
	THUMB_11,
	THUMB_12,
	THUMB_13,
	THUMB_14,
	THUMB_15,
	THUMB_16,
	THUMB_17,
	THUMB_18,
	THUMB_19
};

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
	uint32_t R13_abt;
	uint32_t R14_abt;
	uint32_t SPSR_abt;
	uint32_t R13_und;
	uint32_t R14_und;
	uint32_t SPSR_und;
	uint32_t R8_fiq;
	uint32_t R9_fiq;
	uint32_t R10_fiq;
	uint32_t R11_fiq;
	uint32_t R12_fiq;
	uint32_t R13_fiq;
	uint32_t R14_fiq;
	uint32_t SPSR_fiq;
};

struct pipeline
{
	uint32_t instrPipeline[3];
	instruction instrOperation[3];
	uint8_t pipelinePtr;
	bool pendingFlush;
};

class arm7tdmi
{
	public:
		arm7tdmi(memory* mem, bool bios, bool* requestIRQ, bool* halted);
		void step();
	private:
		cpuState state;
		pipeline Pipeline;
		bool* requestIRQ;
		bool* halted;
		memory* Memory;
		bool checkCondCode(uint32_t instr);
		uint32_t getReg(int index);
		void setReg(int index, uint32_t value);
		uint32_t getSPSR();
		void setSPSR(uint32_t value);
		void fetch();
		void decode();
		void execute();
		void flushPipeline();
		void processInterrupt();
		void softwareInterrupt();

		//ARM instructions
		void ARM_BranchExchange(uint32_t currentInstruction);
		void ARM_Branch(uint32_t currentInstruction);
		void ARM_DataProcessing(uint32_t currentInstruction);
		void ARM_PSRTransfer(uint32_t currentInstruction);
		void ARM_Multiply(uint32_t currentInstruction);
		void ARM_SingleDataTransfer(uint32_t currentInstruction);
		void ARM_HalfwordDataTransfer(uint32_t currentInstruction);
		void ARM_BlockDataTransfer(uint32_t currentInstruction);
		void ARM_SingleDataSwap(uint32_t currentInstruction);

		//THUMB instructions
		void THUMB_MoveShiftedRegister(uint16_t currentInstruction);
		void THUMB_AddSubtract(uint16_t currentInstruction);
		void THUMB_MvCmpAddSubImmediate(uint16_t currentInstruction);
		void THUMB_ALUOps(uint16_t currentInstruction);
		void THUMB_HiRegOps_BranchExchange(uint16_t currentInstruction);
		void THUMB_LoadPCRelative(uint16_t currentInstruction);
		void THUMB_LoadStoreRegOffset(uint16_t currentInstruction);
		void THUMB_LoadStoreSignExtend(uint16_t currentInstruction);
		void THUMB_LoadStoreImmediate(uint16_t currentInstruction);
		void THUMB_LoadStoreHalfword(uint16_t currentInstruction);
		void THUMB_LoadStoreSPRelative(uint16_t currentInstruction);
		void THUMB_LoadAddress(uint16_t currentInstruction);
		void THUMB_AddOffsetSP(uint16_t currentInstruction);
		void THUMB_PushPop(uint16_t currentInstruction);
		void THUMB_MultipleLoadStore(uint16_t currentInstruction);
		void THUMB_ConditionalBranch(uint16_t currentInstruction);
		void THUMB_UnconditionalBranch(uint16_t currentInstruction);
		void THUMB_LongBranchLink(uint16_t currentInstruction);

		//Helper functions
		void setFlagsLogical(uint32_t result, int carryOut);
		void setFlagsArithmetic(uint32_t op1, uint32_t op2, uint32_t result, bool addition);
		int logicalShiftLeft(uint32_t* value, int shiftAmount);
		bool logicalShiftRight(uint32_t* value, int shiftAmount);
		bool arithmeticShiftRight(uint32_t* value, int shiftAmount);
		bool rotateRight(uint32_t* value, int shiftAmount);
		void rotateRightSpecial(uint32_t* value, int shiftAmount);
};