#include "logging.hpp"
#include "helpers.hpp"
#include "arm7tdmi.hpp"
#include "memory.hpp"
#include "gpu.hpp"

int main(int argc, char** argv)
{
	//Read the ROM file
	if (argc < 2)
	{
		logging::fatal("Need a ROM file!", "qGBA");
	}
	FILE* romFile = fopen(argv[1], "rb");
	if (!romFile)
	{
		logging::fatal("Couldn't open " + std::string(argv[1]), "qGBA");
	}
	//Get the file size
	fseek(romFile, 0, SEEK_END);
	unsigned int romSize = ftell(romFile);
	fseek(romFile, 0, SEEK_SET);
	//Copy the rom into memory
	uint8_t* rom = new uint8_t[romSize];
	fread(rom, romSize, 1, romFile);
	fclose(romFile);
	logging::info("Opened " + std::string(argv[1]), "qGBA");

	//Read the ROM header
	uint8_t ninLogo[156] = {
		0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21, 0x3D, 0x84, 0x82, 0x0A,
		0x84, 0xE4, 0x09, 0xAD, 0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21,
		0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20, 0x10, 0x46, 0x4A, 0x4A,
		0xF8, 0x27, 0x31, 0xEC, 0x58, 0xC7, 0xE8, 0x33, 0x82, 0xE3, 0xCE, 0xBF,
		0x85, 0xF4, 0xDF, 0x94, 0xCE, 0x4B, 0x09, 0xC1, 0x94, 0x56, 0x8A, 0xC0,
		0x13, 0x72, 0xA7, 0xFC, 0x9F, 0x84, 0x4D, 0x73, 0xA3, 0xCA, 0x9A, 0x61,
		0x58, 0x97, 0xA3, 0x27, 0xFC, 0x03, 0x98, 0x76, 0x23, 0x1D, 0xC7, 0x61,
		0x03, 0x04, 0xAE, 0x56, 0xBF, 0x38, 0x84, 0x00, 0x40, 0xA7, 0x0E, 0xFD,
		0xFF, 0x52, 0xFE, 0x03, 0x6F, 0x95, 0x30, 0xF1, 0x97, 0xFB, 0xC0, 0x85,
		0x60, 0xD6, 0x80, 0x25, 0xA9, 0x63, 0xBE, 0x03, 0x01, 0x4E, 0x38, 0xE2,
		0xF9, 0xA2, 0x34, 0xFF, 0xBB, 0x3E, 0x03, 0x44, 0x78, 0x00, 0x90, 0xCB,
		0x88, 0x11, 0x3A, 0x94, 0x65, 0xC0, 0x7C, 0x63, 0x87, 0xF0, 0x3C, 0xAF,
		0xD6, 0x25, 0xE4, 0x8B, 0x38, 0x0A, 0xAC, 0x72, 0x21, 0xD4, 0xF8, 0x07
	};
	bool ninLogoValid = true;
	for (int i = 0; i < sizeof(ninLogo); i++)
	{
		if (rom[i + 4] != ninLogo[i])
		{
			if (i == 152 && (rom[i + 4] & 0b01111011) == ninLogo[i])
			{
				//Bits 2 and 7 of this specific byte don't matter for verification.
				//When both are set, debug mode is on.
				if ((rom[i + 4] & 0b10000100) == 0b10000100)
				{
					logging::warning("This ROM has debug mode on. This emulator doesn't handle that yet", "qGBA");
					continue;
				}
			}
			else if (i == 154 && (rom[i + 4] & 0b11111100) == ninLogo[i])
			{
				//Bits 0 and 1 of this byte also don't matter for verification, apparently.
				//Doesn't seem to activate any special feature, though.
				continue;
			}
			logging::warning("Nintendo Logo in ROM is invalid", "qGBA");
			ninLogoValid = false;
			break;
		}
	}
	if (ninLogoValid)
	{
		logging::info("Nintendo logo in ROM is valid", "qGBA");
	}
	std::string gameName = "";
	for (int i = 0; i < 12; i++)
	{
		gameName += (char)rom[0xA0 + i];
	}
	logging::info("Game name: " + gameName, "qGBA");
	logging::info(std::string("Game code: ") + (char)rom[0xAC] + (char)rom[0xAD] + (char)rom[0xAE] + (char)rom[0xAF], "qGBA");
	logging::info(std::string("Maker code: ") + (char)rom[0xB0] + (char)rom[0xB1], "qGBA");
	if (rom[0xB2] != 0x96)
	{
		logging::warning("The ROM header fixed byte is wrong ([0xB2] != 0x96)", "qGBA");
	}
	//0xB3 - Main unit code. Should be 0, but it doesn't matter.
	//0xB4 - Device type. Normally 0. Only matters for the GBA hardware debugger.
	//0xB5 to 0xBB - Reserved space (All 0). Doesn't matter.
	logging::info(std::string("Version number: ") + std::to_string(rom[0xBC]), "qGBA");
	uint8_t chk = 0;
	for (int i = 0xA0; i <= 0xBC; i++)
	{
		chk -= rom[i];
	}
	chk -= 0x19;
	if (rom[0xBD] != chk)
	{
		logging::warning("Header checksum is incorrect (is " + helpers::intToHex(rom[0xBD]) + " should be " + helpers::intToHex(chk) + ")", "qGBA");
	}
	else
	{
		logging::info("Header checksum: " + helpers::intToHex(chk) + " (ok)", "qGBA");
	}
	//0xBE and 0xBF - Reserved space (All 0). Doesn't matter.
	//Rest of the header only matters for multiboot.

	gpu GPU{};
	memory mem(rom, romSize, &GPU);
	arm7tdmi CPU(&mem, false);

	//for (int i = 0; i < 200; i++)
	while(true)
	{
		CPU.step();
	}

	delete[] rom;
	logging::info("Exited successfully", "qGBA");
	return 0;
}