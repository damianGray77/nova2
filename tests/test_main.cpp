// Test entry point: `nova2_tests` runs every suite; `nova2_tests <suite>` runs
// one. Add a suite by writing test_<thing>.cpp, declaring and listing it
// below, adding the source to tests/CMakeLists.txt, and adding its name to
// NOVA2_SUITES there.
#include <cstdio>  // fprintf
#include <cstring> // strcmp

#include "check.h"
#include "nova2/config.h"

void suite_smoke();
void suite_containers();
void suite_str();
void suite_rng();
void suite_config();
void suite_keys();
void suite_memory();
void suite_cpu();
void suite_os();
void suite_sched();
void suite_region();
void suite_noise();
void suite_pool();
void suite_world();
void suite_observe();
void suite_power();
void suite_supply();
void suite_cutout();
void suite_fade();
void suite_fails();
void suite_boot();
void suite_fuzz();
void suite_determinism();
void suite_view();
void suite_taker();
void suite_spread();
void suite_map();
void suite_net();
void suite_send();
void suite_record();
void suite_sources();
void suite_solve();
void suite_heat();
void suite_lowv();
void suite_gate4();
void suite_gate5();
void suite_wear();
void suite_blocks();
void suite_visit();
void suite_links();
void suite_respawn();
void suite_events();
void suite_hammer();
void suite_flaky();
void suite_straits();
void suite_current();
void suite_guards();
void suite_watch();
void suite_determinism5();

namespace {

struct Suite {
    const char* name;
    void (*fn)();
};

const Suite kSuites[] = {
    {"smoke", suite_smoke},
    {"containers", suite_containers},
    {"str", suite_str},
    {"rng", suite_rng},
    {"config", suite_config},
    {"keys", suite_keys},
    {"memory", suite_memory},
    {"cpu", suite_cpu},
    {"os", suite_os},
    {"sched", suite_sched},
    {"region", suite_region},
    {"noise", suite_noise},
    {"pool", suite_pool},
    {"world", suite_world},
    {"observe", suite_observe},
    {"power", suite_power},
    {"supply", suite_supply},
    {"cutout", suite_cutout},
    {"fade", suite_fade},
    {"fails", suite_fails},
    {"boot", suite_boot},
    {"fuzz", suite_fuzz},
    {"determinism", suite_determinism},
    {"view", suite_view},
    {"taker", suite_taker},
    {"spread", suite_spread},
    {"map", suite_map},
    {"net", suite_net},
    {"send", suite_send},
    {"record", suite_record},
    {"sources", suite_sources},
    {"solve", suite_solve},
    {"heat", suite_heat},
    {"lowv", suite_lowv},
    {"gate4", suite_gate4},
    {"gate5", suite_gate5},
    {"wear", suite_wear},
    {"blocks", suite_blocks},
    {"visit", suite_visit},
    {"links", suite_links},
    {"respawn", suite_respawn},
    {"events", suite_events},
    {"hammer", suite_hammer},
    {"flaky", suite_flaky},
    {"straits", suite_straits},
    {"current", suite_current},
    {"guards", suite_guards},
    {"watch", suite_watch},
    {"determinism5", suite_determinism5},
};

} // namespace

// Gate 4 turned the sources on the map and heat on by default, and gate 5
// wear and the rest of its strengths: the suites from before each keep
// testing what they test (main, below).
const char* const ntest::kFallback =
    "supply.model = place\nheat.on = false\nwear.rate = 0\ndisk.block_endurance = 0\nvisit.mean = 0\n"
    "link.cuts = 0\nnet.lifetime = 0\nramp.lead.max = 0\npatch.storm.rate = 0\npatch.swell.rate = 0\n"
    "patch.brownout.rate = 0\npatch.heatwave.rate = 0\npatch.outage.rate = 0\nhammer.strength = 0\n"
    "flaky.share = 0\nflaky.worn = 1\nstrait.share = 0\npatch.gust.rate = 0\ncurrent.strength = 0\n"
    "os.killer = 0\nos.watchdog = 0\n";

int main(int argc, char** argv) {
    // Gate 4 turned the sources on the map and heat on by default. The
    // suites from before it keep testing what they test: a source at each
    // place, and no heat (Config::set_fallback). Gate 4's suites ask for
    // the sources and for heat by name.
    {
        nova2::Config::ParseError err;
        // Gate 5 likewise turned wear on by default: the suites from before
        // it run with nothing wearing, and gate 5's ask for it by name.
        if (!nova2::Config::set_fallback(ntest::kFallback, &err)) {
            fprintf(stderr, "the test defaults don't parse\n");
            return 2;
        }
    }
    if (argc > 1) {
        for (const Suite& s : kSuites) {
            if (strcmp(argv[1], s.name) == 0) {
                s.fn();
                return ntest::summary(s.name);
            }
        }
        fprintf(stderr, "unknown suite '%s'\n", argv[1]);
        return 2;
    }
    for (const Suite& s : kSuites)
        s.fn();
    return ntest::summary("all");
}
