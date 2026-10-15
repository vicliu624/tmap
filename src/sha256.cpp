#include "tmap/sha256.h"
#include <algorithm>
#include <cstring>
namespace tmap
{
namespace
{
constexpr uint32_t constants[64]{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
} // namespace
void Sha256::reset()
{
    constexpr uint32_t initial[8]{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (size_t i = 0; i < 8; ++i) state_[i] = initial[i];
    bytes_ = used_ = 0;
    finished_ = false;
}
void Sha256::block()
{
    for (size_t i = 0; i < 16; ++i)
        schedule_[i] = (uint32_t(buffer_[i * 4]) << 24) | (uint32_t(buffer_[i * 4 + 1]) << 16) | (uint32_t(buffer_[i * 4 + 2]) << 8) | buffer_[i * 4 + 3];
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (size_t i = 0; i < 64; ++i)
    {
        if (i >= 16)
        {
            const auto x = schedule_[(i - 15) & 15], y = schedule_[(i - 2) & 15];
            schedule_[i & 15] += (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + schedule_[(i - 7) & 15] + (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
        }
        const auto t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + constants[i] + schedule_[i & 15];
        const auto t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}
Result Sha256::update(const uint8_t* data, size_t count)
{
    if (finished_ || (!data && count)) return Result::Invalid;
    if (count > UINT64_MAX / 8 - bytes_) return Result::BudgetExceeded;
    bytes_ += count;
    while (count)
    {
        const auto amount = std::min<size_t>(count, 64 - used_);
        std::memcpy(buffer_.data() + used_, data, amount);
        used_ += uint8_t(amount);
        data += amount;
        count -= amount;
        if (used_ == 64)
        {
            block();
            used_ = 0;
        }
    }
    return Result::Ok;
}
Result Sha256::finish(std::array<uint8_t, 32>& digest)
{
    if (!finished_)
    {
        buffer_[used_++] = 0x80;
        if (used_ > 56)
        {
            while (used_ < 64) buffer_[used_++] = 0;
            block();
            used_ = 0;
        }
        while (used_ < 56) buffer_[used_++] = 0;
        const uint64_t bits = bytes_ * 8;
        for (size_t i = 0; i < 8; ++i) buffer_[63 - i] = uint8_t(bits >> (i * 8));
        block();
        finished_ = true;
    }
    for (size_t i = 0; i < 32; ++i) digest[i] = uint8_t(state_[i / 4] >> (24 - (i % 4) * 8));
    return Result::Ok;
}
} // namespace tmap
