#include "tmap/calendar_rules.h"
#include "tmap/road_permissions.h"
#include "tmap/road_rules.h"
#include "tmap/turn_permissions.h"
#include "tmap/turn_rules.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
struct File : RandomAccessFile
{
    std::array<uint8_t, 96> bytes{};
    uint64_t generation = 1;
    size_t calls = 0;
    bool replace_during_read = false;
    uint64_t size() const override { return bytes.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t count) override
    {
        if (++calls % 2) return Status::Busy;
        if (at > bytes.size() || count > bytes.size() - at) return Status::Invalid;
        std::memcpy(output, bytes.data() + at, count);
        if (replace_during_read) ++generation;
        return Status::Ok;
    }
};
uint64_t epoch(void* context) { return static_cast<File*>(context)->generation; }
void u32(uint8_t* p, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8 * i));
}
void record(File& f, size_t index, uint8_t days, uint32_t ordinal, uint32_t value)
{
    auto* p = f.bytes.data() + index * 24;
    p[8] = 255;
    p[9] = 15;
    p[12] = 160;
    p[13] = 5;
    p[14] = days;
    u32(p + 16, ordinal);
    u32(p + 20, value);
}
int main()
{
    uint8_t turn = 99;
    for (uint32_t general = 0; general <= 10; ++general)
        for (uint32_t specific = 0; specific <= 11; ++specific)
        {
            CHECK(resolveTurnProperty(general, specific, turn) == Result::Ok);
            CHECK(turn == (specific == 11 ? general : specific));
        }
    turn = 99;
    CHECK(resolveTurnProperty(11, 0, turn) == Result::Invalid && turn == 99);
    CHECK(resolveTurnProperty(0, 12, turn) == Result::Invalid && turn == 99);
    TurnDecisionCursor decision;
    bool allowed = false;
    size_t decision_cases = 0;
    for (uint8_t action = 0; action <= 10; ++action)
        for (uint8_t mode : {uint8_t(1), uint8_t(2)})
            for (bool complete : {false, true})
                for (uint32_t target : {5u, 6u})
                {
                    CHECK(beginTurnDecision(1, 5, decision) == Result::Ok);
                    CHECK(turnDecisionMatch(decision, 9, mode, 1, action, target, complete) == Result::More);
                    CHECK(finishTurnDecision(decision, allowed) == Result::Ok);
                    const bool expected = mode != 1 || !action || (action < 7 ? !complete || target != 5 : target == 5);
                    CHECK(allowed == expected);
                    ++decision_cases;
                }
    CHECK(decision_cases == 88);
    std::array<uint8_t, 24> relation_bytes{};
    u32(relation_bytes.data() + 8, 3);
    u32(relation_bytes.data() + 16, 9);
    TurnRelation relation;
    relation.group = 99;
    CHECK(decodeTurnRelation(relation_bytes.data(), relation_bytes.size(), 12, 1, relation) == Result::Ok);
    CHECK(relation.edges == 3 && relation.group == 9);
    relation.group = 99;
    CHECK(decodeTurnRelation(relation_bytes.data(), relation_bytes.size(), 11, 1, relation) == Result::Invalid && relation.group == 99);
    relation_bytes[0] = 1;
    CHECK(decodeTurnRelation(relation_bytes.data(), relation_bytes.size(), 16, 1, relation) == Result::Invalid && relation.group == 99);
    relation_bytes[0] = 0;
    CHECK(decodeTurnRelation(relation_bytes.data(), relation_bytes.size(), 12, 1, relation) == Result::Ok);
    std::array<uint8_t, 16> match_bytes{};
    u32(match_bytes.data() + 4, 2);
    u32(match_bytes.data() + 8, 5);
    u32(match_bytes.data() + 12, 1);
    TurnContextMatch match;
    CHECK(decodeTurnContextMatch(match_bytes.data(), match_bytes.size(), 1, 6, match) == Result::Ok);
    CHECK(validateTurnContextMatch(match, relation, 5) == Result::Ok);
    CHECK(validateTurnContextMatch(match, relation, 4) == Result::Invalid);
    match.complete = false;
    CHECK(validateTurnContextMatch(match, relation, 5) == Result::Invalid);
    match.next_edge = 99;
    CHECK(decodeTurnContextMatch(match_bytes.data(), match_bytes.size(), 1, 5, match) == Result::Invalid && match.next_edge == 99);
    u32(match_bytes.data() + 12, 2);
    CHECK(decodeTurnContextMatch(match_bytes.data(), match_bytes.size(), 1, 6, match) == Result::Invalid && match.next_edge == 99);
    CHECK(beginTurnDecision(1, 5, decision) == Result::Ok);
    CHECK(turnDecisionMatch(decision, 9, 1, 1, 7, 6, true) == Result::More);
    CHECK(turnDecisionMatch(decision, 9, 1, 1, 7, 5, true) == Result::More);
    CHECK(finishTurnDecision(decision, allowed) == Result::Ok && allowed);
    CHECK(beginTurnDecision(1, 5, decision) == Result::Ok);
    CHECK(turnDecisionMatch(decision, 9, 1, 1, 7, 5, false) == Result::More);
    CHECK(turnDecisionMatch(decision, 9, 1, 2, 7, 6, true) == Result::More);
    CHECK(finishTurnDecision(decision, allowed) == Result::Ok && !allowed);
    CHECK(beginTurnDecision(1, 5, decision) == Result::Ok);
    CHECK(turnDecisionMatch(decision, 9, 1, 1, 7, 5, true) == Result::More);
    CHECK(turnDecisionMatch(decision, 10, 1, 1, 7, 6, true) == Result::More);
    CHECK(finishTurnDecision(decision, allowed) == Result::Ok && !allowed);
    allowed = true;
    CHECK(beginTurnDecision(1, 5, decision) == Result::Ok);
    CHECK(turnDecisionMatch(decision, 9, 1, 2, 7, 5, true) == Result::More);
    CHECK(turnDecisionMatch(decision, 9, 1, 1, 7, 5, true) == Result::Invalid);
    CHECK(finishTurnDecision(decision, allowed) == Result::Invalid && allowed);
    CHECK(beginTurnDecision(1, 5, decision) == Result::Ok);
    CHECK(turnDecisionMatch(decision, 9, 1, 1, 7, 5, true) == Result::More);
    CHECK(turnDecisionMatch(decision, 9, 1, 2, 8, 5, true) == Result::Invalid);
    CHECK(beginTurnDecision(1, 5, decision) == Result::Ok);
    cancel(decision);
    CHECK(finishTurnDecision(decision, allowed) == Result::Cancelled && allowed);
    CHECK(beginTurnDecision(1, 5, decision) == Result::Ok);
    CHECK(finishTurnDecision(decision, allowed) == Result::Ok && allowed);
    std::printf("TURN_DECISION cursor=%zu cases=%zu\n", sizeof(decision), decision_cases);
    RoadPermissions permissions;
    RoadMasks masks{99, 99};
    permissions.access = RoadAccess::Denied;
    permissions.foot = RoadAccess::Allowed;
    CHECK(resolveRoadPermissions(permissions, 3, 3, masks) == Result::Ok && masks.forward == 1 && masks.backward == 1);
    permissions.bicycle = RoadAccess::Allowed;
    permissions.oneway = RoadDirection::Forward;
    CHECK(resolveRoadPermissions(permissions, 0, 3, masks) == Result::Ok && masks.forward == 3 && masks.backward == 1);
    permissions.bicycle_oneway = RoadDirection::Backward;
    permissions.foot_oneway = RoadDirection::Forward;
    CHECK(resolveRoadPermissions(permissions, 0, 3, masks) == Result::Ok && masks.forward == 1 && masks.backward == 2);
    CHECK(resolveRoadPermissions(permissions, 0, 1, masks) == Result::Ok && masks.forward == 1 && masks.backward == 0);
    masks = {99, 99};
    permissions.access = static_cast<RoadAccess>(3);
    CHECK(resolveRoadPermissions(permissions, 3, 3, masks) == Result::Invalid && masks.forward == 99 && masks.backward == 99);
    size_t permission_cases = 0;
    for (unsigned encoded = 0; encoded < 81 * 64; ++encoded)
    {
        unsigned remaining = encoded;
        permissions.access = static_cast<RoadAccess>(remaining % 3);
        remaining /= 3;
        permissions.vehicle = static_cast<RoadAccess>(remaining % 3);
        remaining /= 3;
        permissions.foot = static_cast<RoadAccess>(remaining % 3);
        remaining /= 3;
        permissions.bicycle = static_cast<RoadAccess>(remaining % 3);
        remaining /= 3;
        permissions.oneway = static_cast<RoadDirection>(remaining % 4);
        remaining /= 4;
        permissions.bicycle_oneway = static_cast<RoadDirection>(remaining % 4);
        remaining /= 4;
        permissions.foot_oneway = static_cast<RoadDirection>(remaining % 4);
        for (uint8_t highway = 0; highway < 4; ++highway)
            for (uint8_t requested = 1; requested < 4; ++requested)
            {
                uint8_t expected_forward = 0, expected_backward = 0;
                for (uint8_t bit = 1; bit <= 2; bit *= 2)
                {
                    if (!(bit & requested)) continue;
                    bool allowed = (highway & bit) != 0;
                    if (permissions.access == RoadAccess::Denied) allowed = false;
                    if (bit == 2 && permissions.vehicle == RoadAccess::Denied) allowed = false;
                    const auto override_access = bit == 1 ? permissions.foot : permissions.bicycle;
                    if (override_access != RoadAccess::Inherit) allowed = override_access == RoadAccess::Allowed;
                    auto direction = bit == 1 ? permissions.foot_oneway : permissions.bicycle_oneway;
                    if (bit == 2 && direction == RoadDirection::Inherit) direction = permissions.oneway;
                    if (allowed && direction != RoadDirection::Backward) expected_forward |= bit;
                    if (allowed && direction != RoadDirection::Forward) expected_backward |= bit;
                }
                CHECK(resolveRoadPermissions(permissions, highway, requested, masks) == Result::Ok);
                CHECK(masks.forward == expected_forward && masks.backward == expected_backward);
                ++permission_cases;
            }
    }
    CHECK(permission_cases == 62208);
    File file;
    record(file, 0, 127, 0, 1);
    record(file, 1, 8, 1, 2);
    record(file, 2, 16, 2, 3);
    record(file, 3, 8, 2, 3);
    CalendarRuleCursor cursor;
    CalendarRuleResult output{999, false};
    CHECK(beginCalendarRules(file, 0, 4, 0, 0, 7, cursor, 4, epoch, &file) == Result::Ok);
    Result result;
    size_t steps = 0;
    do
    {
        result = calendarRulesStep(cursor, 1, output);
        CHECK(++steps < 20);
        if (result == Result::Busy || result == Result::More) CHECK(output.value == 999 && !output.matched);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Ok && output.value == 3 && output.matched && file.calls == 8);
    CHECK(calendarRulesStep(cursor, 1, output) == Result::Ok && file.calls == 8);
    output = {999, false};
    CHECK(beginCalendarRules(file, 0, 4, 0, 0, 7, cursor, 4, epoch, &file) == Result::Ok);
    CHECK(calendarRulesStep(cursor, 1, output) == Result::Busy);
    ++file.generation;
    CHECK(calendarRulesStep(cursor, 1, output) == Result::Cancelled && output.value == 999);
    CHECK(beginCalendarRules(file, 0, 1, 0, 0, 7, cursor, 4, epoch, &file) == Result::Ok);
    file.replace_during_read = true;
    steps = 0;
    do
    {
        result = calendarRulesStep(cursor, 4, output);
        CHECK(++steps < 4);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Cancelled && output.value == 999);
    file.replace_during_read = false;
    CHECK(beginCalendarRules(file, 0, 4, 0, 0, 7, cursor) == Result::Ok);
    cancel(cursor);
    CHECK(calendarRulesStep(cursor, 1, output) == Result::Cancelled);
    CHECK(beginCalendarRules(file, 0, 5, 0, 0, 7, cursor, 4) == Result::BudgetExceeded);
    CHECK(beginCalendarRules(file, 0, 5, 0, 0, 7, cursor) == Result::Invalid);
    u32(file.bytes.data() + 3 * 24 + 16, 0);
    CHECK(beginCalendarRules(file, 0, 4, 0, 0, 7, cursor) == Result::Ok);
    steps = 0;
    do
    {
        result = calendarRulesStep(cursor, 1, output);
        CHECK(++steps < 20);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Invalid && output.value == 999 && !output.matched);
    CHECK(beginCalendarRules(file, 0, 0, 0, 0, 7, cursor) == Result::Ok);
    CHECK(calendarRulesStep(cursor, 1, output) == Result::Ok && output.value == 7 && !output.matched);
    std::array<RoadRuleRange, 7> programs{};
    std::array<uint8_t, 120> descriptor{};
    u32(descriptor.data(), 1);
    descriptor[5] = 3;
    descriptor[6] = 1;
    u32(descriptor.data() + 8 + 2 * 16 + 8, 1);
    u32(descriptor.data() + 8 + 6 * 16 + 12, 1);
    uint8_t highway = 99;
    bool reversed = false;
    CHECK(decodeRoadRuleProgram(descriptor.data(), descriptor.size(), 24, programs, highway, reversed) == Result::Ok);
    CHECK(highway == 3 && reversed && programs[2].count == 1 && programs[6].fallback == 1);
    highway = 99;
    programs[0].fallback = 99;
    descriptor[7] = 1;
    CHECK(decodeRoadRuleProgram(descriptor.data(), descriptor.size(), 24, programs, highway, reversed) == Result::Invalid);
    CHECK(highway == 99 && programs[0].fallback == 99);
    descriptor[7] = 0;
    CHECK(decodeRoadRuleProgram(descriptor.data(), descriptor.size(), 24, programs, highway, reversed, 0) == Result::BudgetExceeded);
    CHECK(decodeRoadRuleProgram(descriptor.data(), descriptor.size(), 23, programs, highway, reversed) == Result::Invalid);
    for (auto& p : programs) p = {};
    programs[0].fallback = 1;
    programs[2] = {0, 1, 0};
    programs[3].fallback = 2;
    programs[4].fallback = 2;
    programs[6].fallback = 1;
    record(file, 0, 8, 0, 2);
    RoadRuleCursor road;
    masks = {99, 99};
    CHECK(beginRoadRules(file, programs, 0, 0, 0, 3, road, 4, epoch, &file) == Result::Ok);
    steps = 0;
    do
    {
        result = roadRulesStep(road, 1, masks);
        CHECK(++steps < 20);
        if (result == Result::More || result == Result::Busy) CHECK(masks.forward == 99 && masks.backward == 99);
    } while (result == Result::More || result == Result::Busy);
    CHECK(result == Result::Ok && masks.forward == 3 && masks.backward == 1);
    CHECK(roadRulesStep(road, 1, masks) == Result::Ok);
    CHECK(beginRoadRules(file, programs, 0, 0, 0, 3, road, 4, epoch, &file) == Result::Ok);
    CHECK(roadRulesStep(road, 1, masks) == Result::More);
    ++file.generation;
    CHECK(roadRulesStep(road, 1, masks) == Result::Cancelled);
    CHECK(beginRoadRules(file, programs, 0, 0, 0, 3, road, 0) == Result::BudgetExceeded);
    record(file, 0, 16, 0, 99); // Invalid action must fail even when its predicate is inactive.
    CHECK(beginRoadRules(file, programs, 0, 0, 0, 3, road) == Result::Ok);
    do
    {
        result = roadRulesStep(road, 20, masks);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Invalid);
    CHECK(beginRoadRules(file, programs, 0, 0, 0, 3, road) == Result::Ok);
    cancel(road);
    CHECK(roadRulesStep(road, 1, masks) == Result::Cancelled);
    std::printf("ROAD cursor=%zu masks=%zu\n", sizeof(road), sizeof(masks));
    std::array<RoadRuleRange, 2> turn_programs{};
    std::array<uint8_t, 40> turn_descriptor{};
    u32(turn_descriptor.data(), 1);
    turn_descriptor[4] = 2;
    u32(turn_descriptor.data() + 8 + 12, 2);
    u32(turn_descriptor.data() + 24 + 8, 1);
    u32(turn_descriptor.data() + 24 + 12, 11);
    uint8_t turn_mode = 99;
    CHECK(decodeTurnRuleProgram(turn_descriptor.data(), turn_descriptor.size(), 24, turn_programs, turn_mode) == Result::Ok);
    CHECK(turn_mode == 2 && turn_programs[0].fallback == 2 && turn_programs[1].count == 1);
    turn_mode = 99;
    turn_programs[0].fallback = 99;
    turn_descriptor[4] = 1; // Foot cannot inherit a generic bicycle restriction.
    CHECK(decodeTurnRuleProgram(turn_descriptor.data(), turn_descriptor.size(), 24, turn_programs, turn_mode) == Result::Invalid);
    CHECK(turn_mode == 99 && turn_programs[0].fallback == 99);
    turn_descriptor[4] = 2;
    CHECK(decodeTurnRuleProgram(turn_descriptor.data(), turn_descriptor.size(), 23, turn_programs, turn_mode) == Result::Invalid);
    CHECK(decodeTurnRuleProgram(turn_descriptor.data(), turn_descriptor.size(), 24, turn_programs, turn_mode, 0) == Result::BudgetExceeded);
    turn_descriptor[5] = 1;
    CHECK(decodeTurnRuleProgram(turn_descriptor.data(), turn_descriptor.size(), 24, turn_programs, turn_mode) == Result::Invalid);
    turn_programs[0] = {};
    turn_programs[0].fallback = 2;
    turn_programs[1] = {0, 1, 11};
    record(file, 0, 8, 0, 0);
    TurnRuleCursor turn_cursor;
    for (auto utc : {uint64_t(0), uint64_t(86400)})
    {
        turn = 99;
        CHECK(beginTurnRules(file, turn_programs, utc, 0, turn_cursor, 1, epoch, &file) == Result::Ok);
        steps = 0;
        do
        {
            result = turnRulesStep(turn_cursor, 1, turn);
            CHECK(++steps < 20);
            if (result == Result::Busy || result == Result::More) CHECK(turn == 99);
        } while (result == Result::Busy || result == Result::More);
        CHECK(result == Result::Ok && turn == (utc == 0 ? 0 : 2));
    }
    turn = 99;
    record(file, 0, 8, 0, 11); // Inheritance is legal only as fallback, even if this action is inactive.
    CHECK(beginTurnRules(file, turn_programs, 86400, 0, turn_cursor) == Result::Ok);
    do
    {
        result = turnRulesStep(turn_cursor, 1, turn);
    } while (result == Result::Busy || result == Result::More);
    CHECK(result == Result::Invalid && turn == 99);
    CHECK(beginTurnRules(file, turn_programs, 0, 0, turn_cursor, 0) == Result::BudgetExceeded);
    CHECK(beginTurnRules(file, turn_programs, 0, 0, turn_cursor, 1, epoch, &file) == Result::Ok);
    CHECK(turnRulesStep(turn_cursor, 1, turn) == Result::More);
    ++file.generation;
    CHECK(turnRulesStep(turn_cursor, 1, turn) == Result::Cancelled && turn == 99);
    CHECK(beginTurnRules(file, turn_programs, 0, 0, turn_cursor) == Result::Ok);
    cancel(turn_cursor);
    CHECK(turnRulesStep(turn_cursor, 1, turn) == Result::Cancelled && turn == 99);
    std::printf("TURN cursor=%zu output=%zu\n", sizeof(turn_cursor), sizeof(turn));
    std::printf("PASS streaming clauses, Busy, unpublished output, last-match, OR, epoch, cancel, limits; cursor=%zu output=%zu permissions=%zu masks=%zu permission_cases=%zu\n", sizeof(cursor), sizeof(output), sizeof(permissions), sizeof(masks), permission_cases);
}
