// kerfnest-cli: problem JSON in, result JSON out. Prints the solve time to stdout; the result
// file holds only the deterministic result, so two runs can be compared byte for byte.
//
//     kerfnest-cli <problem.json> <result.json>

#include <kerfnest/kerfnest.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using nlohmann::json;
using namespace kerfnest;

namespace {

Point ReadPoint(const json& j) { return {j.at(0).get<double>(), j.at(1).get<double>()}; }

Segment ReadSegment(const json& j)
{
    Segment s;
    const std::string type = j.at("type");
    s.type = type == "line" ? SegmentType::Line : type == "arc" ? SegmentType::Arc : type == "circle" ? SegmentType::Circle
                                                                                                       : throw std::invalid_argument("segment type '" + type + "'");
    s.start = ReadPoint(j.at("start"));
    s.end = ReadPoint(j.at("end"));
    if (s.type != SegmentType::Line) {
        s.center = ReadPoint(j.at("center"));
        s.clockwise = j.at("clockwise");
    }
    return s;
}

Problem ReadProblem(const json& j)
{
    Problem p;
    const json& s = j.at("settings");
    p.settings.spacingMm = s.at("spacingMm");
    p.settings.marginMm = s.at("marginMm");
    p.settings.anglesDeg = s.at("anglesDeg").get<std::vector<int>>();
    p.settings.seed = s.value("seed", 0ull);
    p.settings.workBudget = s.value("workBudget", 0ull);
    p.settings.threads = s.value("threads", 1);
    for (const json& t : j.at("sheetTypes"))
        p.sheetTypes.push_back({t.at("id"), t.at("width"), t.at("height")});
    for (const json& jp : j.at("parts")) {
        Part part;
        part.id = jp.at("id");
        part.quantity = jp.at("quantity");
        for (const json& jl : jp.at("loops")) {
            Loop loop;
            for (const json& js : jl)
                loop.push_back(ReadSegment(js));
            part.loops.push_back(std::move(loop));
        }
        p.parts.push_back(std::move(part));
    }
    return p;
}

const char* CodeName(const RefusalCode c)
{
    switch (c) {
    case RefusalCode::InvalidContour: return "invalid_contour";
    case RefusalCode::PartExceedsAllSheets: return "part_exceeds_all_sheets";
    case RefusalCode::NestIncomplete: return "nest_incomplete";
    }
    return "unknown";
}

json WriteResult(const Result& result)
{
    if (const Refusal* r = std::get_if<Refusal>(&result))
        return {{"refusal", {{"code", CodeName(r->code)}, {"message", r->message}}}};
    const Solution& s = std::get<Solution>(result);
    json placements = json::array();
    for (const Placement& p : s.placements)
        placements.push_back({{"partId", p.partId}, {"sheet", p.sheetIndex}, {"x", p.x}, {"y", p.y}, {"angleDeg", p.angleDeg}});
    return {{"sheetTypeIds", s.sheetTypeIds}, {"placements", placements}};
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: kerfnest-cli <problem.json> <result.json>\n";
        return 2;
    }
    const Problem problem = ReadProblem(json::parse(std::ifstream(argv[1])));
    const auto t0 = std::chrono::steady_clock::now();
    const Result result = Solve(problem);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::ofstream(argv[2]) << WriteResult(result).dump(1) << "\n";
    std::cout << "seconds " << seconds << "\n";
    return std::holds_alternative<Refusal>(result) ? 1 : 0;
}
