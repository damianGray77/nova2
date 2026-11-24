#include "nova2/snapshot.h"

#include <cstdio>  // fopen, fwrite
#include <cstring> // memcmp, memcpy

#include "nova2/hash.h"

namespace nova2 {

namespace {

const char kMagic[8] = {'N', 'O', 'V', 'A', '2', 'S', 'N', 'P'};
constexpr u64 kChecksumDomain = 0x6E6F7661322D736Eull; // "nova2-sn"

} // namespace

void snapshot_begin(u64 seed, u64 tick, Str config_text, ByteWriter& out, u32 flags) {
    out.put_bytes(kMagic, sizeof kMagic);
    out.put_u32(kSnapshotVersion);
    out.put_u32(flags);
    out.put_u64(seed);
    out.put_u64(tick);
    out.put_str(config_text);
}

void snapshot_finish(ByteWriter& out) { out.put_u64(hash_bytes(out.data(), out.size(), kChecksumDomain)); }

SnapshotFile::~SnapshotFile() {
    if (f_)
        fclose(f_);
}

bool SnapshotFile::open(const char* path, usize chunk) {
    f_ = fopen(path, "wb");
    h_ = Hasher(kChecksumDomain);
    bytes_ = 0;
    ok_ = f_ != nullptr;
    out_.drain_to(drain, this, chunk < 8 ? 8 : chunk);
    return ok_;
}

// Whole words, in order: the checksum is hash_bytes of the whole snapshot,
// which takes it a word at a time and its odd bytes last.
void SnapshotFile::drain(void* ctx, const u8* p, usize n) {
    SnapshotFile& s = *static_cast<SnapshotFile*>(ctx);
    for (usize i = 0; i < n; i += 8) {
        u64 w;
        memcpy(&w, p + i, 8);
        s.h_.word(w);
    }
    s.ok_ = s.ok_ && s.f_ && fwrite(p, 1, n, s.f_) == n;
    s.bytes_ += n;
}

bool SnapshotFile::close() {
    if (!f_)
        return false;
    out_.spill();
    const usize odd = out_.size(); // 0 to 7 bytes
    h_.bytes(out_.data(), odd);
    ok_ = ok_ && fwrite(out_.data(), 1, odd, f_) == odd;
    const u64 sum = h_.finish();
    u8 b[8];
    for (int i = 0; i < 8; ++i)
        b[i] = static_cast<u8>(sum >> (8 * i));
    ok_ = ok_ && fwrite(b, 1, 8, f_) == 8;
    ok_ = fclose(f_) == 0 && ok_;
    f_ = nullptr;
    bytes_ += odd + 8;
    return ok_;
}

bool snapshot_parse(const u8* data, usize n, SnapshotHeader* h, TextBuf* err) {
    if (n < sizeof kMagic + 8 || memcmp(data, kMagic, sizeof kMagic) != 0) {
        err->append("snapshot: not a Nova 2 snapshot (bad magic)");
        return false;
    }
    ByteReader r(data, n - 8);
    u8 magic[8];
    u32 flags = 0;
    r.get_bytes(magic, sizeof magic);
    r.get_u32(&h->version);
    if (r.ok() && (h->version < kSnapshotOldest || h->version > kSnapshotVersion)) {
        err->append("snapshot: unsupported format version ");
        err->append_u64(h->version);
        err->append(" (this build reads versions ");
        err->append_u64(kSnapshotOldest);
        err->append(" to ");
        err->append_u64(kSnapshotVersion);
        err->append_char(')');
        return false;
    }
    r.get_u32(&flags);
    r.get_u64(&h->seed);
    r.get_u64(&h->tick);
    r.get_str(&h->config_text);
    if (!r.ok()) {
        err->append("snapshot: header truncated");
        return false;
    }
    ByteReader tail(data + n - 8, 8);
    u64 stored = 0;
    tail.get_u64(&stored);
    if (stored != hash_bytes(data, n - 8, kChecksumDomain)) {
        err->append("snapshot: checksum mismatch (the file is corrupt or truncated)");
        return false;
    }
    if (flags != 0 && flags != kSnapshotPlace) {
        err->append("snapshot: unknown flags");
        return false;
    }
    h->flags = flags;
    if (flags == kSnapshotPlace) {
        r.get_u32(&h->place);
        if (!r.ok()) {
            err->append("snapshot: the place section is truncated");
            return false;
        }
    }
    h->world_offset = r.pos();
    h->world_size = r.remaining();
    return true;
}

bool snapshot_config(const SnapshotHeader& h, Config* cfg, Config::ParseError* pe) {
    if (!cfg->parse(h.config_text, pe))
        return false;
    if (h.version < 4) {
        if (!cfg->has("supply.model"))
            cfg->set("supply.model", "place");
        if (!cfg->has("heat.on"))
            cfg->set("heat.on", "false");
        if (!cfg->has("power.low_voltage_margin"))
            cfg->set("power.low_voltage_margin", "0");
    }
    // Gate 4's builds (formats 3 and 4) had section 7's placeholders as
    // their defaults for the seven numbers Damian agreed on 2026-11-20
    // (build-plan-gate4.md 11, step 11), which this build's defaults are: a
    // save that left them to its build means the placeholders.
    if (h.version < 5) {
        static const char* const kPlaceholder[][2] = {{"heat.p.sink", "0.55"},     {"heat.p.capacity", "11"},
                                                      {"heat.e.sink", "0.07"},     {"heat.e.capacity", "1.4"},
                                                      {"sources.size.min", "50"},  {"sources.size.max", "400"},
                                                      {"grid.link_resistance", "5"}};
        for (usize i = 0; i < sizeof kPlaceholder / sizeof kPlaceholder[0]; ++i)
            if (!cfg->has(kPlaceholder[i][0]))
                cfg->set(kPlaceholder[i][0], kPlaceholder[i][1]);
    }
    if (h.version < 6)
        gate5_off(cfg, false);
    return true;
}

// Gate 5's strengths, each at its off (build-plan-gate5.md 3). The list
// grows with the steps that add them.
void gate5_off(Config* cfg, bool whatever) {
    static const char* const kOff[][2] = {{"wear.rate", "0"}, {"disk.block_endurance", "0"}, {"os.levelling", "false"},
                                          {"visit.mean", "0"},  {"link.cuts", "0"},
                                          {"net.lifetime", "0"}, {"ramp.lead.max", "0"},
                                          {"patch.storm.rate", "0"}, {"patch.swell.rate", "0"},
                                          {"patch.brownout.rate", "0"}, {"patch.heatwave.rate", "0"},
                                          {"patch.outage.rate", "0"}, {"hammer.strength", "0"},
                                          {"flaky.share", "0"},        {"flaky.worn", "1"},
                                          {"strait.share", "0"},        {"patch.gust.rate", "0"},
                                          {"current.strength", "0"}, {"os.killer", "0"}, {"os.watchdog", "0"}};
    for (usize i = 0; i < sizeof kOff / sizeof kOff[0]; ++i)
        if (whatever || !cfg->has(kOff[i][0]))
            cfg->set(kOff[i][0], kOff[i][1]);
}

} // namespace nova2
