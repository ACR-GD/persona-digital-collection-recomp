// CI stand-in for the generated gen/ folder (which is derived from the game and never published).
// It lets the runtime compile and link on every platform without any game code.
#pragma once
#define IMAGE_BASE 0x400000
#define IMAGE_SIZE 0x136000
#define TEXT_LO 0x401000
#define TEXT_HI 0x401000
#define EXE_SIZE 1
#define EXE_CRC32 0u
