#include "input.hpp"
#include "logging.hpp"

/*4000130h - KEYINPUT - Key Status (R)
  Bit   Expl.
  0     Button A        (0=Pressed, 1=Released)
  1     Button B        (etc.)
  2     Select          (etc.)
  3     Start           (etc.)
  4     Right           (etc.)
  5     Left            (etc.)
  6     Up              (etc.)
  7     Down            (etc.)
  8     Button R        (etc.)
  9     Button L        (etc.)
  10-15 Not used

4000132h - KEYCNT - Key Interrupt Control (R/W)
  Bit   Expl.
  0     Button A        (0=Ignore, 1=Select)
  1     Button B        (etc.)
  2     Select          (etc.)
  3     Start           (etc.)
  4     Right           (etc.)
  5     Left            (etc.)
  6     Up              (etc.)
  7     Down            (etc.)
  8     Button R        (etc.)
  9     Button L        (etc.)
  10-13 Not used
  14    IRQ Enable Flag (0=Disable, 1=Enable)
  15    IRQ Condition   (0=Logical OR, 1=Logical AND)

In logical OR mode, an interrupt is requested when at least one of the selected buttons is pressed.
In logical AND mode, an interrupt is requested when ALL of the selected buttons are pressed.*/

input::input(interrupt* Interrupt)
{
    this->Interrupt = Interrupt;
    keyState = 0x3FF; // All buttons released
	keyInterruptState = 0; // Key interrupts disabled
}

void input::keyChanged(SDL_Keycode key, bool value)
{
	switch (key)
	{
        case SDLK_q:        setStateBit(9, value); break;
        case SDLK_w:        setStateBit(8, value); break;
        case SDLK_DOWN:     setStateBit(7, value); break;
        case SDLK_UP:       setStateBit(6, value); break;
        case SDLK_LEFT:     setStateBit(5, value); break;
        case SDLK_RIGHT:    setStateBit(4, value); break;
        case SDLK_RETURN:   setStateBit(3, value); break;
        case SDLK_RSHIFT:   setStateBit(2, value); break;
        case SDLK_a:        setStateBit(1, value); break;
        case SDLK_s:        setStateBit(0, value); break;
	}
    if (keyInterruptState & 0x4000) // Are key IRQs enabled?
    {
        if (keyInterruptState & 0x8000)
        {
            // Logical AND: interrupt when all selected buttons are pressed
            uint16_t buttonInterruptsEnabled = keyInterruptState & 0x3FF;
            if ((buttonInterruptsEnabled & (~keyState)) == buttonInterruptsEnabled)
            {
                Interrupt->requestInterrupt(interruptType::Keypad);
            }
        }
        else
        {
            // Logical OR: interrupt when at least one selected button is pressed
            if (keyInterruptState & (~keyState))
            {
                Interrupt->requestInterrupt(interruptType::Keypad);
            }
        }
    }
}

void input::setStateBit(uint8_t index, bool value)
{
    keyState &= ~(1 << index);
    keyState |= ((uint16_t)value) << index;
}

uint8_t input::getRegister(uint32_t addr)
{
    switch (addr)
    {
        case 0x4000130: // KEYINPUT byte 1
            return keyState & 0xFF;
        case 0x4000131: // KEYINPUT byte 2
            return keyState >> 8;
        case 0x4000132: // KEYCNT byte 1
            return keyInterruptState & 0xFF;
        case 0x4000133: // KEYCNT byte 2
            return keyInterruptState >> 8;
        default:
            logging::error("this should never happen: invalid getRegister in input", "input");
            return 0;
    }
}

void input::setRegister(uint32_t addr, uint8_t value)
{
    switch (addr)
    {
        case 0x4000130: case 0x4000131: // KEYINPUT
            logging::warning("Tried to write KEYINPUT", "input");
            break;
        case 0x4000132: // KEYCNT byte 1
            keyInterruptState &= 0xFF00;
            keyInterruptState |= value;
            break;
        case 0x4000133: // KEYCNT byte 2
            keyInterruptState &= 0x3CFF;
            keyInterruptState |= ((uint16_t)value << 8) & 0xC300;
            break;
        default:
            logging::error("this should never happen: invalid setRegister in input", "input");
            break;
    }
}
