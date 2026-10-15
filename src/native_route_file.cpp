#include "tmap/native_route_file.h"
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
namespace tmap
{
namespace
{
bool seekRoute(std::FILE* file, uint64_t offset)
{
    if (offset > uint64_t(std::numeric_limits<int64_t>::max())) return false;
#if defined(_WIN32)
    return _fseeki64(file, static_cast<int64_t>(offset), SEEK_SET) == 0;
#else
    static_assert(sizeof(off_t) >= 8, "Route adapter requires 64-bit file offsets");
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}
} // namespace
bool NativeRouteFile::openNew(const char* path)
{
    close();
    if (!path) return false;
#if defined(_WIN32)
    const auto fd = _open(path, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return false;
    file_ = _fdopen(fd, "w+b");
    if (!file_) _close(fd);
#else
    const auto fd = ::open(path, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) return false;
    file_ = fdopen(fd, "w+b");
    if (!file_) ::close(fd);
#endif
    if (!file_) std::remove(path);
    return file_ != nullptr;
}
bool NativeRouteFile::flush() { return file_ && std::fflush(file_) == 0; }
void NativeRouteFile::close()
{
    if (file_) std::fclose(file_);
    file_ = nullptr;
    size_ = 0;
}
Status NativeRouteFile::readAt(uint64_t offset, uint8_t* output, size_t bytes)
{
    if (!file_ || (!output && bytes) || offset > size_ || bytes > size_ - offset || !seekRoute(file_, offset)) return Status::IoError;
    return std::fread(output, 1, bytes, file_) == bytes ? Status::Ok : Status::IoError;
}
Status NativeRouteFile::writeAt(uint64_t offset, const uint8_t* bytes, size_t count)
{
    if (!file_ || (!bytes && count) || count > UINT64_MAX - offset || !seekRoute(file_, offset)) return Status::IoError;
    if (std::fwrite(bytes, 1, count, file_) != count) return Status::IoError;
    if (offset + count > size_) size_ = offset + count;
    return Status::Ok;
}
} // namespace tmap
