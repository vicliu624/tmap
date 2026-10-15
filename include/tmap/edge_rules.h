#pragma once
#include "tmap/road_rules.h"
#include "tmap/section_file.h"
namespace tmap
{
// Optional caller-owned evaluator, fixed query time, one in-flight edge.
// Construct after package.open; discard/reconstruct after package close/reopen.
class EdgeRuleReader
{
  public:
    EdgeRuleReader(OutdoorPackage& package, uint64_t utc, int32_t local_offset);
    EdgeRuleReader(const EdgeRuleReader&) = delete;
    EdgeRuleReader& operator=(const EdgeRuleReader&) = delete;
    Result begin(uint32_t edge, uint8_t static_access, uint32_t max_records = 4096);
    Result step(size_t budget, uint8_t& access);
    void cancel();
    bool validFor(const OutdoorPackage& package) const
    {
        return &package == &package_ && package_.isOpen() && package_.generation() == generation_;
    }
    Result read(uint32_t edge, uint8_t static_access, uint8_t& access);
    bool validFor(const OutdoorPackage& package, uint64_t utc, int32_t offset) const
    {
        return validFor(package) && utc == utc_ && offset == offset_;
    }
    uint8_t* edgeBytes(uint32_t row, bool& loaded)
    {
        if (raw_edge_ != row)
        {
            raw_edge_ = row;
            raw_loaded_ = false;
            raw_completed_ = 0;
        }
        loaded = raw_loaded_;
        return raw_.data();
    }
    void markEdgeLoaded() { raw_loaded_ = true; }
    Result loadEdgeBytes(uint32_t row);

  private:
    OutdoorPackage& package_;
    PackageSectionFile records_;
    union Scratch
    {
        RoadProgramReadCursor descriptor;
        RoadRuleCursor rules;
        Scratch() : descriptor{} {}
        ~Scratch() {}
    } scratch_;
    std::array<RoadRuleRange, 7> programs_{};
    std::array<uint8_t, 8> reference_{};
    std::array<uint8_t, 48> raw_{};
    uint32_t raw_edge_ = UINT32_MAX;
    bool raw_loaded_ = false;
    uint8_t raw_completed_ = 0;
    uint64_t generation_ = 0, utc_ = 0;
    int32_t offset_ = 0;
    uint32_t edge_ = 0, maximum_ = 4096;
    uint8_t base_ = 0, highway_ = 0, access_ = 0, stage_ = 0;
    bool reversed_ = false;
    uint8_t barrier_index_ = 0;
    bool barrier_program_ = false, rules_active_ = false;
    Result terminal_ = Result::Invalid;
};
} // namespace tmap
