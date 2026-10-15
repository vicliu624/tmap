#include "tmap/sha256.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace tmap;
#define CHECK(x)                                                \
    do                                                          \
    {                                                           \
        if (!(x))                                               \
        {                                                       \
            std::fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); \
            std::exit(1);                                       \
        }                                                       \
    } while (false)
void verify(const char* text, const char* expected)
{
    for (size_t chunk = 1; chunk < 70; ++chunk)
    {
        Sha256 hash;
        const size_t count = std::strlen(text);
        for (size_t i = 0; i < count; i += chunk) CHECK(hash.update(reinterpret_cast<const uint8_t*>(text + i), std::min(chunk, count - i)) == Result::Ok);
        std::array<uint8_t, 32> digest{}, again{};
        CHECK(hash.finish(digest) == Result::Ok && hash.finish(again) == Result::Ok && digest == again);
        CHECK(hash.update(nullptr, 0) == Result::Invalid);
        char hexadecimal[65];
        for (size_t i = 0; i < 32; ++i) std::sprintf(hexadecimal + i * 2, "%02x", unsigned(digest[i]));
        CHECK(std::strcmp(hexadecimal, expected) == 0);
    }
}
int main(int argc, char** argv)
{
    verify("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    verify("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    verify("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 hash;
    for (size_t i = 0; i < 1000000; ++i) CHECK(hash.update(reinterpret_cast<const uint8_t*>("a"), 1) == Result::Ok);
    std::array<uint8_t, 32> digest{};
    hash.finish(digest);
    char hexadecimal[65];
    for (size_t i = 0; i < 32; ++i) std::sprintf(hexadecimal + i * 2, "%02x", unsigned(digest[i]));
    CHECK(std::strcmp(hexadecimal, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    hash.reset();
    CHECK(hash.update(nullptr, 1) == Result::Invalid && hash.update(nullptr, 0) == Result::Ok);
    CHECK(argc == 2);
    auto source = std::fopen((std::string(argv[1]) + "/virtual-reference.tmap").c_str(), "rb");
    auto oracle = std::fopen((std::string(argv[1]) + "/virtual-reference.sha256").c_str(), "r");
    CHECK(source && oracle);
    uint8_t buffer[7];
    size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), source))) CHECK(hash.update(buffer, count) == Result::Ok);
    CHECK(!std::ferror(source));
    std::fclose(source);
    CHECK(hash.finish(digest) == Result::Ok);
    for (size_t i = 0; i < 32; ++i) std::sprintf(hexadecimal + i * 2, "%02x", unsigned(digest[i]));
    char expected[65];
    CHECK(std::fscanf(oracle, "%64s", expected) == 1 && !std::strcmp(hexadecimal, expected));
    std::fclose(oracle);
    std::printf("PASS SHA256 standard vectors, padding, 1..69 byte chunk splits, million byte stream, finish/reset; workspace=%zu\n", sizeof(Sha256));
}
