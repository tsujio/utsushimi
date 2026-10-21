#include "touch/adaptive_contact.h"
#include <array>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
using namespace utsushimi;
int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "usage: touch-replay capture.log > comparison.csv\n"; return 2; }
    std::ifstream input(argv[1]);
    if (!input) { std::cerr << "cannot open input\n"; return 2; }
    std::array<ContactDetector,3> old{};
    std::array<AdaptiveContact,3> trial{};
    std::regex pattern(R"(touch_trace: t=(\d+) a=(\d+) b=(\d+) c=(\d+))");
    std::smatch m; std::string line;
    uint64_t last=0; unsigned rows=0, gaps=0, resets=0;
    std::cout << "time_ms,electrode,raw,old_baseline,trial_baseline,threshold,old_held,trial_held,fault\n";
    while (std::getline(input,line)) {
        if (!std::regex_search(line,m,pattern)) continue;
        uint64_t t=std::stoull(m[1]);
        if (rows && t<=last) { old={}; trial={}; ++resets; }
        else if (rows && t-last>150) ++gaps;
        last=t; ++rows;
        for (unsigned i=0;i<3;++i) {
            auto raw=static_cast<uint32_t>(std::stoul(m[i+2]));
            if (raw) { old[i].update(raw,t); trial[i].update(raw,t); }
            else { old[i].missing(); trial[i].missing(); }
            std::cout << t << ',' << "ABC"[i] << ',' << raw << ',' << old[i].baseline() << ','
                      << trial[i].baseline() << ',' << trial[i].threshold() << ',' << old[i].touching()
                      << ',' << trial[i].touching() << ',' << trial[i].fault() << '\n';
        }
    }
    std::cerr << "scans=" << rows << " gaps_over_150ms=" << gaps << " resets=" << resets
              << " (no ground-truth labels; accuracy not calculated)\n";
    return rows ? 0 : 1;
}
