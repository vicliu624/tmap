#pragma once

#include <cstddef>
#include <cstring>

namespace tmap
{
// Restrict a path to complete common segments. Comparing arbitrary byte
// prefixes could invent an administrative name or split a UTF-8 scalar.
inline void commonAdministrativePath(char* path, const char* other)
{
    if (!path || !other) return;
    size_t left = 0, right = 0, common_end = 0;
    while (path[left] && other[right])
    {
        while (path[left] == ' ') ++left;
        while (other[right] == ' ') ++right;
        size_t left_end = left, right_end = right;
        while (path[left_end] && path[left_end] != '/') ++left_end;
        while (other[right_end] && other[right_end] != '/') ++right_end;
        size_t a = left_end, b = right_end;
        while (a > left && path[a - 1] == ' ') --a;
        while (b > right && other[b - 1] == ' ') --b;
        if (a - left != b - right || std::memcmp(path + left, other + right, a - left)) break;
        common_end = a;
        if (!path[left_end] || !other[right_end]) break;
        left = left_end + 1;
        right = right_end + 1;
    }
    path[common_end] = 0;
}
} // namespace tmap
