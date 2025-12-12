// Minimal Print.h compatibility for ESP-IDF
// Provides Print class for Adafruit_GFX inheritance

#ifndef PRINT_H
#define PRINT_H

#include "Arduino.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>

class Print {
public:
    virtual size_t write(uint8_t) = 0;
    
    size_t print(const char* str) {
        if (str == nullptr) return 0;
        return write((const uint8_t*)str, strlen(str));
    }
    
    size_t print(int val, int base = 10) {
        char buf[33];
        if (base == 16) {
            snprintf(buf, sizeof(buf), "%x", val);
        } else if (base == 8) {
            snprintf(buf, sizeof(buf), "%o", val);
        } else if (base == 2) {
            // Binary conversion
            char* p = buf;
            uint32_t uval = (val < 0) ? -val : val;
            if (val < 0) *p++ = '-';
            if (uval == 0) {
                *p++ = '0';
            } else {
                char* start = p;
                while (uval) {
                    *p++ = (uval & 1) ? '1' : '0';
                    uval >>= 1;
                }
                *p = '\0';
                // Reverse the string
                char* end = p - 1;
                while (start < end) {
                    char tmp = *start;
                    *start = *end;
                    *end = tmp;
                    start++;
                    end--;
                }
            }
            *p = '\0';
        } else {
            snprintf(buf, sizeof(buf), "%d", val);
        }
        return print(buf);
    }
    
    size_t print(unsigned long val, int base = 10) {
        char buf[33];
        if (base == 16) {
            snprintf(buf, sizeof(buf), "%lx", val);
        } else if (base == 8) {
            snprintf(buf, sizeof(buf), "%lo", val);
        } else if (base == 2) {
            // Binary conversion
            char* p = buf;
            if (val == 0) {
                *p++ = '0';
            } else {
                char* start = p;
                while (val) {
                    *p++ = (val & 1) ? '1' : '0';
                    val >>= 1;
                }
                *p = '\0';
                // Reverse the string
                char* end = p - 1;
                while (start < end) {
                    char tmp = *start;
                    *start = *end;
                    *end = tmp;
                    start++;
                    end--;
                }
            }
            *p = '\0';
        } else {
            snprintf(buf, sizeof(buf), "%lu", val);
        }
        return print(buf);
    }
    
    size_t println() {
        size_t n = write('\r');
        n += write('\n');
        return n;
    }
    
    size_t println(const char* str) {
        size_t n = print(str);
        n += println();
        return n;
    }
    
    size_t println(int val, int base = 10) {
        size_t n = print(val, base);
        n += println();
        return n;
    }
    
    size_t println(unsigned long val, int base = 10) {
        size_t n = print(val, base);
        n += println();
        return n;
    }
    
    // F() macro compatibility
    size_t print(const __FlashStringHelper* str) {
        return print((const char*)str);
    }
    
    size_t println(const __FlashStringHelper* str) {
        return println((const char*)str);
    }
    
    // String compatibility (std::string)
    size_t print(const String& str) {
        return print(str.c_str());
    }
    
    size_t println(const String& str) {
        return println(str.c_str());
    }
    
protected:
    size_t write(const uint8_t* buffer, size_t size) {
        size_t count = 0;
        for (size_t i = 0; i < size; i++) {
            count += write(buffer[i]);
        }
        return count;
    }
};

#endif // PRINT_H

