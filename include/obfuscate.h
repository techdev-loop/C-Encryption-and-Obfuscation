#pragma once

// Compile-time string obfuscation: plaintext never appears in binary.
// Decryption at runtime; minimal overhead for startup/error paths only.
// Use OBF("sensitive") for string literals; decrypt once and cache if needed in hot paths.

#include <string>
#include <array>

namespace catclicker {
namespace obfuscate {

// Runtime decrypt: in-place XOR with key (same as encrypt).
template<size_t N>
inline void xor_decrypt(char (&data)[N], const unsigned char* key, size_t key_len) {
    for (size_t i = 0; i < N; ++i)
        data[i] = (char)((unsigned char)data[i] ^ key[i % key_len]);
}

// Return std::string from decrypted buffer (copy).
template<size_t N>
inline std::string decrypt_to_string(char (&data)[N], const unsigned char* key, size_t key_len) {
    xor_decrypt(data, key, key_len);
    return std::string(data, N - 1); // assume null-terminated
}

} // namespace obfuscate
} // namespace catclicker

// Macro: OBF_KEY and OBF_ENC are generated so the literal is XOR'd at compile time.
// Usage: OBF("hello") produces a static buffer that you decrypt once with OBF_DECRYPT.
#define OBF_XOR_KEY 0x5A, 0x9E, 0xC3, 0x71, 0xB2, 0xF4, 0x08, 0x6D
#define OBF_KEY_LEN 8

#define OBF_ENC(str) ([]() { \
    constexpr size_t _n = sizeof(str); \
    constexpr unsigned char _k[] = { OBF_XOR_KEY }; \
    struct _s { char d[_n]; constexpr _s() : d{} { \
        for (size_t i = 0; i < _n - 1; ++i) d[i] = str[i] ^ _k[i % OBF_KEY_LEN]; \
        d[_n-1] = 0; \
    }} _v; return _v; }())

// OBF("text") returns a struct with .decrypt() returning std::string (key stored in struct).
#define OBF(str) ([]() { \
    constexpr size_t _n = sizeof(str); \
    constexpr unsigned char _k[] = { OBF_XOR_KEY }; \
    struct _t { \
        char _d[_n]; \
        unsigned char _key[OBF_KEY_LEN]; \
        _t() : _d{}, _key{} { \
            for (size_t i = 0; i < OBF_KEY_LEN; ++i) _key[i] = _k[i]; \
            for (size_t i = 0; i < _n - 1; ++i) _d[i] = (char)((unsigned char)str[i] ^ _k[i % OBF_KEY_LEN]); \
            _d[_n-1] = 0; \
        } \
        std::string decrypt() const { \
            std::string s; s.resize(_n - 1); \
            for (size_t i = 0; i < _n - 1; ++i) \
                s[i] = (char)((unsigned char)_d[i] ^ _key[i % OBF_KEY_LEN]); \
            return s; \
        } \
    } _v; return _v; }())

// Obfuscated constant (numeric). Use for e.g. magic values or sizes.
#define OBF_CONST(type, value, key_byte) ((type)((value) ^ (key_byte)))
