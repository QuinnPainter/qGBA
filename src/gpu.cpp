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
		case 0:
		{
			if (enableBG0)
			{
				for (int x = 0; x < xResolution; x++)
				{
					uint32_t mapBaseAddr = (uint32_t)BG0Control.screenBaseBlock << 11;
					uint32_t mapEntryAddr = mapBaseAddr + ((x / 8) * 2) + ((currentScanline / 8) * 32 * 2);
					uint16_t mapEntry = vram[mapEntryAddr] | (((uint16_t)vram[mapEntryAddr + 1]) << 8);

					uint32_t tileBaseAddr = (uint32_t)BG0Control.charBaseBlock << 14;
					uint32_t tileAddr = tileBaseAddr + ((mapEntry & 0x3FF) * (BG0Control.colourDepth ? 64 : 32));
					uint8_t tileRow = currentScanline % 8;
					uint8_t tileColumn = x % 8;
					if (mapEntry & 0x400) // Horizontal Flip
					{
						tileColumn = 7 - tileColumn;
					}
					if (mapEntry & 0x800) // Vertical flip
					{
						tileRow = 7 - tileRow;
					}
					if (BG0Control.colourDepth) // 256 colours, 1 palette.
					{
						uint8_t pixelEntryAddr = (tileRow * 8) + tileColumn;
						uint8_t pixelEntry = vram[tileAddr + pixelEntryAddr];
						uint16_t paletteEntryAddr = pixelEntry;
						uint16_t paletteColour = paletteRAM[paletteEntryAddr * 2] | ((uint16_t)paletteRAM[(paletteEntryAddr * 2) + 1] << 8);
						int addr = (currentScanline * xResolution) + x;
						screenData[addr * 3] = (paletteColour & 0x1F) << 3;
						screenData[(addr * 3) + 1] = ((paletteColour >> 5) & 0x1F) << 3;
						screenData[(addr * 3) + 2] = ((paletteColour >> 10) & 0x1F) << 3;
					}
					else // 16 colours, 16 palettes.
					{
						uint8_t paletteNum = mapEntry >> 12;
						uint16_t paletteBaseAddr = paletteNum * 16;
						uint16_t paletteEntryAddr = paletteBaseAddr;
						uint8_t pixelEntryAddr = (tileRow * 4) + tileColumn / 2;
						uint8_t pixelEntry = vram[tileAddr + pixelEntryAddr];
						if (tileColumn & 1)
						{
							paletteEntryAddr += pixelEntry >> 4;
						}
						else
						{
							paletteEntryAddr += pixelEntry & 0xF;
						}
						uint16_t paletteColour = paletteRAM[paletteEntryAddr * 2] | ((uint16_t)paletteRAM[(paletteEntryAddr * 2) + 1] << 8);
						int addr = (currentScanline * xResolution) + x;
						screenData[addr * 3] = (paletteColour & 0x1F) << 3;
						screenData[(addr * 3) + 1] = ((paletteColour >> 5) & 0x1F) << 3;
						screenData[(addr * 3) + 2] = ((paletteColour >> 10) & 0x1F) << 3;
					}
				}
			}
			break;
		}
		case 3:
		{
			if (enableBG2)
			{
				for (int x = 0; x < xResolution; x++)
				{
					int addr1 = (currentScanline * xResolution * 2) + (x * 2);
					int addr2 = (currentScanline * xResolution) + x;
					uint16_t colour = vram[addr1] | ((uint16_t)vram[addr1 + 1] << 8);
					screenData[addr2 * 3] = (colour & 0x1F) << 3;
					screenData[(addr2 * 3) + 1] = ((colour >> 5) & 0x1F) << 3;
					screenData[(addr2 * 3) + 2] = ((colour >> 10) & 0x1F) << 3;
				}
			}
			break;
		}
		case 4:
		{
			if (enableBG2)
			{
				for (int x = 0; x < xResolution; x++)
				{
					int addr = (currentScanline * xResolution) + x;
					int frame = bitmapFrame ? 0xA000 : 0;
					uint8_t paletteIndex = vram[addr + frame];
					uint16_t paletteColour = paletteRAM[paletteIndex * 2] | ((uint16_t)paletteRAM[(paletteIndex * 2) + 1] << 8);
					screenData[addr * 3] = (paletteColour & 0x1F) << 3;
					screenData[(addr * 3) + 1] = ((paletteColour >> 5) & 0x1F) << 3;
					screenData[(addr * 3) + 2] = ((paletteColour >> 10) & 0x1F) << 3;
				}
			}
			break;
		}
		case 5:
		{
			if (currentScanline >= 128)
			{
				break;
			}
			if (enableBG2)
			{
				for (int x = 0; x < 160; x++)
				{
					int frame = bitmapFrame ? 0xA000 : 0;
					int addr1 = (currentScanline * 160 * 2) + (x * 2) + frame;
					int addr2 = (currentScanline * xResolution) + x;
					uint16_t colour = vram[addr1] | ((uint16_t)vram[addr1 + 1] << 8);
					screenData[addr2 * 3] = (colour & 0x1F) << 3;
					screenData[(addr2 * 3) + 1] = ((colour >> 5) & 0x1F) << 3;
					screenData[(addr2 * 3) + 2] = ((colour >> 10) & 0x1F) << 3;
				}
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
			enableBG0 = value & 0b00001;
			enableBG1 = value & 0b00010;
			enableBG2 = value & 0b00100;
			enableBG3 = value & 0b01000;
			enableOBJ = value & 0b10000;
			break;
		case 0x02: case 0x03: // Green Swap - unimplemented
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
		case 0x08: BG0Control.setLow(value); break;
		case 0x09: BG0Control.setHigh(value); break;
		case 0x0A: BG1Control.setLow(value); break;
		case 0x0B: BG1Control.setHigh(value); break;
		case 0x0C: BG2Control.setLow(value); break;
		case 0x0D: BG2Control.setHigh(value); break;
		case 0x0E: BG3Control.setLow(value); break;
		case 0x0F: BG3Control.setHigh(value); break;
		case 0x10: BG0XOffset = (BG0XOffset & ~0xFF) | value; break;
		case 0x11: BG0XOffset = (BG0XOffset & 0xFF) | (((uint16_t)value & 0x1) << 8); break;
		case 0x12: BG0YOffset = (BG0YOffset & ~0xFF) | value; break;
		case 0x13: BG0YOffset = (BG0YOffset & 0xFF) | (((uint16_t)value & 0x1) << 8); break;
		case 0x14: BG1XOffset = (BG1XOffset & ~0xFF) | value; break;
		case 0x15: BG1XOffset = (BG1XOffset & 0xFF) | (((uint16_t)value & 0x1) << 8); break;
		case 0x16: BG1YOffset = (BG1YOffset & ~0xFF) | value; break;
		case 0x17: BG1YOffset = (BG1YOffset & 0xFF) | (((uint16_t)value & 0x1) << 8); break;
		case 0x18: BG2XOffset = (BG2XOffset & ~0xFF) | value; break;
		case 0x19: BG2XOffset = (BG2XOffset & 0xFF) | (((uint16_t)value & 0x1) << 8); break;
		case 0x1A: BG2YOffset = (BG2YOffset & ~0xFF) | value; break;
		case 0x1B: BG2YOffset = (BG2YOffset & 0xFF) | (((uint16_t)value & 0x1) << 8); break;
		case 0x1C: BG3XOffset = (BG3XOffset & ~0xFF) | value; break;
		case 0x1D: BG3XOffset = (BG3XOffset & 0xFF) | (((uint16_t)value & 0x1) << 8); break;
		case 0x1E: BG3YOffset = (BG3YOffset & ~0xFF) | value; break;
		case 0x1F: BG3YOffset = (BG3YOffset & 0xFF) | (((uint16_t)value & 0x1) << 8); break;
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
			uint8_t ret = (videoMode & 0x07)
				| ((uint8_t)bitmapFrame << 4);
			return ret;
		}
		case 0x01: // DISPCNT byte 2
		{
			uint8_t ret = (uint8_t)enableBG0
				| ((uint8_t)enableBG1 << 1)
				| ((uint8_t)enableBG2 << 2)
				| ((uint8_t)enableBG3 << 3)
				| ((uint8_t)enableOBJ << 4);
			return ret;
		}
		case 0x02: case 0x03: // Green Swap - unimplemented
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
		case 0x08: return BG0Control.getLow();
		case 0x09: return BG0Control.getHigh();
		case 0x0A: return BG1Control.getLow();
		case 0x0B: return BG1Control.getHigh();
		case 0x0C: return BG2Control.getLow();
		case 0x0D: return BG2Control.getHigh();
		case 0x0E: return BG3Control.getLow();
		case 0x0F: return BG3Control.getHigh();
		case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
		case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
			return 0; // BG Scroll Offsets are Write-Only
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

void bgControl::setLow(uint8_t value)
{
	priority = value & 0x3;
	charBaseBlock = (value >> 2) & 0x3;
	mosaic = value & 0x40;
	colourDepth = value & 0x80;
}

void bgControl::setHigh(uint8_t value)
{
	screenBaseBlock = value & 0x1F;
	displayOverflow = value & 0x20;
	screenSize = (value >> 6) & 0x3;
}

uint8_t bgControl::getLow()
{
	uint8_t ret = priority
		| (charBaseBlock << 2)
		| ((uint8_t)mosaic << 6)
		| ((uint8_t)colourDepth << 7);
	return ret;
}

uint8_t bgControl::getHigh()
{
	uint8_t ret = screenBaseBlock
		| ((uint8_t)displayOverflow << 5)
		| (screenSize << 6);
	return ret;
}