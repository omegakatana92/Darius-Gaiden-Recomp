// Darius Gaiden - RetroFront game adapters (Original + Extra Edition)
// Connects the statically recompiled 68EC020 programs and the reimplemented Taito F3 board (src/*.c)
// to the RetroFront frontend. One adapter class, parameterised by edition; each edition is a separate
// profile with a stable id. The C core is a single machine instance: only the active edition's adapter
// ever loads content into it, and loading an edition replaces the previous machine completely.
#include "rf/frontend.h"
#include "core.h"   // C core: has its own extern "C" guard
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <SDL.h>   // SDL_main on Windows

extern "C" const char darius_gaiden_original_profile_json[];   // generated from profiles/*.json
extern "C" const char darius_gaiden_extra_profile_json[];

namespace {

// Adapter-level save-state header. The core blob carries its own magic, format version, ROM fingerprint
// and CRC32; this header adds the edition and the runtime build compatibility number.
constexpr uint32_t kStateMagic = 0x44475331u;      // "DGS1"
constexpr uint32_t kStateVersion = 1;              // layout of this header
constexpr uint32_t kRuntimeCompat = 1;             // bump when the core's machine model changes incompatibly
constexpr size_t kHdr = 48;

// Verified per-edition data (see docs/VERIFICATION.md): the 1P score is a 32-bit big-endian value in
// work RAM. Found by matching RAM against the on-screen score at three different frames per edition.
struct EditionData { const char *id, *title, *short_label, *nv_name, *zip; uint32_t score_addr; const char *year; };
const EditionData kEd[2] = {
    {"darius_gaiden_original", "Darius Gaiden", "Original", "dariusg.nv", "dariusg.zip", 0x41b1d8, "1994"},
    {"darius_gaiden_extra", "Darius Gaiden", "Extra Edition", "dariusgx.nv", "dariusgx.zip", 0x41bba0, "1995"},
};

class DariusAdapter : public rf::GameAdapter {
public:
    explicit DariusAdapter(edition_t e) : ed(e), d(kEd[e == ED_EXTRA ? 1 : 0]) { build_actions(); }

    rf::GameInfo info() const override {
        rf::GameInfo g;
        g.id = d.id; g.title = d.title; g.platform = "Taito F3 System";
        g.year = d.year; g.developer = "Taito";
        g.refresh_hz = 26686000.0 / 4.0 / 432.0 / 262.0;   // 58.97 Hz, the board's video timing
        g.audio_rate = AUDIO_RATE;
        g.rotation = rf::Rotation::None;                    // horizontal cabinet (MAME ROT0)
        return g;
    }
    rf::Capabilities capabilities() const override {
        rf::Capabilities c;
        c.can_pause = true;          // the core advances only when run_frame is called
        c.can_reset = true;
        c.save_states = true;        // explicit, versioned full-machine snapshot taken between frames
        c.native_save = true;        // 93C46 EEPROM (high scores + operator settings), one file per edition
        c.attract_preview = true;
        c.fast_forward = true;
        c.gadgets = {{"score", "1P Score"}};   // the only game value verified for both editions so far
        return c;
    }
    rf::ContentInfo content() const override {
        rf::ContentInfo ci;
        if (ed == ED_ORIGINAL) {
            ci.description = "MAME romset 'dariusg' (Darius Gaiden - Silver Hawk, Ver 2.5O 1994/09/19), as a .zip or a folder";
            ci.file_names = {"dariusg.zip"};
        } else {
            ci.description = "MAME romset 'dariusgx' (Darius Gaiden - Silver Hawk Extra Version, Ver 2.7J 1995/03/06), as a .zip or a folder. "
                             "A split set also needs dariusg.zip beside it.";
            ci.file_names = {"dariusgx.zip", "dariusg.zip"};   // a merged dariusg.zip also holds the Extra files; CRCs decide
        }
        ci.extensions = {".zip"};
        return ci;
    }
    const std::vector<rf::InputAction> &input_actions() const override { return acts; }

    bool verify_content(const std::string &path, std::string &error) override {
        if (!core_verify_romset(ed, path.c_str())) { error = core_last_error(); return false; }
        return true;
    }
    bool load_content(const std::string &path, std::string &error) override {
        // on failure the core is untouched, so a previously loaded set of this edition keeps working
        if (!core_load_romset(ed, path.c_str())) { error = core_last_error(); return false; }
        loaded = core_edition() == ed;
        return loaded;
    }
    void reset() override {
        if (!active()) return;
        core_reset();
        coin_pulse[0] = coin_pulse[1] = 0; prev_coin[0] = prev_coin[1] = false; test_hold = service_hold = 0;
    }
    void run_frame(const rf::InputState &in) override {
        if (!active()) return;
        uint16_t p[2] = {0, 0};
        for (size_t i = 0; i < acts.size() && i < in.pressed.size(); i++) if (in.pressed[i]) p[acts[i].player] |= bits[i];
        // one short coin pulse per press, so holding the key inserts one coin
        for (int pl = 0; pl < 2; pl++) {
            bool c = (p[pl] & IN_COIN) != 0;
            if (c && !prev_coin[pl]) coin_pulse[pl] = 6;
            prev_coin[pl] = c;
            p[pl] &= ~IN_COIN;
            if (coin_pulse[pl] > 0) { coin_pulse[pl]--; p[pl] |= IN_COIN; }
        }
        if (test_hold > 0) { test_hold--; p[0] |= IN_TEST; }
        if (service_hold > 0) { service_hold--; p[0] |= IN_SERVICE; }
        g_input[0] = p[0]; g_input[1] = p[1];
        core_run_frame(CPU_MODE_RECOMP);
    }
    rf::VideoFrame video() override {
        rf::VideoFrame f;
        f.pixels = core_framebuffer(); f.width = SCREEN_W; f.height = SCREEN_H; f.pitch = SCREEN_W * 4;
        f.display_aspect = 4.0 / 3.0;
        return f;
    }
    size_t read_audio(int16_t *out, size_t max_frames) override { return active() ? (size_t)core_audio_samples(out, (int)max_frames) : 0; }

    bool save_state(std::vector<uint8_t> &out) override {
        if (!active()) return false;
        uint8_t *b = nullptr; size_t n = core_state_save(&b);
        if (!n) return false;
        out.assign(kHdr + n, 0);
        uint32_t hdr[6] = {kStateMagic, kStateVersion, (uint32_t)ed, core_rom_fingerprint(), kRuntimeCompat, (uint32_t)n};
        memcpy(&out[0], hdr, sizeof hdr);
        out[24] = (uint8_t)coin_pulse[0]; out[25] = (uint8_t)coin_pulse[1]; out[26] = prev_coin[0]; out[27] = prev_coin[1];
        out[28] = (uint8_t)test_hold; out[29] = (uint8_t)service_hold;
        memcpy(&out[kHdr], b, n);
        free(b);
        return true;
    }
    bool load_state(const uint8_t *p, size_t n) override {
        last_reject.clear();
        if (!active()) { last_reject = "No game is running."; return false; }
        if (n < kHdr) { last_reject = "The save is truncated."; return false; }
        uint32_t hdr[6]; memcpy(hdr, p, sizeof hdr);
        if (hdr[0] != kStateMagic) { last_reject = "Not a Darius Gaiden save state."; return false; }
        if (hdr[1] != kStateVersion) { last_reject = "Save-state format version not supported by this build."; return false; }
        if (hdr[2] != (uint32_t)ed) {
            last_reject = std::string("This save belongs to Darius Gaiden - ") + (hdr[2] == ED_EXTRA ? "Extra Edition" : "Original") + ", not the selected edition.";
            return false;
        }
        if (hdr[3] != core_rom_fingerprint()) { last_reject = "The save was made with different ROM files."; return false; }
        if (hdr[4] != kRuntimeCompat) { last_reject = "The save was made by an incompatible build of the runtime."; return false; }
        if (hdr[5] != n - kHdr) { last_reject = "The save is truncated or damaged."; return false; }
        if (!core_state_load(p + kHdr, n - kHdr)) { last_reject = "The save is damaged (checksum or layout mismatch)."; return false; }   // atomic: nothing changed on failure
        coin_pulse[0] = p[24]; coin_pulse[1] = p[25]; prev_coin[0] = p[26]; prev_coin[1] = p[27];
        test_hold = p[28]; service_hold = p[29];
        return true;
    }
    std::string state_error() const override { return last_reject; }

    void load_native_save(const std::string &dir) override {
        if (active()) core_eeprom_load((dir + d.nv_name).c_str());   // missing file = blank EEPROM, as a new board
    }
    void flush_native_save(const std::string &dir) override { if (active()) core_eeprom_save((dir + d.nv_name).c_str()); }

    bool gadget(const std::string &id, rf::GadgetValue &v) override {
        v.valid = false;
        if (!active() || id != "score") return false;
        uint32_t a = d.score_addr - 0x400000;
        uint32_t s = ((uint32_t)g_ram[a] << 24) | (g_ram[a + 1] << 16) | (g_ram[a + 2] << 8) | g_ram[a + 3];
        if (s > 99999999u) return true;              // outside the 8-digit HUD range: not a score, hide
        v.value = (double)s; v.text = std::to_string(s); v.valid = true;
        return true;
    }

    std::vector<rf::ExtraAction> extras() override {
        return {
            {"service_coin", "Insert Service Credit", "Adds a credit through the service switch (not counted as a coin).", true, false},
            {"factory", "Reset High Scores & Game Settings", "Erases this edition's EEPROM. The game writes factory defaults and restarts itself, as a new board does.", false, true},
        };
    }
    bool run_extra(const std::string &id, std::string &msg) override {
        if (!active()) { msg = "Game not loaded"; return false; }
        if (id == "service_coin") { service_hold = 6; msg = "Service credit added"; return true; }
        if (id == "factory") { core_eeprom_factory(); reset(); msg = "High scores and settings reset"; return true; }
        return false;
    }
    void about(std::vector<std::pair<std::string, std::string>> &kv) override {
        char fp[16]; snprintf(fp, sizeof fp, "%08X", core_rom_fingerprint());
        kv.push_back({"Edition", std::string(d.short_label) + "  (MAME '" + core_edition_setname(ed) + "', " + core_edition_version(ed) + ")"});
        kv.push_back({"ROM fingerprint", fp});
        kv.push_back({"Program code", "Motorola 68EC020, statically recompiled to native code (separately for each edition)"});
        kv.push_back({"Translation", std::to_string(core_stats_native()) + " instructions native, " + std::to_string(core_stats_fallback()) + " via interpreter"});
        kv.push_back({"Video", "TC0630FDP: 4 playfields, sprites, pivot/text, line effects, 320x232 @ 58.97 Hz"});
        kv.push_back({"Sound", "68000 + Ensoniq ES5505 (32 voices), 44.1 kHz stereo; ES5510 reverb DSP not emulated"});
        kv.push_back({"Saving", "EEPROM high scores (per edition); exact save states"});
    }

private:
    edition_t ed;
    const EditionData &d;
    std::vector<rf::InputAction> acts;
    std::vector<uint16_t> bits;
    bool loaded = false;
    int coin_pulse[2] = {0, 0}; bool prev_coin[2] = {false, false};
    int test_hold = 0, service_hold = 0;
    std::string last_reject;

    // the core is shared by both editions: act only while it holds this edition's machine
    bool active() const { return loaded && core_edition() == ed; }

    void add(const std::string &id, const std::string &label, int pl, rf::ActionRole r, uint16_t bit, const char *sl = "") {
        rf::InputAction a; a.id = id; a.label = label; a.player = pl; a.role = r; a.short_label = sl;
        acts.push_back(a); bits.push_back(bit);
    }
    void build_actions() {
        using R = rf::ActionRole;
        // The program reads three buttons per player (verified in the input routine of both editions;
        // the board's fourth button line is never read). Button 3 toggles "[ PAUSE ]" in the Extra
        // Edition (observed); no effect was observed in the Original.
        const char *b3 = ed == ED_EXTRA ? "Button C (Pause)" : "Button C (no observed function)";
        for (int pl = 0; pl < 2; pl++) {
            std::string P = pl ? "p2." : "p1.";
            std::string n = pl ? "P2 " : "";
            auto id = [&](const char *s) { return P + s; };
            auto lab = [&](const char *s) { return n + s; };
            add(id("up"), lab("Up"), pl, R::Up, IN_UP);
            add(id("down"), lab("Down"), pl, R::Down, IN_DOWN);
            add(id("left"), lab("Left"), pl, R::Left, IN_LEFT);
            add(id("right"), lab("Right"), pl, R::Right, IN_RIGHT);
            add(id("a"), lab("Button A (Shot)"), pl, R::Button, IN_B1, "A");
            add(id("b"), lab("Button B (Black Hole Bomb)"), pl, R::Button, IN_B2, "B");
            add(id("c"), lab(b3), pl, R::Button, IN_B3, "C");
            add(id("start"), lab("Start"), pl, R::Start, IN_START);
            add(id("coin"), lab("Insert Coin"), pl, R::Coin, IN_COIN);
        }
        add("service", "Service Credit", 0, R::Service, IN_SERVICE);
    }
};
} // namespace

int main(int argc, char **argv) {
    rf::MultiOptions opt;
    opt.app_id = "darius_gaiden";
    opt.app_title = "Darius Gaiden";
    opt.version = "0.1.0";
    opt.editions.push_back({kEd[0].id, "Original", "Darius Gaiden - Original", "Ver 2.5O 1994/09/19 (MAME dariusg)",
                            darius_gaiden_original_profile_json, [] { return std::unique_ptr<rf::GameAdapter>(new DariusAdapter(ED_ORIGINAL)); }});
    opt.editions.push_back({kEd[1].id, "Extra Edition", "Darius Gaiden - Extra Edition", "Extra Version, Ver 2.7J 1995/03/06 (MAME dariusgx)",
                            darius_gaiden_extra_profile_json, [] { return std::unique_ptr<rf::GameAdapter>(new DariusAdapter(ED_EXTRA)); }});
    return rf::run_frontend_multi(opt, argc, argv);
}
