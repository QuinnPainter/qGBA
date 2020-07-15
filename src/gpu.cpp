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
constexpr int xResolution = 240;
constexpr int yResolution = 160;

constexpr int xWindowSize = xResolution * 2;
constexpr int yWindowSize = yResolution * 2;

gpu::gpu()
{
	cycleCounter = 0;
	currentScanline = 0;
	vblank = false;
	paletteRAM = new uint8_t[1024];
	vram = new uint8_t[98304];
	objectRAM = new uint8_t[1024];
	memset(paletteRAM, 0, 1024);
	memset(vram, 0, 98304);
	memset(objectRAM, 0, 1024);

	screenData = new uint8_t[xResolution * yResolution * 3];

	gpu::window = SDL_CreateWindow("qGBA", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, xWindowSize, yWindowSize, SDL_WINDOW_SHOWN);
	if (gpu::window == NULL)
	{
		logging::fatal("Window could not be created! SDL_Error: " + std::string(SDL_GetError()));
	}
	gpu::screenRenderer = SDL_CreateRenderer(gpu::window, -1, SDL_RENDERER_ACCELERATED);
	if (gpu::screenRenderer == NULL)
	{
		logging::fatal("Renderer could not be created! SDL_Error: " + std::string(SDL_GetError()));
	}
	gpu::screenTexture = SDL_CreateTexture(gpu::screenRenderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, xResolution, yResolution);
}

gpu::~gpu()
{
	SDL_DestroyTexture(gpu::screenTexture);
	SDL_DestroyRenderer(gpu::screenRenderer);
	SDL_DestroyWindow(gpu::window);
	delete[] screenData;
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
		if (vblank == false)
		{
			drawScanline();
		}
		currentScanline++;
		if (currentScanline == vDrawScanlines)
		{
			vblank = true;
			displayScreen();
		}
		if (currentScanline == vDrawScanlines + vBlankScanlines)
		{
			vblank = false;
			currentScanline = 0;
		}
	}
}

void gpu::drawScanline()
{
	/*
	0 - 4   Red Intensity(0 - 31)
	5 - 9   Green Intensity(0 - 31)
	10 - 14 Blue Intensity(0 - 31)
	15    Not used
	*/
	switch (videoMode)
	{
		case 3:
		{
			for (int i = 0; i < xResolution; i++)
			{
				int addr1 = (currentScanline * xResolution * 2) + (i * 2);
				int addr2 = (currentScanline * xResolution) + i;
				uint16_t colour = vram[addr1] | ((uint16_t)vram[addr1 + 1] << 8);
				screenData[addr2 * 3] = (colour & 0x1F) << 3;
				screenData[(addr2 * 3) + 1] = ((colour >> 5) & 0x1F) << 3;
				screenData[(addr2 * 3) + 2] = ((colour >> 10) & 0x1F) << 3;
			}
			break;
		}
		case 4:
		{
			for (int i = 0; i < xResolution; i++)
			{
				int addr = (currentScanline * xResolution) + i;
				int frame = bitmapFrame ? 0xA000 : 0;
				uint8_t paletteIndex = vram[addr + frame];
				uint16_t paletteColour = paletteRAM[paletteIndex * 2] | ((uint16_t)paletteRAM[(paletteIndex * 2) + 1] << 8);
				screenData[addr * 3] = (paletteColour & 0x1F) << 3;
				screenData[(addr * 3) + 1] = ((paletteColour >> 5) & 0x1F) << 3;
				screenData[(addr * 3) + 2] = ((paletteColour >> 10) & 0x1F) << 3;
			}
			break;
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
	}
	else if (addr >= 0x07000000 && addr < 0x07000400)
	{
		objectRAM[addr - 0x07000000] = value;
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
			logging::important("Switched to video mode: " + helpers::intToHex(videoMode), "gpu");
			if (videoMode > 5)
			{
				logging::fatal("Switched to invalid video mode: " + helpers::intToHex(videoMode), "gpu");
			}
			bitmapFrame = value & 0x10;
			break;
		case 0x01: // DISPCNT byte 2
			break;
		case 0x04: // DISPSTAT byte 1
			break;
		case 0x05: // DISPSTAT byte 2
			break;
		case 0x06: // VCOUNT byte 1
			logging::error("Write to VCOUNT: 0x4000006", "gpu");
			break;
		case 0x07: // VCOUNT byte 2
			logging::error("Write to VCOUNT: 0x4000007", "gpu");
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
							| (uint8_t)bitmapFrame << 4;
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
		case 0x06: // VCOUNT byte 1
			return currentScanline;
		case 0x07: // VCOUNT byte 2 (unused)
			return 0;
		default:
			logging::error("Read from unhandled GPU register: " + helpers::intToHex(addr), "gpu");
			return 0;
	}
}

void gpu::displayScreen()
{
	SDL_UpdateTexture(gpu::screenTexture, NULL, gpu::screenData, xResolution * 3);
	SDL_RenderCopy(gpu::screenRenderer, gpu::screenTexture, NULL, NULL);
	SDL_RenderPresent(gpu::screenRenderer);
}