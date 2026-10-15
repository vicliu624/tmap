#pragma once
#include "tmap/reader.h"
#include <array>
#ifndef TMAP_CACHE_PAGE_BYTES
#define TMAP_CACHE_PAGE_BYTES 4096
#endif
#ifndef TMAP_PACKAGE_CACHE_PAGES
#define TMAP_PACKAGE_CACHE_PAGES 2
#endif
#ifndef TMAP_PACKAGE_SECTION_SLOTS
#define TMAP_PACKAGE_SECTION_SLOTS 32
#endif

namespace tmap
{
enum class Result : uint8_t
{
    Ok,
    Missing,
    Busy,
    More,
    Invalid,
    IoError,
    Cancelled,
    Unsupported,
    NoPath,
    BudgetExceeded,
    Incomplete,
    OutsideCoverage,
    Incompatible
};
const char* resultName(Result result);
Result convert(Status status);
enum class SectionType : uint32_t
{
    Terrain = 1000,
    DemIndex = 1001,
    DemData = 1002,
    ContourIndex = 1010,
    ContourData = 1011,
    ContourOverviewIndex = 1012,
    ContourOverviewData = 1013,
    TerrainLodPolicy = 1020,
    TerrainLodIndex = 1021,
    TerrainLodData = 1022,
    Graph = 1100,
    Nodes = 1101,
    Edges = 1102,
    Shapes = 1103,
    Restrictions = 1104,
    EdgeSpatial = 1105,
    EdgeRefs = 1106,
    RoadNames = 1107,
    EdgeTerrainIndex = 1108,
    EdgeTerrainData = 1109,
    EdgeTerrainPolicy = 1110,
    LandmarkPolicy = 1111,
    LandmarkDistances = 1112,
    RoutingProfile = 1113,
    RouteHistoryStates = 1114,
    RouteHistoryTransitions = 1115,
    RoadRuleReferences = 1116,
    RoadRulePrograms = 1117,
    RoadRuleRecords = 1118,
    BarrierRuleReferences = 1119,
    TurnRulePrograms = 1120,
    TurnRuleRecords = 1121,
    TurnRuleRelations = 1122,
    TurnRuleEdges = 1123,
    TurnRuleContextIndex = 1124,
    TurnRuleContextMatches = 1125,
    AdministrativeIndex = 1200,
    AdministrativeRecords = 1201,
    AdministrativeGeometry = 1202,
    AdministrativePaths = 1203
};
enum Capability : uint32_t
{
    Elevation = 1,
    Contours = 2,
    Routing = 4,
    Administration = 8,
    Display = 16
};
struct OutdoorSection
{
    SectionType type{};
    uint64_t offset = 0, length = 0, bytes = 0, count = 0;
};
struct OutdoorInfo
{
    std::array<uint8_t, 16> id{}, build{}, series{};
    Bounds bounds{};
    uint32_t capabilities = 0;
    uint64_t revision = 0;
};
// Own on heap/PSRAM. The package and all its consumers share one owner thread.
struct PackageWorkspace
{
    static constexpr size_t kPageBytes = TMAP_CACHE_PAGE_BYTES, kPayloadBytes = kPageBytes - 32,
                            kSections = TMAP_PACKAGE_SECTION_SLOTS, kCachePages = TMAP_PACKAGE_CACHE_PAGES;
    static_assert(kPageBytes == 1024 || kPageBytes == 2048 || kPageBytes == 4096, "Supported cache capacity");
    static_assert(kSections >= 4 && kSections <= 64 && kCachePages >= 2 && kCachePages <= 4, "Bounded storage configuration");
    struct Page
    {
        std::array<uint8_t, kPageBytes> bytes{};
        uint64_t index = 0;
        uint32_t type = 0, age = 0;
    };
    std::array<Page, kCachePages> pages{};
    std::array<OutdoorSection, kSections> sections{};
};
namespace detail
{
template <size_t PageBytes, size_t Pages, size_t Sections>
struct PackageStorageAbi
{
};
void packageStorageAbi(PackageStorageAbi<PackageWorkspace::kPageBytes, PackageWorkspace::kCachePages, PackageWorkspace::kSections>*);
} // namespace detail
class OutdoorPackage
{
  public:
    explicit OutdoorPackage(PackageWorkspace& workspace) : workspace_(workspace) { detail::packageStorageAbi(nullptr); }
    OutdoorPackage(const OutdoorPackage&) = delete;
    OutdoorPackage& operator=(const OutdoorPackage&) = delete;
    Result open(RandomAccessFile& file, size_t section_budget = 32);
    void close();
    bool isOpen() const { return file_ != nullptr; }
    const OutdoorInfo& info() const { return info_; }
    uint64_t generation() const { return generation_; }
    const OutdoorSection* section(SectionType type) const;
    const OutdoorSection* sectionAt(size_t index) const;
    Result read(SectionType type, uint64_t offset, uint8_t* output, size_t bytes);
    Result record(SectionType type, uint64_t row, size_t stride, uint8_t* output);
    uint64_t pageReads() const { return page_reads_; }
    uint32_t pageBytes() const { return page_bytes_; }
    uint32_t payloadBytes() const { return page_bytes_ ? page_bytes_ - 32 : 0; }

  private:
    PackageWorkspace& workspace_;
    RandomAccessFile* file_ = nullptr;
    RandomAccessFile* opening_file_ = nullptr;
    uint64_t opening_size_ = 0;
    uint32_t opening_count_ = 0, opening_crc_ = 0;
    uint32_t page_bytes_ = 0, opening_next_ = 0, opening_chunk_rows_ = 0, opening_chunk_next_ = 0, opening_running_crc_ = UINT32_MAX;
    uint64_t opening_previous_end_ = 0;
    bool header_loaded_ = false;
    OutdoorInfo info_{};
    size_t sections_ = 0;
    uint64_t generation_ = 0, page_reads_ = 0;
    uint64_t last_payload_page_ = 0;
    uint32_t clock_ = 0, last_payload_type_ = 0;
    bool dual_payload_ = false;
    Result page(const OutdoorSection& section, uint64_t index, const uint8_t*& payload, bool dual = false);
};
struct ValidationCursor
{
    uint64_t generation = 0, offset = 0;
    uint32_t section = 0;
    bool complete = true;
    bool cancelled = false;
};
struct ReadCursor
{
    uint64_t generation = 0, offset = 0;
    uint8_t* output = nullptr;
    size_t bytes = 0, completed = 0;
    SectionType type{};
    bool complete = true;
    Result terminal = Result::Invalid;
};
Result beginRead(OutdoorPackage& package, SectionType type, uint64_t offset, uint8_t* output, size_t bytes, ReadCursor& cursor);
Result readStep(OutdoorPackage& package, ReadCursor& cursor, size_t page_budget);
void cancelRead(ReadCursor& cursor);
Result beginValidation(OutdoorPackage& package, ValidationCursor& cursor);
void cancelValidation(ValidationCursor& cursor);
Result validationStep(OutdoorPackage& package, ValidationCursor& cursor, size_t page_budget);
static_assert(sizeof(PackageWorkspace) <= sizeof(Workspace), "Configured outdoor workspace cannot exceed legacy working storage");
} // namespace tmap
