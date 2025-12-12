// Compatibility header for pgmspace.h (AVR program memory space)
// In ESP-IDF, all memory is in RAM, so these are just regular pointers

#ifndef PROGMEM
#define PROGMEM
#endif

#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))

typedef const void* PGM_P;
typedef const uint8_t* PGM_VOID_P;

