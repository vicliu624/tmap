#include "tmap/native_file.h"
#include "tmap/terrain.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
using namespace tmap;
#define CHECK(x)                                                                    \
    do                                                                              \
    {                                                                               \
        if (!(x))                                                                   \
        {                                                                           \
            std::fprintf(stderr, "unified check failed at %d: %s\n", __LINE__, #x); \
            return 1;                                                               \
        }                                                                           \
    } while (false)
int main(int argc, char** argv)
{
    CHECK(argc == 3);
    NativeFile baseline_file, combined_file;
    CHECK(baseline_file.open(argv[1]) && combined_file.open(argv[2]));
    auto display_storage = std::make_unique<Workspace>();
    Reader display(*display_storage);
    CHECK(display.open(baseline_file) == Status::Ok);
    const auto count = display.poiCount();
    std::vector<std::vector<uint8_t>> pixels(3);
    std::vector<Status> statuses;
    for (uint32_t semantic = 1; semantic <= 3; ++semantic)
    {
        Tile tile;
        const auto result = display.lookupTile(semantic, 0, 0, 0, tile);
        statuses.push_back(result);
        if (result == Status::Ok)
        {
            pixels[semantic - 1].resize(tile.bytes);
            CHECK(display.readTile(tile, pixels[semantic - 1].data(), tile.bytes) == Status::Ok);
        }
    }
    CHECK(display.open(combined_file) == Status::Ok && display.poiCount() == count);
    for (uint32_t semantic = 1; semantic <= 3; ++semantic)
    {
        Tile tile;
        CHECK(display.lookupTile(semantic, 0, 0, 0, tile) == statuses[semantic - 1]);
        if (statuses[semantic - 1] == Status::Ok)
        {
            std::vector<uint8_t> actual(tile.bytes);
            CHECK(display.readTile(tile, actual.data(), actual.size()) == Status::Ok);
            CHECK(actual == pixels[semantic - 1]);
        }
    }
    auto outdoor_storage = std::make_unique<PackageWorkspace>();
    OutdoorPackage package(*outdoor_storage);
    Result result;
    do
    {
        result = package.open(combined_file, 1);
    } while (result == Result::More || result == Result::Busy);
    if (PackageWorkspace::kPageBytes < 4096)
    {
        CHECK(result == Result::BudgetExceeded);
        return 0;
    }
    CHECK(result == Result::Ok && (package.info().capabilities & Display));
    TerrainReader terrain(package);
    CHECK(terrain.initialize() == Result::Ok);
    const auto fine = terrain.info().resolution_m;
    ElevationCursor pending;
    CHECK(terrain.beginElevation({10000, 10000}, pending) == Result::Ok);
    CHECK(terrain.initializeForZoom(0) == Result::Ok);
    CHECK(terrain.info().resolution_m > fine);
    ElevationSample height;
    CHECK(terrain.elevationStep(pending, 1, height) == Result::Cancelled);
    CHECK(terrain.elevation({10000, 10000}, height) == Result::Ok);
    CHECK(terrain.initialize() == Result::Ok && terrain.info().resolution_m == fine);
    CHECK(terrain.elevation({10000, 10000}, height) == Result::Ok);
    ValidationCursor validation;
    CHECK(beginValidation(package, validation) == Result::Ok);
    do
    {
        result = validationStep(package, validation, 1);
    } while (result == Result::More);
    CHECK(result == Result::Ok);
    std::printf("unified display/workspace=%zu outdoor/workspace=%zu terrain=%zu poi=%llu\n",
                sizeof(Workspace), sizeof(PackageWorkspace), sizeof(TerrainReader), static_cast<unsigned long long>(count));
    return 0;
}
