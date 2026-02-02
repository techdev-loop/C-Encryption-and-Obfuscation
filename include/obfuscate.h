#pragma once

// Compile-time string obfuscation. Strings are XOR-encrypted and decrypted at runtime.
// Key varies per build when used with OBFUSCATE_KEY from CMake.
#ifndef OBFUSCATE_KEY
#define OBFUSCATE_KEY 0x5A
#endif

namespace catclicker {
namespace obf {

template<size_t N>
struct ObfString {
    char data[N];
    constexpr ObfString(const char (&str)[N], uint8_t k = OBFUSCATE_KEY) : data{} {
        for (size_t i = 0; i < N; i++)
            data[i] = str[i] ^ (k + (uint8_t)i);
    }
    std::string decrypt() const {
        std::string s;
        s.resize(N - 1);
        for (size_t i = 0; i < N - 1; i++)
            s[i] = data[i] ^ (OBFUSCATE_KEY + (uint8_t)i);
        return s;
    }
};

} // namespace obf
} // namespace catclicker

#define OBF(str) ([]() { \
    constexpr catclicker::obf::ObfString<sizeof(str)> _s(str); \
    return _s.decrypt(); \
}())

#define OBF_LIT(str, key) ([]() { \
    constexpr catclicker::obf::ObfString<sizeof(str)> _s(str, key); \
    return _s.decrypt(); \
}())
