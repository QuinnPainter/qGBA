#include "gpu.hpp"
#include "logging.hpp"
#include "helpers.hpp"

/* The GBA has a TFT color LCD that is 240 x 160 pixels in size
and has a refresh rate of exactly 280,896 cpu cycles per frame, or
around 59.73 hz. Most GBA programs will need to structure themselves
around this refresh rate. Each refresh consists of a 160 scanline vertical
draw (VDraw) period followed by a 68 scanline blank (VBlank) period.
Furthermore, each of these scanlines consists of a 1004 cycle draw period (HDraw)
followed by a 228 cycle blank period (HBlank). During the HDraw and VDraw periods
the graphics hardware processes background and obj (sprite) data and draws it on
the screen, while the HBlank and VBlank periods are left open so that program code
can modify background and obj data without risk of creating graphical artifacts. */

constexpr int cyclesPerScanline = 1232;
constexpr int vDrawScanlines = 160;
constexpr int vBlankScanlines = 68;

gpu::gpu()
{
	cycleCounter = 0;
	currentScanline = 0;
	vblank = false;
	paletteRAM = new uint8_t[1024];
	vram = new uint8_t[98304];
	objectRAM = new uint8_t[1024];
	memset(paletteRAM, 0, sizeof(paletteRAM));
	memset(vram, 0, sizeof(vram));
	memset(objectRAM, 0, sizeof(objectRAM));
}

gpu::~gpu()
{
	delete[] paletteRAM;
	delete[] vram;
	delete[] objectRAM;
}

void gpu::step(int cycles)
{
	cycleCounter += cycles;
	if (cycleCounter >= cyclesPerScanline)
	{
		cycleCounter %= cyclesPerScanline;
		currentScanline++;
		if (currentScanline >= vDrawScanlines)
		{
			vblank = true;
			if (currentScanline >= vDrawScanlines + vBlankScanlines)
			{
				vblank = false;
				currentScanline = 0;
			}
		}
	}
}

void gpu::setVRAM(uint32_t addr, uint8_t value)
{
	if (addr >= 0x05000000 && addr < 0x05000400)
	{
		paletteRAM[addr - 0x05000000] = value;
	}
	else if (addr >= 0x06000000 && addr < 0x06018000)
	{
		vram[addr - 0x06000000] = value;
		logging::important("VRAM write: " + helpers::intToHex(addr), "gpu");
	}
	else if (addr >= 0x07000000 && addr < 0x07000400)
	{
		objectRAM[addr - 07000000] = value;
	}
	else
	{
		logging::error("Write to invalid VRAM address: " + helpers::intToHex(addr), "gpu");
	}
}

uint8_t gpu::getVRAM(uint32_t addr)
{
	if (addr >= 0x05000000 && addr < 0x05000400)
	{
		return paletteRAM[addr - 0x05000000];
	}
	else if (addr >= 0x06000000 && addr < 0x06018000)
	{
		return vram[addr - 0x06000000];
	}
	else if (addr >= 0x07000000 && addr < 0x07000400)
	{
		return objectRAM[addr - 07000000];
	}
	else
	{
		logging::error("Read from invalid VRAM address: " + helpers::intToHex(addr), "gpu");
		return 0;
	}
}

void gpu::setRegister(uint32_t addr, uint8_t value)
{
	switch (addr - 0x4000000)
	{
		case 0x00: // DISPCNT byte 1
			videoMode = value & 0x7;
			if (videoMode > 5)
			{
				logging::fatal("Switched to invalid video mode: " + helpers::intToHex(value), "gpu");
			}
			break;
		case 0x01: // DISPCNT byte 2
			break;
		case 0x04: // DISPSTAT byte 1
			break;
		case 0x05: // DISPSTAT byte 2
			break;
		default:
			logging::error("Write to unhandled GPU register: " + helpers::intToHex(addr), "gpu");
			break;
	}
}

uint8_t gpu::getRegister(uint32_t addr)
{
	switch (addr - 0x4000000)
	{
		case 0x00: // DISPCNT byte 1
		{
			uint8_t ret = videoMode & 0x07
						  | 0x08; // Game Boy Colour mode
			return ret;
		}
		case 0x01: // DISPCNT byte 2
			return 0;
		case 0x04: // DISPSTAT byte 1
		{
			uint8_t ret = (uint8_t)vblank;
			return ret;
		}
		case 0x05: // DISPSTAT byte 2
			return 0;
		default:
			logging::error("Read from unhandled GPU register: " + helpers::intToHex(addr), "gpu");
			return 0;
	}
}