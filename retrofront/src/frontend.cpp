// RetroFront - Frontend Core: state machine, main menu, in-game quick menu, settings pages, loop
#include "rf/frontend.h"
#include "rf_internal.h"
#include "gfx.h"
#include "display.h"
#include "io.h"
#include "saves.h"
#include "menu.h"
#include <cmath>
#include <algorithm>
#include <memory>
#include <cstdio>
#include <cstring>

namespace rf {

enum class Mode { NoContent, MainMenu, Playing, Paused, Arrange };

struct Toast { std::string text; double t0; bool error; };
struct Dialog {
    bool active = false;
    std::string title, text;
    std::vector<std::string> buttons;
    int sel = 0;
    std::function<void(int)> cb;
};

// frontend hotkey actions live after the adapter's actions in the binding table
enum FeAction { FE_QUICKSAVE, FE_QUICKLOAD, FE_SCREENSHOT, FE_FFWD, FE_REWIND, FE_COUNT };

class Frontend {
public:
    std::unique_ptr<GameAdapter> ad;
    FrontendOptions opt;
    // ---- editions (one entry for a single-game build). Only the active edition has an adapter.
    std::vector<EditionEntry> eds;
    int cur_ed = -1;
    std::string app_id, app_title, app_pref;
    bool multi = false, target_ok = true;
    struct EdStatus { int state = 0; std::string path, msg; };   // 0 unknown, 1 ready, 2 invalid, 3 not configured
    std::vector<EdStatus> ed_status;
    std::string accent = "theme";
    mutable Theme theme_cache;
    std::string ekey(const std::string &k) const { return "editions." + eds[cur_ed].id + "." + k; }
    std::string ekey(int i, const std::string &k) const { return "editions." + eds[i].id + "." + k; }
    const EditionEntry &edition() const { return eds[cur_ed]; }
    bool session_active() const { return mode == Mode::Playing || mode == Mode::Paused || mode == Mode::Arrange; }
    void activate_edition(int i);
    void switch_edition(int i, bool save_first);
    void request_switch(int i);
    std::vector<std::string> candidates(const ContentInfo &c, const json &profile, const std::string &saved, const std::string &explicit_path) const;
    void refresh_status(int i, bool search);
    std::string status_text(int i) const;
    void pick_content_for(int i);
    GameInfo gi; Capabilities caps; ContentInfo ci;
    std::vector<InputAction> actions;     // adapter actions + frontend hotkeys
    size_t n_game_actions = 0;
    Config cfg; ThemeManager themes; Gfx g; DisplayManager disp; AudioManager audio;
    InputManager input; SaveManager saves; GadgetManager gadgets; MenuStack menu;
    SDL_Window *win = nullptr; SDL_Renderer *ren = nullptr;
    Mode mode = Mode::NoContent;
    bool running = true, loaded = false;
    std::string pref, content_path, content_error, theme_id = "arcade_gold";
    InputState in_full, in_game;
    std::vector<uint8_t> fe_prev;
    uint64_t frame_no = 0;
    double playtime = 0, time_s = 0;
    // validated features (profile request AND adapter capability)
    bool f_states = false, f_rewind = false, f_gadgets = false, f_crt = true, f_attract = false, f_ff = true;
    // gameplay settings
    bool autosave = true, pause_on_focus = true, preview_audio = false, rewinding = false, ffwd = false;
    int ff_speed = 3;
    int arrange_idx = 0;
    double fps = 0, emu_ms = 0; int fps_frames = 0; uint64_t fps_t0 = 0; double emu_acc = 0;
    std::vector<Toast> toasts;
    Dialog dlg;
    uint32_t mouse_t = 0; bool mouse_visible = true;
    std::string capture_label; size_t capture_action = 0; bool capture_pad = false;

    int init(int argc, char **argv);
    void loop();
    void shutdown();

    // ---- content
    bool find_and_load(const std::string &explicit_path);
    bool load_content(const std::string &path);
    // ---- flow
    void go_main_menu();
    void pick_content() { pick_content_for(cur_ed); }
    void start_game();
    void open_quick_menu();
    void resume();
    void return_to_main();
    void quit();
    void toast(const std::string &s, bool err = false) { toasts.push_back({s, time_s, err}); if (toasts.size() > 4) toasts.erase(toasts.begin()); }
    void confirm(const std::string &title, const std::string &text, std::function<void()> yes, const std::string &yes_label = "Yes") {
        dlg = Dialog{true, title, text, {yes_label, "Cancel"}, 1, [yes](int b) { if (b == 0) yes(); }};
    }
    void message(const std::string &title, const std::string &text) { dlg = Dialog{true, title, text, {"OK"}, 0, nullptr}; }
    void ask(const std::string &title, const std::string &text, std::vector<std::string> buttons, int def, std::function<void(int)> cb) {
        dlg = Dialog{true, title, text, std::move(buttons), def, std::move(cb)};
    }
    // ---- settings
    void load_settings();
    void apply_display();
    void save_settings();
    const Theme &theme() const {
        theme_cache = themes.get(theme_id);
        static const struct { const char *id; uint32_t rgb; } acc[] = {{"cyan", 0x3fd8ff}, {"amber", 0xffb02e}, {"magenta", 0xff4fd8}, {"lime", 0x9cff4a}, {"red", 0xff4a5a}, {"white", 0xf2f2f2}};
        for (auto &a : acc) if (accent == a.id) theme_cache.accent = rgba(a.rgb);
        return theme_cache;
    }
    // ---- saves
    void do_save(const std::string &slot);
    void do_load(const std::string &slot);
    void screenshot();
    // ---- pages
    std::unique_ptr<Page> page_main();
    std::unique_ptr<Page> page_quick();
    std::unique_ptr<Page> page_nocontent();
    std::unique_ptr<Page> page_display();
    std::unique_ptr<Page> page_crt();
    std::unique_ptr<Page> page_audio();
    std::unique_ptr<Page> page_controls();
    std::unique_ptr<Page> page_bindings(int player, bool hotkeys);
    std::unique_ptr<Page> page_gameplay();
    std::unique_ptr<Page> page_gadgets();
    std::unique_ptr<Page> page_extras();
    std::unique_ptr<Page> page_about();
    std::unique_ptr<Page> page_text(const std::string &title, const std::string &text);
    std::unique_ptr<Page> page_slots(bool saving);
    std::unique_ptr<Page> page_browser(const std::string &dir, const std::vector<std::string> &exts, const std::string &title,
                                       std::function<void(const std::string &)> pick);
    std::unique_ptr<Page> page_controls_ref();
    std::unique_ptr<Page> page_editions();
    std::unique_ptr<Page> page_roms();
    // ---- drawing
    void draw();
    bool draw_no_present = false;
    void draw_backdrop(double dim);
    void draw_logo(double x, double y, double w);
    void draw_prompts(const std::vector<std::pair<std::string, std::string>> &items);
    void draw_dialog();
    void draw_toasts();
    void draw_capture();
    SDL_Rect preview_rect() const;
    // ---- frame
    void run_game_frame(bool with_input, bool audio_out);
    void handle_ui(const UiEvent &e);
    void handle_hotkeys();
};

// =====================================================================================================
int Frontend::init(int argc, char **argv) {
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, app_title.c_str(), SDL_GetError(), nullptr);
        return 1;
    }
    multi = eds.size() > 1;
    app_pref = platform::pref_dir(app_id);
    cfg.load_user(platform::join(app_pref, "settings.json"));   // shared by all editions
    themes.init(platform::join(platform::exe_dir(), "themes"));
    ed_status.assign(eds.size(), EdStatus());

    bool steam_deck = platform::is_steam_deck(), gamescope = platform::is_gamescope();
    bool force_window = false, want_fs = false;
    std::string explicit_path, want_ed;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--fullscreen") want_fs = true;
        else if (a == "--windowed") force_window = true;
        else if (a == "--edition" && i + 1 < argc) want_ed = argv[++i];
        else if (a.rfind("--edition=", 0) == 0) want_ed = a.substr(10);
        else if (a[0] != '-') explicit_path = a;
    }
    // edition: command line (id or label, case-insensitive prefix) > last used > first
    int start_ed = 0;
    std::string last = cfg.get_str("editions.last", "");
    for (size_t i = 0; i < eds.size(); i++) if (eds[i].id == last) start_ed = (int)i;
    if (!want_ed.empty()) {
        auto low = [](std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; };
        bool found = false;
        for (size_t i = 0; i < eds.size() && !found; i++)
            if (low(eds[i].id) == low(want_ed) || low(eds[i].label).rfind(low(want_ed), 0) == 0 || low(eds[i].id).find(low(want_ed)) != std::string::npos) { start_ed = (int)i; found = true; }
        if (!found) SDL_Log("unknown edition '%s' - using %s", want_ed.c_str(), eds[start_ed].id.c_str());
    }
    activate_edition(start_ed);
    if (want_fs) disp.set.fullscreen = true;
    if (force_window) disp.set.fullscreen = false;
    if ((steam_deck || gamescope) && !force_window && !cfg.has_user("display.fullscreen")) disp.set.fullscreen = true;

    int ww = 1280, wh = 800;
    SDL_Rect ub;
    if (SDL_GetDisplayUsableBounds(0, &ub) == 0) { ww = std::min(1280, ub.w - 40); wh = std::min(800, ub.h - 60); }
    win = SDL_CreateWindow(app_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (disp.set.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, app_title.c_str(), SDL_GetError(), nullptr); return 1; }
    SDL_SetWindowMinimumSize(win, 640, 400);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | (disp.set.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_TARGETTEXTURE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, app_title.c_str(), SDL_GetError(), nullptr); return 1; }
    SDL_RendererInfo ri; SDL_GetRendererInfo(ren, &ri);
    target_ok = (ri.flags & SDL_RENDERER_TARGETTEXTURE) != 0;
    if (!target_ok) { f_crt = false; disp.set.crt = false; if (disp.set.filter == "sharp") disp.set.filter = "nearest"; }
    g.init(ren); disp.init(ren);
    audio.open(gi.audio_rate);

    if (find_and_load(explicit_path)) go_main_menu();
    else { mode = Mode::NoContent; menu.push(page_nocontent()); }
    fps_t0 = SDL_GetPerformanceCounter();
    return 0;
}

// Build the adapter, profile, features, input table and save folder of one edition. Nothing is loaded.
void Frontend::activate_edition(int i) {
    cur_ed = i;
    ad = eds[i].make();
    gi = ad->info(); caps = ad->capabilities(); ci = ad->content();
    pref = multi ? platform::join(app_pref, eds[i].id) + "/" : app_pref;
    platform::make_dirs(pref);
    cfg.load_profile(gi.id, eds[i].profile_json, pref);
    saves.init(pref, gi.id);
    // feature validation: requested by the profile AND provided by the runtime
    f_states = caps.save_states && cfg.feature_requested("save_states", true);
    f_rewind = f_states && cfg.feature_requested("rewind", true);
    f_gadgets = cfg.feature_requested("gadgets", true);
    f_crt = target_ok && cfg.feature_requested("crt_shader", true);
    f_attract = caps.attract_preview && cfg.feature_requested("attract_preview", true);
    f_ff = caps.fast_forward && cfg.feature_requested("fast_forward", true);
    // input actions: game first, then frontend hotkeys
    actions = ad->input_actions();
    n_game_actions = actions.size();
    const char *fe_names[FE_COUNT][2] = {{"fe.quicksave", "Quick save"}, {"fe.quickload", "Quick load"}, {"fe.screenshot", "Screenshot"},
                                         {"fe.ffwd", "Fast-forward (hold)"}, {"fe.rewind", "Rewind (hold)"}};
    for (int a = 0; a < FE_COUNT; a++) { InputAction x; x.id = fe_names[a][0]; x.label = fe_names[a][1]; x.player = 0; actions.push_back(x); }
    input.init(actions);
    fe_prev.assign(FE_COUNT, 0);
    in_full.pressed.clear(); in_game.pressed.clear();
    loaded = false; content_error.clear();
    load_settings();
    if (multi) { cfg.set("editions.last", eds[i].id); cfg.save_user(); }
}

// Controlled runtime transition: end the current session, flush its saves, destroy its adapter, then
// build the selected edition from scratch. No memory, audio, input, timers or game data carry over.
void Frontend::switch_edition(int i, bool save_first) {
    if (i == cur_ed || i < 0 || i >= (int)eds.size()) return;
    if (loaded) {
        if (save_first && f_states && session_active()) do_save("auto");
        ad->flush_native_save(pref);
    }
    audio.clear();
    saves.rewind_clear();
    saves.free_textures();
    rewinding = ffwd = false;
    save_settings();
    menu.clear();
    dlg.active = false;
    ad.reset();                       // the old edition's adapter is gone before the new one exists
    disp.clear_frame();
    int old_rate = gi.audio_rate;
    activate_edition(i);
    if (gi.audio_rate != old_rate) { audio.close(); audio.open(gi.audio_rate); }
    playtime = 0; frame_no = 0;
    input.suppress_until_release();
    if (find_and_load("")) { go_main_menu(); toast(edition().long_label + " selected"); }
    else { mode = Mode::NoContent; menu.push(page_nocontent()); }
    refresh_status(i, false);
}

// Ask before ending a session; offer to save a state first when supported.
void Frontend::request_switch(int i) {
    if (i == cur_ed) { toast(edition().long_label + " is already selected"); return; }
    if (!session_active()) { switch_edition(i, false); return; }
    std::string txt = "End the current " + edition().label + " session and switch to " + eds[i].long_label + "?";
    if (f_states) ask("Switch Edition", txt + " Your progress can be saved to Continue first.", {"Save & Switch", "Switch", "Cancel"}, 0,
                      [this, i](int b) { if (b == 0) switch_edition(i, true); else if (b == 1) switch_edition(i, false); });
    else ask("Switch Edition", txt + " Unsaved progress will be lost.", {"Switch", "Cancel"}, 1, [this, i](int b) { if (b == 0) switch_edition(i, false); });
}

// =====================================================================================================
// settings
void Frontend::load_settings() {
    DisplaySettings &d = disp.set;
    d.orientation = cfg.get_str("display.orientation", d.orientation);
    d.aspect = cfg.get_str("display.aspect_mode", cfg.get_str("display.aspect", "original") == "4:3" ? "original" : d.aspect);
    d.integer_scale = cfg.get_bool("display.integer_scaling", d.integer_scale);
    d.filter = cfg.get_str("display.filter", d.filter);
    d.vsync = cfg.get_bool("display.vsync", d.vsync);
    d.fullscreen = cfg.get_bool("display.fullscreen", d.fullscreen);
    d.crt = f_crt && cfg.get_bool("display.crt", d.crt);
    d.scanlines = cfg.get_num("display.scanlines", d.scanlines);
    d.curvature = cfg.get_num("display.curvature", d.curvature);
    d.mask = cfg.get_str("display.mask", d.mask);
    d.mask_strength = cfg.get_num("display.mask_strength", d.mask_strength);
    d.glow = cfg.get_num("display.glow", d.glow);
    d.brightness = cfg.get_num("display.brightness", d.brightness);
    d.gamma = cfg.get_num("display.gamma", d.gamma);
    d.zoom = cfg.get_num("display.zoom", d.zoom);
    d.border = cfg.get_str("display.border", d.border);
    d.wallpaper = cfg.get_str("display.wallpaper", d.wallpaper);
    if (d.filter != "nearest" && d.filter != "linear" && d.filter != "sharp") d.filter = "nearest";
    audio.volume = clampi(cfg.get_int("audio.volume", 100), 0, 100);
    audio.mute = cfg.get_bool("audio.mute", false);
    audio.latency = clampi(cfg.get_int("audio.latency", 1), 0, 2);
    audio.device = cfg.get_str("audio.device", "");
    theme_id = cfg.get_str(ekey("theme"), cfg.get_str("theme", "arcade_gold"));   // per edition, default from its profile
    accent = cfg.get_str("ui.accent", "theme");
    autosave = cfg.get_bool("gameplay.autosave", true);
    pause_on_focus = cfg.get_bool("gameplay.pause_on_focus_loss", true);
    preview_audio = cfg.get_bool("gameplay.menu_preview_audio", false);
    ff_speed = clampi(cfg.get_int("gameplay.ff_speed", 3), 2, 8);
    saves.rewind_enabled = f_rewind && cfg.get_bool("gameplay.rewind", false);
    content_path = cfg.get_str(ekey("content_path"), "");
    // input: preset defaults, then user overrides
    input.defaults_from_profile(cfg.profile, cfg.get_str("input.preset", "default"));
    auto fe_default = [&](int a, SDL_Scancode k) {
        auto &b = input.bindings[n_game_actions + a];
        if (b.empty()) b.push_back({Binding::Key, k});
    };
    fe_default(FE_QUICKSAVE, SDL_SCANCODE_F5); fe_default(FE_QUICKLOAD, SDL_SCANCODE_F8); fe_default(FE_SCREENSHOT, SDL_SCANCODE_F12);
    fe_default(FE_FFWD, SDL_SCANCODE_TAB); fe_default(FE_REWIND, SDL_SCANCODE_BACKSPACE);
    if (cfg.user.contains("input")) input.load(cfg.user["input"]);
    gadgets.init(caps, cfg.profile);
    if (cfg.user.contains("gadgets")) gadgets.load(cfg.user["gadgets"]);
}
void Frontend::save_settings() {
    const DisplaySettings &d = disp.set;
    cfg.set("display.orientation", d.orientation); cfg.set("display.aspect_mode", d.aspect);
    cfg.set("display.integer_scaling", d.integer_scale); cfg.set("display.filter", d.filter);
    cfg.set("display.vsync", d.vsync); cfg.set("display.fullscreen", d.fullscreen); cfg.set("display.crt", d.crt);
    cfg.set("display.scanlines", d.scanlines); cfg.set("display.curvature", d.curvature); cfg.set("display.mask", d.mask);
    cfg.set("display.mask_strength", d.mask_strength); cfg.set("display.glow", d.glow);
    cfg.set("display.brightness", d.brightness); cfg.set("display.gamma", d.gamma); cfg.set("display.zoom", d.zoom);
    cfg.set("display.border", d.border); cfg.set("display.wallpaper", d.wallpaper);
    cfg.set("audio.volume", audio.volume); cfg.set("audio.mute", audio.mute); cfg.set("audio.latency", audio.latency); cfg.set("audio.device", audio.device);
    cfg.set(ekey("theme"), theme_id); cfg.set("ui.accent", accent);
    cfg.set("gameplay.autosave", autosave); cfg.set("gameplay.pause_on_focus_loss", pause_on_focus);
    cfg.set("gameplay.menu_preview_audio", preview_audio); cfg.set("gameplay.ff_speed", ff_speed);
    cfg.set("gameplay.rewind", saves.rewind_enabled);
    cfg.set("input", input.save());
    cfg.set("gadgets", gadgets.save());
    if (!content_path.empty()) cfg.set(ekey("content_path"), content_path);
    cfg.save_user();
}
void Frontend::apply_display() {
    SDL_SetWindowFullscreen(win, disp.set.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#if SDL_VERSION_ATLEAST(2, 0, 18)
    SDL_RenderSetVSync(ren, disp.set.vsync ? 1 : 0);
#endif
    disp.invalidate_luts();
}

// =====================================================================================================
// content
bool Frontend::load_content(const std::string &path) {
    std::string err;
    if (!ad->load_content(path, err)) { content_error = platform::filename(path) + ": " + err; return false; }
    loaded = true; content_path = path; content_error.clear();
    ad->load_native_save(pref);
    ad->reset();
    cfg.set(ekey("content_path"), path); cfg.save_user();
    ed_status[cur_ed] = EdStatus{1, path, "Ready"};
    return true;
}
std::vector<std::string> Frontend::candidates(const ContentInfo &c, const json &profile, const std::string &saved, const std::string &explicit_path) const {
    std::vector<std::string> cands;
    if (!explicit_path.empty()) cands.push_back(explicit_path);
    if (!saved.empty()) cands.push_back(saved);
    std::vector<std::string> dirs = {platform::exe_dir(), platform::join(platform::exe_dir(), "roms")};
    if (const char *ai = getenv("APPIMAGE")) { dirs.push_back(platform::parent(ai)); dirs.push_back(platform::join(platform::parent(ai), "roms")); }
    std::string h = platform::home_dir();
    for (auto rel : {"Downloads", "roms", "Emulation/roms/arcade", "Emulation/roms/mame", "Emulation/roms/fbneo", "Documents"})
        dirs.push_back(platform::join(h, rel));
    if (profile.contains("content") && profile["content"].contains("search_paths"))
        for (auto &p : profile["content"]["search_paths"]) if (p.is_string()) {
            std::string s_ = p.get<std::string>();
            if (s_.rfind("~/", 0) == 0) s_ = platform::join(h, s_.substr(2));
            dirs.push_back(s_);
        }
    for (auto &d : dirs) for (auto &n : c.file_names) {
        cands.push_back(platform::join(d, n));
        std::string stem = n.substr(0, n.find('.'));
        cands.push_back(platform::join(platform::join(d, stem), n));
        cands.push_back(platform::join(d, stem));          // unzipped folder named after the set
    }
    std::vector<std::string> out;
    auto ext_ok = [&c](const std::string &x) {
        std::string l = x; for (auto &ch : l) ch = (char)tolower((unsigned char)ch);
        for (auto &e : c.extensions) if (l.size() >= e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0) return true;
        return c.extensions.empty();
    };
    for (auto &x : cands) {
        bool ok = platform::is_dir(x) || (platform::file_exists(x) && (ext_ok(x) || x == explicit_path || x == saved));
        if (ok && std::find(out.begin(), out.end(), x) == out.end()) out.push_back(x);
    }
    return out;
}
// Errors are reported for the explicit/remembered path and for files named like this edition's primary
// set; secondary candidates (e.g. a parent set that may or may not be merged) fail silently.
static bool primary_candidate(const std::string &c, const ContentInfo &ci, const std::string &saved, const std::string &explicit_path) {
    if (c == saved || c == explicit_path || ci.file_names.empty()) return true;
    std::string f = platform::filename(c), n = ci.file_names[0];
    return f == n || f == n.substr(0, n.find('.'));
}
bool Frontend::find_and_load(const std::string &explicit_path) {
    std::string first_err, saved = content_path;
    for (auto &c : candidates(ci, cfg.profile, saved, explicit_path)) {
        if (load_content(c)) return true;
        if (first_err.empty() && primary_candidate(c, ci, saved, explicit_path)) first_err = content_error;
    }
    content_error = first_err;
    ed_status[cur_ed] = content_error.empty() ? EdStatus{3, "", "Not configured"} : EdStatus{2, content_path, content_error};
    return false;
}

// Status of any edition without disturbing the active one: the active edition reports its loaded state;
// others are verified with a temporary adapter (CRC checks only; nothing is loaded into the runtime).
void Frontend::refresh_status(int i, bool search) {
    EdStatus st;
    if (i == cur_ed && loaded) { ed_status[i] = EdStatus{1, content_path, "Ready"}; return; }
    std::string saved = i == cur_ed ? content_path : cfg.get_str(ekey(i, "content_path"), "");
    auto tmp = eds[i].make();
    json prof = json::parse(eds[i].profile_json, nullptr, false);
    if (prof.is_discarded()) prof = json::object();
    std::vector<std::string> list;
    if (search || saved.empty()) list = candidates(tmp->content(), prof, saved, "");   // nothing remembered: look in the usual places
    else if (!saved.empty()) list.push_back(saved);
    std::string first_err, first_path;
    for (auto &p : list) {
        std::string err;
        if (tmp->verify_content(p, err)) {
            st = EdStatus{1, p, "Ready"};
            if (p != saved) { cfg.set(ekey(i, "content_path"), p); cfg.save_user(); if (i == cur_ed) content_path = p; }
            ed_status[i] = st;
            return;
        }
        if (first_err.empty() && primary_candidate(p, tmp->content(), saved, "")) { first_err = err; first_path = p; }
    }
    if (!first_err.empty()) st = EdStatus{2, first_path, first_err};
    else st = EdStatus{3, "", "Not configured. Needs " + tmp->content().description + "."};
    ed_status[i] = st;
}
std::string Frontend::status_text(int i) const {
    switch (ed_status[i].state) {
    case 1: return "Ready";
    case 2: return "ROM problem";
    case 3: return "ROM needed";
    default: return "Not checked";
    }
}
void Frontend::pick_content_for(int i) {
    std::string saved = i == cur_ed ? content_path : cfg.get_str(ekey(i, "content_path"), "");
    std::string start = saved.empty() ? platform::join(platform::home_dir(), "Downloads") : platform::parent(saved);
    menu.push(page_browser(start, {".zip"}, "SELECT ROM SET - " + eds[i].long_label, [this, i](const std::string &f) {
        if (i == cur_ed) {
            if (loaded) ad->flush_native_save(pref);
            bool was_session = session_active();
            if (load_content(f)) {
                toast(eds[i].label + ": loaded " + platform::filename(f));
                if (was_session || mode == Mode::NoContent) go_main_menu();
                else if (!menu.empty()) menu.replace_top(page_roms());
            } else {
                message("Could not use this file", content_error + (loaded ? " The previous ROM set is still in use." : ""));
                ed_status[i] = EdStatus{2, f, content_error};
            }
            return;
        }
        auto tmp = eds[i].make();
        std::string err;
        if (tmp->verify_content(f, err)) {
            cfg.set(ekey(i, "content_path"), f); cfg.save_user();
            ed_status[i] = EdStatus{1, f, "Ready"};
            toast(eds[i].label + ": ROM set OK");
        } else {
            ed_status[i] = EdStatus{2, f, err};
            message("Could not use this file for " + eds[i].long_label, err);
        }
        if (!menu.empty()) menu.replace_top(page_roms());
    }));
}

// =====================================================================================================
// flow
void Frontend::go_main_menu() {
    mode = Mode::MainMenu;
    menu.clear();
    menu.push(page_main());
    if (loaded) ad->reset();
    saves.rewind_clear();
    audio.clear();
}
void Frontend::start_game() {
    if (!loaded) return;
    ad->reset();
    playtime = 0; frame_no = 0;
    saves.rewind_clear();
    audio.clear();
    menu.clear();
    input.suppress_until_release();
    mode = Mode::Playing;
}
void Frontend::open_quick_menu() {
    if (mode != Mode::Playing) return;
    if (!caps.can_pause) return;   // runtime cannot be paused safely: keep running, no overlay
    mode = Mode::Paused;
    ffwd = rewinding = false;
    audio.clear();
    ad->flush_native_save(pref);
    menu.clear();
    menu.push(page_quick());
}
void Frontend::resume() {
    menu.clear();
    input.suppress_until_release();
    mode = Mode::Playing;
}
void Frontend::return_to_main() {
    if (autosave && f_states && (mode == Mode::Playing || mode == Mode::Paused)) do_save("auto");
    ad->flush_native_save(pref);
    go_main_menu();
}
void Frontend::quit() {
    if (loaded && autosave && f_states && (mode == Mode::Playing || mode == Mode::Paused)) do_save("auto");
    running = false;
}

// =====================================================================================================
// saves
void Frontend::do_save(const std::string &slot) {
    if (!f_states || !disp.has_frame()) return;
    std::string err;
    if (saves.save(*ad, slot, disp.pixels(), disp.out_w(), disp.out_h(), playtime, err))
        toast(slot == "auto" ? "Progress saved" : slot == "quick" ? "Quick save" : "Saved to slot " + slot);
    else toast(err, true);
}
void Frontend::do_load(const std::string &slot) {
    if (!f_states) return;
    std::string err; double pt = 0;
    if (saves.load(*ad, slot, &pt, err)) {
        playtime = pt;
        saves.rewind_clear();
        audio.clear();
        run_game_frame(false, false);     // produce a picture of the restored state
        toast(slot == "auto" ? "Continuing" : slot == "quick" ? "Quick load" : "Loaded slot " + slot);
        if (mode != Mode::Playing) resume();
    } else toast(err, true);
}
void Frontend::screenshot() {
    if (!disp.has_frame()) return;
    std::string dir = platform::join(pref, "screenshots");
    platform::make_dirs(dir);
    std::string p = platform::join(dir, gi.id + "-" + platform::timestamp_compact() + ".png");
    if (write_png(p, disp.pixels().data(), disp.out_w(), disp.out_h(), disp.out_w())) toast("Screenshot saved");
    else toast("Could not save screenshot", true);
}

// =====================================================================================================
// pages
static std::string pct(double v) { return fmt("%d%%", (int)std::round(v * 100)); }

std::unique_ptr<Page> Frontend::page_main() {
    auto p = std::make_unique<Page>();
    p->title = "MAIN MENU";
    p->subtitle = multi ? "Edition: " + edition().long_label : std::string();
    std::string el = edition().label;
    {
        auto m = act("Start Game", "Boot " + edition().long_label + " from power-on, just like the arcade board.", [this] { start_game(); });
        if (multi) m.value = [el] { return el; };
        p->items.push_back(m);
    }
    if (multi) {
        auto m = act("Select Edition", "Choose which edition Start Game and Continue launch. Each edition keeps its own ROM set, saves and high scores.", [this] {
            for (size_t i = 0; i < eds.size(); i++) refresh_status((int)i, false);
            menu.push(page_editions());
        }, true);
        m.value = [el] { return el; };
        p->items.push_back(m);
    }
    {
        auto m = act("Continue", "Pick up the last " + el + " session (saved automatically when you leave).", [this] { do_load("auto"); });
        m.visible = [this] { return f_states && saves.exists("auto"); };
        if (multi) m.value = [el] { return el; };
        p->items.push_back(m);
    }
    {
        auto m = act("Load State", "Load a " + el + " save state from a slot.", [this] { menu.push(page_slots(false)); }, true);
        m.visible = [this] { return f_states; };
        if (multi) m.value = [el] { return el; };
        p->items.push_back(m);
    }
    p->items.push_back(act("Display Settings", "Screen size, filtering, CRT effects, colour and borders.", [this] { menu.push(page_display()); }, true));
    p->items.push_back(act("Audio Settings", "Volume, output device and latency.", [this] { menu.push(page_audio()); }, true));
    p->items.push_back(act("Controller Settings", "Button mapping, presets, dead zone and the menu shortcut.", [this] { menu.push(page_controls()); }, true));
    p->items.push_back(act("Gameplay Settings", "Autofire, rewind, autosave and game options.", [this] { menu.push(page_gameplay()); }, true));
    {
        auto m = act("Gadgets", "Score, input display, FPS and other on-screen panels: show, hide, move and resize.", [this] { menu.push(page_gadgets()); }, true);
        m.visible = [this] { return f_gadgets; };
        p->items.push_back(m);
    }
    p->items.push_back(act(multi ? "ROM Configuration" : "ROM Selection", multi ? std::string("Set and check the ROM set of every edition.") : "Choose which copy of " + (ci.file_names.empty() ? std::string("the game") : ci.file_names[0]) + " (or unzipped folder) to use. Current: " + (content_path.empty() ? std::string("none") : content_path),
        [this] { if (multi) { for (size_t i = 0; i < eds.size(); i++) refresh_status((int)i, false); menu.push(page_roms()); } else pick_content(); }, true));
    p->items.push_back(act("Extras", "Service menu, screenshots, data folders and more.", [this] { menu.push(page_extras()); }, true));
    p->items.push_back(act("About", "About this game and this release.", [this] { menu.push(page_about()); }, true));
    auto ex = act("Exit", "Quit to the desktop.", [this] { quit(); });
    p->items.push_back(ex);
    p->on_key = [this](const UiEvent &e) { if (e.key == UiKey::Back || e.key == UiKey::Menu) { confirm("Exit", "Quit " + app_title + "?", [this] { quit(); }, "Quit"); return true; } return false; };
    return p;
}
std::unique_ptr<Page> Frontend::page_editions() {
    auto p = std::make_unique<Page>();
    p->title = "SELECT EDITION";
    p->subtitle = "Current: " + edition().long_label;
    p->wide = true;
    for (size_t k = 0; k < eds.size(); k++) {
        int i = (int)k;
        std::string desc = eds[i].detail + ".  " + (ed_status[i].state == 1 ? "ROM set: " + ed_status[i].path : ed_status[i].msg);
        auto m = act(eds[i].long_label, desc, [this, i] {
            if (i == cur_ed) { menu.pop(); toast(eds[i].long_label + " is already selected"); return; }
            request_switch(i);
        });
        m.value = [this, i] { return (i == cur_ed ? std::string("SELECTED  -  ") : std::string()) + status_text(i); };
        p->items.push_back(m);
    }
    p->items.push_back(act("ROM Configuration", "Set or change where each edition's ROM set is.", [this] { menu.push(page_roms()); }, true));
    return p;
}
std::unique_ptr<Page> Frontend::page_roms() {
    auto p = std::make_unique<Page>();
    p->title = "ROM CONFIGURATION";
    p->subtitle = "Each edition is checked file by file (CRC32). Nothing is copied or changed.";
    p->wide = true;
    for (size_t k = 0; k < eds.size(); k++) {
        int i = (int)k;
        auto tmp = eds[i].make();
        ContentInfo c = tmp->content();
        p->items.push_back(header(eds[i].long_label + (i == cur_ed ? "  (selected)" : "")));
        p->items.push_back(info("Status", [this, i] { return status_text(i); }, ed_status[i].msg));
        p->items.push_back(info("Location", [this, i] { return ed_status[i].path.empty() ? std::string("not set") : ed_status[i].path; }, c.description));
        p->items.push_back(act("Browse...", "Pick " + (c.file_names.empty() ? std::string("the ROM set") : c.file_names[0]) + " or an unzipped folder for this edition.", [this, i] { pick_content_for(i); }, true));
        p->items.push_back(act("Search Automatically", "Look next to the program, in Downloads, roms and Emulation/roms.", [this, i] {
            if (i == cur_ed && !loaded) { if (find_and_load("")) { toast(eds[i].label + ": found"); go_main_menu(); return; } }
            refresh_status(i, true);
            toast(eds[i].label + (ed_status[i].state == 1 ? ": ROM set found" : ": nothing found"), ed_status[i].state != 1);
            menu.replace_top(page_roms());
        }));
        auto f = act("Forget Location", "Clears the remembered path for this edition.", [this, i] {
            if (i == cur_ed && loaded) { toast("This edition's ROM set is in use; switch editions first", true); return; }
            if (cfg.user.contains("editions") && cfg.user["editions"].contains(eds[i].id)) cfg.user["editions"][eds[i].id].erase("content_path");
            cfg.save_user(); if (i == cur_ed) content_path.clear();
            ed_status[i] = EdStatus{3, "", "Not configured"};
            menu.replace_top(page_roms());
        });
        f.visible = [this, i] { return !ed_status[i].path.empty(); };
        p->items.push_back(f);
    }
    return p;
}
std::unique_ptr<Page> Frontend::page_quick() {
    auto p = std::make_unique<Page>();
    p->title = "PAUSED";
    p->subtitle = (multi ? edition().long_label : gi.title) + "  -  " + format_time(playtime);
    p->items.push_back(act("Resume Game", "Back to the game.", [this] { resume(); }));
    p->items.push_back(act(multi ? "Restart " + edition().label : std::string("Restart Game"), "Reset the board. Unsaved progress is lost.", [this] { confirm("Restart", "Reset " + edition().long_label + "? Unsaved progress will be lost.", [this] { start_game(); }, "Restart"); }));
    {
        auto m = act("Save State", "Save to a slot.", [this] { menu.push(page_slots(true)); }, true); m.visible = [this] { return f_states; }; p->items.push_back(m);
        auto l = act("Load State", "Load from a slot.", [this] { menu.push(page_slots(false)); }, true); l.visible = [this] { return f_states; }; p->items.push_back(l);
        auto q = act("Quick Save", "Save to the quick slot (keyboard F5).", [this] { do_save("quick"); resume(); }); q.visible = [this] { return f_states; }; p->items.push_back(q);
        auto ql = act("Quick Load", "Load the quick slot (keyboard F8).", [this] { do_load("quick"); }); ql.visible = [this] { return f_states && saves.exists("quick"); }; p->items.push_back(ql);
    }
    p->items.push_back(act("Display Settings", "", [this] { menu.push(page_display()); }, true));
    p->items.push_back(act("Audio Settings", "", [this] { menu.push(page_audio()); }, true));
    p->items.push_back(act("Controller Configuration", "", [this] { menu.push(page_controls()); }, true));
    {
        auto m = act("Gadgets", "On-screen information panels.", [this] { menu.push(page_gadgets()); }, true); m.visible = [this] { return f_gadgets; }; p->items.push_back(m);
    }
    p->items.push_back(act("Gameplay Settings", "", [this] { menu.push(page_gameplay()); }, true));
    p->items.push_back(act("Screenshot", "Save the current frame as PNG.", [this] { screenshot(); }));
    for (auto &x : ad->extras()) if (x.needs_game) {
        std::string id = x.id;
        p->items.push_back(act(x.label, x.description, [this, id] { std::string msg; if (ad->run_extra(id, msg)) { if (!msg.empty()) toast(msg); resume(); } else if (!msg.empty()) toast(msg, true); }));
    }
    if (multi) {
        auto m = act("Switch Edition", "End this session and start another edition. You can save first.", [this] {
            for (size_t i = 0; i < eds.size(); i++) refresh_status((int)i, false);
            menu.push(page_editions());
        }, true);
        m.value = [this] { return edition().label; };
        p->items.push_back(m);
    }
    p->items.push_back(act("Return to Main Menu", autosave && f_states ? "Your progress is saved for Continue." : "Unsaved progress is lost.", [this] { return_to_main(); }));
    p->items.push_back(act("Exit Game", "Quit to the desktop.", [this] { confirm("Exit", "Quit " + app_title + "?", [this] { quit(); }, "Quit"); }));
    p->on_key = [this](const UiEvent &e) { if ((e.key == UiKey::Back || e.key == UiKey::Menu) && menu.stack.size() == 1) { resume(); return true; } return false; };
    return p;
}
std::unique_ptr<Page> Frontend::page_nocontent() {
    auto p = std::make_unique<Page>();
    p->title = multi ? "ROM SET NEEDED" : std::string("GAME FILES NEEDED");
    p->subtitle = multi ? edition().long_label : ci.description;
    std::string names; for (auto &n : ci.file_names) names += (names.empty() ? "" : ", ") + n;
    p->items.push_back(act("Browse for " + names + "...", "Pick the file with the controller, keyboard or mouse. You can also drag it onto this window.", [this] {
        pick_content_for(cur_ed);
    }, true));
    p->items.push_back(act("Search Again", "Look in the usual places again: next to the program, Downloads, roms, Emulation/roms.", [this] {
        if (find_and_load("")) go_main_menu(); else { toast("Not found", true); menu.replace_top(page_nocontent()); }
    }));
    if (multi) {
        auto m = act("Select Edition", "Use another edition instead. Each edition needs its own ROM set.", [this] {
            for (size_t i = 0; i < eds.size(); i++) refresh_status((int)i, false);
            menu.push(page_editions());
        }, true);
        m.value = [this] { return edition().label; };
        p->items.push_back(m);
        p->items.push_back(act("ROM Configuration", "Set or check the ROM set of every edition.", [this] {
            for (size_t i = 0; i < eds.size(); i++) refresh_status((int)i, false);
            menu.push(page_roms());
        }, true));
    }
    p->items.push_back(act("Exit", "", [this] { running = false; }));
    p->side = [this, names](Gfx &gg, const Theme &t, double x, double y, double w, double) {
        std::string txt = (multi ? edition().long_label + " needs " + ci.description + ".\n\n" : std::string()) + "This program is a native recompilation. It does not include the game's data: you need your own copy of " + names +
                          ".\n\nPut it next to the program or in your Downloads folder and it is found automatically, or browse for it here. The location is remembered.";
        if (!content_error.empty()) txt += "\n\nLast error: " + content_error;
        double yy = y;
        for (auto &l : gg.wrap(FontId::Body, 22 * gg.s, txt, w)) { gg.text(FontId::Body, 22 * gg.s, x, yy, l, t.text_dim); yy += 26 * gg.s; }
    };
    p->on_key = [](const UiEvent &e) { return e.key == UiKey::Back || e.key == UiKey::Menu; };
    return p;
}
std::unique_ptr<Page> Frontend::page_browser(const std::string &start, const std::vector<std::string> &exts, const std::string &title,
                                             std::function<void(const std::string &)> pick) {
    std::string dir = platform::is_dir(start) ? start : platform::home_dir();
    auto p = std::make_unique<Page>();
    p->title = title; p->subtitle = dir; p->wide = true;
    std::string up = platform::parent(dir);
    if (!up.empty() && up != dir)
        p->items.push_back(act("..  (up one folder)", up, [this, up, exts, title, pick] { menu.replace_top(page_browser(up, exts, title, pick), false); }));
    // romsets may also be unzipped folders: offer the current folder itself
    p->items.push_back(act("[ Use this folder ]", "Load the game files directly from " + dir, [this, dir, pick] { auto pk = pick; std::string d = dir; menu.pop(); pk(d); }));
    auto list = platform::list_dir(dir, exts);
    for (auto &e : list) {
        std::string path = e.path;
        if (e.dir) p->items.push_back(act("[" + e.name + "]", path, [this, path, exts, title, pick] { menu.replace_top(page_browser(path, exts, title, pick), false); }, true));
        else {
            auto m = act(e.name, path, [this, path, pick] { auto pk = pick; std::string pth = path; menu.pop(); pk(pth); });
            m.value = [sz = e.size] { return sz > (1u << 20) ? fmt("%.1f MB", sz / 1048576.0) : fmt("%llu KB", (unsigned long long)(sz / 1024)); };
            p->items.push_back(m);
        }
    }
    if (list.empty()) p->items.push_back(info("(no matching files here)", [] { return std::string(); }));
    p->items.push_back(header("PLACES"));
    for (auto &r : platform::roots()) if (platform::is_dir(r))
        p->items.push_back(act(r, "", [this, r, exts, title, pick] { menu.replace_top(page_browser(r, exts, title, pick), false); }, true));
    return p;
}
std::unique_ptr<Page> Frontend::page_display() {
    auto p = std::make_unique<Page>();
    p->title = "DISPLAY SETTINGS";
    DisplaySettings &d = disp.set;
    p->items.push_back(header("SCREEN"));
    p->items.push_back(toggle("Fullscreen", "Fullscreen uses the desktop resolution (no mode switch). F11 toggles it any time.",
        [&d] { return d.fullscreen; }, [this, &d](bool v) { d.fullscreen = v; apply_display(); save_settings(); }));
    p->items.push_back(toggle("V-Sync", "Synchronise to the display to avoid tearing.", [&d] { return d.vsync; }, [this, &d](bool v) { d.vsync = v; apply_display(); save_settings(); }));
    {
        auto m = choice("Orientation", "Vertical games: TATE shows the picture upright like the cabinet; YOKO keeps it sideways for a rotated monitor.",
            {{"auto", gi.rotation == Rotation::None ? "Original" : "Cabinet (TATE)"}, {"tate", "TATE (vertical)"}, {"yoko", "YOKO (sideways)"}},
            [&d] { return d.orientation; }, [this, &d](const std::string &v) { d.orientation = v; save_settings(); });
        m.visible = [this] { return gi.rotation != Rotation::None; };
        p->items.push_back(m);
    }
    p->items.push_back(choice("Aspect Ratio", "Original shows the shape of the arcade monitor. Stretch fills the screen and distorts the picture.",
        {{"original", "Original (4:3)"}, {"square", "Square pixels"}, {"stretch", "Stretch to fill"}},
        [&d] { return d.aspect; }, [this, &d](const std::string &v) { d.aspect = v; save_settings(); }));
    p->items.push_back(toggle("Integer Scaling", "Scale by whole numbers only: every pixel the same size, may leave a border.",
        [&d] { return d.integer_scale; }, [this, &d](bool v) { d.integer_scale = v; save_settings(); }));
    {
        auto m = slider("Picture Size", "Shrink the picture inside the screen.", 0.5, 1.0, 0.05, [&d] { return d.zoom; }, [this, &d](double v) { d.zoom = v; save_settings(); }, pct);
        m.visible = [&d] { return !d.integer_scale && d.aspect != "stretch"; };
        p->items.push_back(m);
    }
    p->items.push_back(choice("Filter", "Nearest: sharp pixels. Bilinear: soft. Sharp bilinear: crisp pixels without uneven sizes at non-integer scales.",
        {{"nearest", "Nearest neighbour"}, {"linear", "Bilinear"}, {"sharp", "Sharp bilinear"}},
        [&d] { return d.filter; }, [this, &d](const std::string &v) { d.filter = v; save_settings(); }));
    {
        auto m = act("CRT Effects", "Scanlines, screen curvature, shadow mask and phosphor glow.", [this] { menu.push(page_crt()); }, true);
        m.value = [&d] { return d.crt ? std::string("On") : std::string("Off"); };
        m.visible = [this] { return f_crt; };
        p->items.push_back(m);
    }
    p->items.push_back(header("COLOUR"));
    p->items.push_back(slider("Brightness", "", 0.5, 1.5, 0.05, [&d] { return d.brightness; }, [this, &d](double v) { d.brightness = v; save_settings(); }, pct));
    p->items.push_back(slider("Gamma", "Raise to brighten dark areas without washing out highlights.", 0.6, 1.6, 0.05, [&d] { return d.gamma; }, [this, &d](double v) { d.gamma = v; save_settings(); }, [](double v) { return fmt("%.2f", v); }));
    p->items.push_back(header("BORDER & INTERFACE"));
    p->items.push_back(choice("Border", "What fills the screen around the game picture.", {{"theme", "Animated theme"}, {"black", "Black"}, {"image", "Custom image"}},
        [&d] { return d.border; }, [this, &d](const std::string &v) { d.border = v; save_settings(); }));
    {
        auto m = act("Choose Wallpaper Image...", "PNG or JPG used when Border is set to Custom image.", [this, &d] {
            std::string start = d.wallpaper.empty() ? platform::join(platform::home_dir(), "Pictures") : platform::parent(d.wallpaper);
            menu.push(page_browser(start, {".png", ".jpg", ".jpeg"}, "SELECT WALLPAPER", [this, &d](const std::string &f) { d.wallpaper = f; d.border = "image"; save_settings(); }));
        }, true);
        m.value = [&d] { return d.wallpaper.empty() ? std::string("none") : platform::filename(d.wallpaper); };
        p->items.push_back(m);
    }
    {
        std::vector<std::pair<std::string, std::string>> ch;
        for (auto &id : themes.ids()) ch.push_back({id, themes.name_of(id)});
        p->items.push_back(choice("Interface Theme", multi ? "Colours of menus and gadgets for this edition (each edition remembers its own). Add your own in themes/*.json." : "Colours of menus and gadgets. Add your own in themes/*.json.", ch, [this] { return theme_id; }, [this](const std::string &v) { theme_id = v; save_settings(); }));
        p->items.push_back(choice("Accent Colour", "Highlight colour used for focus, bars and gadgets.",
            {{"theme", "Theme default"}, {"cyan", "Cyan"}, {"amber", "Amber"}, {"magenta", "Magenta"}, {"lime", "Lime"}, {"red", "Red"}, {"white", "White"}},
            [this] { return accent; }, [this](const std::string &v) { accent = v; save_settings(); }));
    }
    p->items.push_back(act("Reset Display Settings", "Back to this game's recommended settings.", [this] {
        confirm("Reset", "Restore the default display settings?", [this] {
            for (auto k : {"orientation", "aspect_mode", "integer_scaling", "filter", "crt", "scanlines", "curvature", "mask", "mask_strength", "glow", "brightness", "gamma", "zoom", "border"})
                if (cfg.user.contains("display")) cfg.user["display"].erase(k);
            bool fs = disp.set.fullscreen, vs = disp.set.vsync; std::string wp = disp.set.wallpaper;
            disp.set = DisplaySettings(); disp.set.fullscreen = fs; disp.set.vsync = vs; disp.set.wallpaper = wp;
            load_settings(); save_settings(); menu.replace_top(page_display());
        });
    }));
    return p;
}
std::unique_ptr<Page> Frontend::page_crt() {
    auto p = std::make_unique<Page>();
    p->title = "CRT EFFECTS";
    DisplaySettings &d = disp.set;
    auto on = [&d] { return d.crt; };
    p->items.push_back(toggle("CRT Effects", "Simulate a 1990s arcade monitor. Scanlines follow the tube, so they run vertically in TATE.", on, [this, &d](bool v) { d.crt = v; save_settings(); }));
    auto add = [&](MenuItem m) { m.enabled = on; p->items.push_back(m); };
    add(slider("Scanlines", "", 0, 1, 0.05, [&d] { return d.scanlines; }, [this, &d](double v) { d.scanlines = v; save_settings(); }, pct));
    add(slider("Screen Curvature", "Curved glass with darker corners.", 0, 1, 0.05, [&d] { return d.curvature; }, [this, &d](double v) { d.curvature = v; save_settings(); }, pct));
    add(choice("Mask", "Aperture grille: vertical RGB stripes (Trinitron-style). Shadow mask: staggered RGB triads.",
        {{"none", "None"}, {"aperture", "Aperture grille"}, {"shadow", "Shadow mask"}}, [&d] { return d.mask; }, [this, &d](const std::string &v) { d.mask = v; save_settings(); }));
    add(slider("Mask Strength", "", 0, 1, 0.05, [&d] { return d.mask_strength; }, [this, &d](double v) { d.mask_strength = v; save_settings(); }, pct));
    add(slider("Phosphor Glow", "Soft light bleeding around bright pixels.", 0, 1, 0.05, [&d] { return d.glow; }, [this, &d](double v) { d.glow = v; save_settings(); }, pct));
    p->items.push_back(act("Preset: Clean Arcade Monitor", "Light scanlines, no curvature.", [this, &d] { d.crt = true; d.scanlines = 0.35; d.curvature = 0; d.mask = "none"; d.glow = 0.15; save_settings(); }));
    p->items.push_back(act("Preset: Consumer TV", "Curved glass, shadow mask, glow.", [this, &d] { d.crt = true; d.scanlines = 0.5; d.curvature = 0.6; d.mask = "shadow"; d.mask_strength = 0.35; d.glow = 0.35; save_settings(); }));
    p->items.push_back(act("Preset: PVM", "Strong scanlines and aperture grille.", [this, &d] { d.crt = true; d.scanlines = 0.7; d.curvature = 0.15; d.mask = "aperture"; d.mask_strength = 0.3; d.glow = 0.1; save_settings(); }));
    return p;
}
std::unique_ptr<Page> Frontend::page_audio() {
    auto p = std::make_unique<Page>();
    p->title = "AUDIO SETTINGS";
    p->items.push_back(slider("Master Volume", "", 0, 100, 5, [this] { return (double)audio.volume; }, [this](double v) { audio.volume = (int)v; save_settings(); }, [](double v) { return fmt("%d", (int)v); }));
    p->items.push_back(toggle("Mute", "", [this] { return audio.mute; }, [this](bool v) { audio.mute = v; save_settings(); }));
    {
        std::vector<std::pair<std::string, std::string>> ch = {{"", "System default"}};
        for (auto &d : audio.devices()) ch.push_back({d, d});
        p->items.push_back(choice("Output Device", "Takes effect immediately.", ch, [this] { return audio.device; }, [this](const std::string &v) { audio.device = v; audio.reopen(); save_settings(); }));
    }
    p->items.push_back(choice("Latency", "Low reacts fastest but may crackle on slow systems. The queue is capped, so audio never drifts behind the picture.",
        {{"0", "Low (512 samples)"}, {"1", "Normal (1024)"}, {"2", "Safe (2048)"}},
        [this] { return std::to_string(audio.latency); }, [this](const std::string &v) { audio.latency = atoi(v.c_str()); audio.reopen(); save_settings(); }));
    p->items.push_back(info("Audio driver", [this] { return audio.driver() + (audio.ok() ? "" : " (no device)"); }));
    return p;
}
std::unique_ptr<Page> Frontend::page_controls() {
    auto p = std::make_unique<Page>();
    p->title = "CONTROLLER SETTINGS";
    int players = 1;
    for (size_t i = 0; i < n_game_actions; i++) players = std::max(players, actions[i].player + 1);
    for (int pl = 0; pl < players; pl++)
        p->items.push_back(act(fmt("Player %d Controls", pl + 1), fmt("Remap player %d. Player %d uses controller %d plus their keyboard keys.", pl + 1, pl + 1, pl + 1), [this, pl] { menu.push(page_bindings(pl, false)); }, true));
    p->items.push_back(act("Frontend Hotkeys", "Quick save/load, screenshot, fast-forward and rewind buttons.", [this] { menu.push(page_bindings(0, true)); }, true));
    {
        std::vector<std::pair<std::string, std::string>> ch;
        for (auto &n : input.presets(cfg.profile)) {
            std::string lab = n;
            if (cfg.profile.contains("control_preset_names") && cfg.profile["control_preset_names"].contains(n)) lab = cfg.profile["control_preset_names"][n].get<std::string>();
            ch.push_back({n, lab});
        }
        p->items.push_back(choice("Preset", "Applies a ready-made layout to all players (replaces your mapping).", ch, [this] { return cfg.get_str("input.preset", "default"); },
            [this](const std::string &v) {
                cfg.set("input.preset", v);
                json keep = input.save();
                input.defaults_from_profile(cfg.profile, v);
                auto &fe = input.bindings;
                for (int a = 0; a < FE_COUNT; a++) {  // hotkeys survive preset changes
                    auto it = keep["bindings"].find(actions[n_game_actions + a].id);
                    fe[n_game_actions + a].clear();
                    if (it != keep["bindings"].end()) for (auto &b : *it) fe[n_game_actions + a].push_back(Binding::from_json(b));
                }
                save_settings(); toast("Preset applied");
            }));
    }
    p->items.push_back(slider("Stick Dead Zone", "How far the analogue stick must move before it counts as a direction.", 0.1, 0.8, 0.05,
        [this] { return input.deadzone; }, [this](double v) { input.deadzone = v; save_settings(); }, pct));
    p->items.push_back(choice("Menu Shortcut (controller)", "Opens the pause menu during play. Esc always works on a keyboard.",
        {{"back+start", "View + Menu (Select + Start)"}, {"l3+r3", "L3 + R3"}, {"lb+rb+start", "LB + RB + Start"}, {"guide", "Guide / Home"}},
        [this] { return input.menu_combo; }, [this](const std::string &v) { input.menu_combo = v; save_settings(); }));
    p->items.push_back(act("Controls Reference", "Everything mapped right now.", [this] { menu.push(page_controls_ref()); }, true));
    p->items.push_back(header("CONNECTED"));
    for (int i = 0; i < 4; i++) {
        auto m = info(fmt("Controller %d", i + 1), [this, i] { return input.pad_name(i); });
        m.visible = [this, i] { return i == 0 || i < input.pad_count(); };
        p->items.push_back(m);
    }
    p->items.push_back(act("Reset All Controls", "", [this] {
        confirm("Reset", "Restore the default controls for every player?", [this] {
            if (cfg.user.contains("input")) cfg.user.erase("input");
            load_settings(); save_settings(); toast("Controls reset");
        });
    }));
    return p;
}
std::unique_ptr<Page> Frontend::page_bindings(int player, bool hot) {
    auto p = std::make_unique<Page>();
    p->title = hot ? "FRONTEND HOTKEYS" : fmt("PLAYER %d CONTROLS", player + 1);
    p->subtitle = "A / Enter: map a button     X / Del: clear     Left/Right: autofire";
    p->wide = true;
    size_t b0 = hot ? n_game_actions : 0, b1 = hot ? actions.size() : n_game_actions;
    for (size_t i = b0; i < b1; i++) {
        if (!hot && actions[i].player != player) continue;
        MenuItem m = act(actions[i].label, "", nullptr);
        m.activate = [this, i] { capture_action = i; capture_label = actions[i].label; input.begin_capture(); };
        m.value = [this, i] {
            std::string s;
            for (auto &b : input.bindings[i]) s += (s.empty() ? "" : "  /  ") + b.describe();
            if (s.empty()) s = "(none)";
            if (i < n_game_actions && input.turbo[i]) s = fmt("[TURBO %dHz] ", (int)std::lround(gi.refresh_hz / input.turbo[i])) + s;
            return s;
        };
        bool is_btn = i < n_game_actions && actions[i].role == ActionRole::Button;
        m.desc = is_btn ? "Left/Right sets autofire for this button." : "";
        p->items.push_back(m);
    }
    p->on_key = [this, p_ = p.get(), b0, b1, player, hot](const UiEvent &e) {
        // map selection index to action index
        std::vector<size_t> idx;
        for (size_t i = b0; i < b1; i++) if (hot || actions[i].player == player) idx.push_back(i);
        if (p_->sel < 0 || p_->sel >= (int)idx.size()) return false;
        size_t a = idx[p_->sel];
        if (e.key == UiKey::Aux) { input.bindings[a].clear(); save_settings(); return true; }
        if ((e.key == UiKey::Left || e.key == UiKey::Right) && a < n_game_actions && actions[a].role == ActionRole::Button) {
            static const int rates[] = {0, 2, 3, 4, 6};   // frames per press cycle: off, 30, 20, 15, 10 Hz at 60fps
            int cur = 0; for (int k = 0; k < 5; k++) if (input.turbo[a] == rates[k]) cur = k;
            cur = (cur + (e.key == UiKey::Right ? 1 : 4)) % 5;
            input.turbo[a] = rates[cur]; save_settings(); return true;
        }
        return false;
    };
    return p;
}
std::unique_ptr<Page> Frontend::page_controls_ref() {
    std::string t;
    for (size_t i = 0; i < actions.size(); i++) {
        std::string s;
        for (auto &b : input.bindings[i]) s += (s.empty() ? "" : ", ") + b.describe();
        std::string who = i < n_game_actions ? fmt("P%d ", actions[i].player + 1) : "";
        t += who + actions[i].label + ":  " + (s.empty() ? "-" : s) + "\n";
    }
    t += "\nAlways available: Esc = pause menu, F11 = fullscreen.\nController pause menu: " + input.menu_combo + ".";
    return page_text("CONTROLS REFERENCE", t);
}
std::unique_ptr<Page> Frontend::page_gameplay() {
    auto p = std::make_unique<Page>();
    p->title = "GAMEPLAY SETTINGS";
    p->items.push_back(header("ASSISTS"));
    for (size_t i = 0; i < n_game_actions; i++) {
        if (actions[i].role != ActionRole::Button || actions[i].player != 0) continue;
        p->items.push_back(choice("Autofire: " + actions[i].label, "Repeatedly presses the button while you hold it.",
            {{"0", "Off"}, {"2", "30 per second"}, {"3", "20 per second"}, {"4", "15 per second"}, {"6", "10 per second"}},
            [this, i] { return std::to_string(input.turbo[i]); }, [this, i](const std::string &v) {
                input.turbo[i] = atoi(v.c_str());
                for (size_t k = 0; k < n_game_actions; k++)   // mirror on other players' same button
                    if (actions[k].player != 0 && actions[k].short_label == actions[i].short_label && actions[k].role == ActionRole::Button) input.turbo[k] = input.turbo[i];
                save_settings();
            }));
    }
    {
        auto m = toggle("Rewind", "Hold Backspace (or your Rewind hotkey) to go back in time. Uses some memory and CPU.",
            [this] { return saves.rewind_enabled; }, [this](bool v) { saves.rewind_enabled = v; saves.rewind_clear(); save_settings(); });
        m.visible = [this] { return f_rewind; };
        p->items.push_back(m);
    }
    {
        auto m = choice("Fast-Forward Speed", "While holding the fast-forward hotkey (Tab).", {{"2", "2x"}, {"3", "3x"}, {"4", "4x"}, {"8", "8x"}},
            [this] { return std::to_string(ff_speed); }, [this](const std::string &v) { ff_speed = atoi(v.c_str()); save_settings(); });
        m.visible = [this] { return f_ff; };
        p->items.push_back(m);
    }
    p->items.push_back(header("SESSION"));
    {
        auto m = toggle("Autosave on Exit", "Save automatically when you leave, so Continue picks up where you stopped.",
            [this] { return autosave; }, [this](bool v) { autosave = v; save_settings(); });
        m.visible = [this] { return f_states; };
        p->items.push_back(m);
    }
    p->items.push_back(toggle("Pause When Window Loses Focus", "", [this] { return pause_on_focus; }, [this](bool v) { pause_on_focus = v; save_settings(); }));
    {
        auto m = toggle("Menu Preview Sound", "Play the attract-mode sound behind the main menu.", [this] { return preview_audio; }, [this](bool v) { preview_audio = v; save_settings(); });
        m.visible = [this] { return f_attract; };
        p->items.push_back(m);
    }
    if (cfg.profile.contains("gameplay_note")) {
        std::string n = cfg.profile["gameplay_note"].get<std::string>();
        p->items.push_back(header("GAME OPTIONS"));
        p->items.push_back(info("Difficulty, lives, extends", [] { return std::string("in the service menu"); }, n));
    }
    return p;
}
std::unique_ptr<Page> Frontend::page_gadgets() {
    auto p = std::make_unique<Page>();
    p->title = "GADGETS";
    p->subtitle = "Information panels around the game picture";
    p->items.push_back(toggle("Show Gadgets", "", [this] { return gadgets.enabled; }, [this](bool v) { gadgets.enabled = v; save_settings(); }));
    for (auto &gd : gadgets.gadgets) {
        std::string id = gd.id;
        std::string d = gd.from_runtime ? "Supplied by the game runtime." : id == "input" ? "Shows what the game receives from player 1, after remapping and autofire."
                       : id == "frametime" ? "Time spent emulating each frame (lower is better; budget is " + fmt("%.1f", 1000.0 / gi.refresh_hz) + " ms)." : "";
        auto m = toggle(gd.label, d, [this, id] { return gadgets.find(id)->enabled; }, [this, id](bool v) { gadgets.find(id)->enabled = v; save_settings(); });
        m.enabled = [this] { return gadgets.enabled; };
        p->items.push_back(m);
    }
    p->items.push_back(act("Arrange Gadgets...", "Move and resize panels on top of the game.", [this] {
        arrange_idx = 0; mode = Mode::Arrange; menu.clear();
    }));
    p->items.push_back(info("Score / lives / stage panels", [this] { return caps.gadgets.empty() ? std::string("not available") : std::string("see above"); },
        "Game-data panels appear only when the runtime exposes verified values. This release has no documented memory map for them, so none are guessed."));
    return p;
}
std::unique_ptr<Page> Frontend::page_slots(bool saving) {
    saves.refresh(ren);
    auto p = std::make_unique<Page>();
    p->title = saving ? "SAVE STATE" : "LOAD STATE";
    p->subtitle = saving ? "Choose a slot" : "Choose a save";
    p->wide = true;
    for (auto &s : saves.slots) {
        if (saving && s.id == "auto") continue;
        if (!saving && !s.exists) continue;
        std::string id = s.id;
        std::string lab = id == "auto" ? "Continue (autosave)" : id == "quick" ? "Quick slot" : "Slot " + id;
        auto m = act(lab, s.exists ? "Saved " + s.date + "  -  play time " + format_time(s.playtime) : "Empty", [this, id, saving, ex = s.exists] {
            if (saving) {
                auto go = [this, id] { do_save(id); menu.replace_top(page_slots(true)); };
                if (ex) confirm("Overwrite", "Replace the save in this slot?", go, "Overwrite"); else go();
            } else do_load(id);
        });
        m.value = [d = s.exists ? s.date : std::string("empty")] { return d; };
        p->items.push_back(m);
    }
    if (p->items.empty()) p->items.push_back(info("No saves yet", [] { return std::string(); }));
    p->side = [this, pp = p.get()](Gfx &gg, const Theme &t, double x, double y, double w, double h) {
        if (pp->items.empty()) return;
        std::string lab = pp->items[pp->sel].label;
        for (auto &s : saves.slots) {
            std::string l = s.id == "auto" ? "Continue (autosave)" : s.id == "quick" ? "Quick slot" : "Slot " + s.id;
            if (l != lab) continue;
            if (!s.tex) { gg.text(FontId::Body, 24 * gg.s, x + w / 2, y + h / 3, "EMPTY", t.text_dim, 1); return; }
            double ar = (double)s.tw / s.th, tw = w, th = w / ar;
            if (th > h * 0.8) { th = h * 0.8; tw = th * ar; }
            SDL_FRect r{(float)(x + (w - tw) / 2), (float)y, (float)tw, (float)th};
            gg.rect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, t.panel_edge);
            SDL_RenderCopyF(gg.r, s.tex, nullptr, &r);
            gg.text(FontId::Body, 22 * gg.s, x + w / 2, y + th + 14 * gg.s, s.date, t.text, 1);
            return;
        }
    };
    if (!saving) p->on_key = [this, pp = p.get()](const UiEvent &e) {
        if (e.key != UiKey::Aux || pp->items.empty()) return false;
        std::string lab = pp->items[pp->sel].label;
        for (auto &s : saves.slots) {
            std::string l = s.id == "auto" ? "Continue (autosave)" : s.id == "quick" ? "Quick slot" : "Slot " + s.id;
            if (l == lab) { std::string id = s.id; confirm("Delete", "Delete this save?", [this, id] { saves.remove(id); menu.replace_top(page_slots(false)); }, "Delete"); }
        }
        return true;
    };
    return p;
}
std::unique_ptr<Page> Frontend::page_extras() {
    auto p = std::make_unique<Page>();
    p->title = "EXTRAS";
    for (auto &x : ad->extras()) {
        std::string id = x.id; bool needs = x.needs_game, conf = x.confirm; std::string label = x.label;
        auto run = [this, id, needs] {
            std::string msg;
            if (needs && mode == Mode::MainMenu) start_game();
            bool ok = ad->run_extra(id, msg);
            if (!msg.empty()) toast(msg, !ok);
            if (ok && needs && mode == Mode::Paused) resume();
        };
        p->items.push_back(act(label, x.description, conf ? std::function<void()>([this, run, label] { confirm(label, "Are you sure?", run); }) : std::function<void()>(run)));
    }
    {
        auto m = act("Gadgets", "Information panels shown during play.", [this] { menu.push(page_gadgets()); }, true);
        m.visible = [this] { return f_gadgets; };
        p->items.push_back(m);
    }
    p->items.push_back(act(multi ? "ROM Configuration" : "Change Game File...", multi ? std::string("Set or check the ROM set of every edition.") : "Pick a different copy of " + (ci.file_names.empty() ? std::string("the game") : ci.file_names[0]) + ".",
        [this] { if (multi) { for (size_t i = 0; i < eds.size(); i++) refresh_status((int)i, false); menu.push(page_roms()); } else pick_content(); }, true));
    p->items.push_back(act("Open Screenshots Folder", platform::join(pref, "screenshots"), [this] {
        std::string d = platform::join(pref, "screenshots"); platform::make_dirs(d);
        SDL_OpenURL(("file:///" + d).c_str());
    }));
    p->items.push_back(act("Open Data Folder", "Settings, saves and high scores: " + pref, [this] { SDL_OpenURL(("file:///" + pref).c_str()); }));
    p->items.push_back(act("Licences", "Open-source components used in this build.", [this] {
        menu.push(page_text("LICENCES",
            "RetroFront frontend - original work.\n"
            "Musashi 68000 core - Karl Stenerud, MIT licence.\n"
            "Taito F3 / Taito EN hardware logic follows MAME's taito_f3, taito_en, es5506 and mc68681 drivers - BSD-3-Clause.\n"
            "SDL2 - zlib licence.   miniz - MIT.   stb_truetype / stb_image - public domain.\n"
            "nlohmann/json - MIT.\n"
            "Press Start 2P (CodeMan38) and Rajdhani (Indian Type Foundry) - SIL Open Font Licence 1.1.\n"
            "Game data is not included and is property of its owners."));
    }, true));
    return p;
}
std::unique_ptr<Page> Frontend::page_about() {
    auto p = std::make_unique<Page>();
    p->title = "ABOUT";
    p->wide = true;
    p->items.push_back(info("Title", [this] { return multi ? edition().long_label : gi.title; }));
    p->items.push_back(info("Original release", [this] { return gi.developer + "  " + gi.year; }));
    p->items.push_back(info("Hardware", [this] { return gi.platform; }));
    std::vector<std::pair<std::string, std::string>> extra; ad->about(extra);
    for (auto &kv : extra) { std::string v = kv.second; p->items.push_back(info(kv.first, [v] { return v; })); }
    p->items.push_back(info("Game file", [this] { return platform::filename(content_path); }, content_path));
    p->items.push_back(info("Frontend", [] { return std::string(kFrontendName) + " " + kFrontendVersion; }));
    p->items.push_back(info("Video", [this] { SDL_RendererInfo ri; SDL_GetRendererInfo(ren, &ri); return std::string(ri.name) + "  " + std::to_string(g.w) + "x" + std::to_string(g.h); }));
    p->items.push_back(info("Platform", [] { return std::string(SDL_GetPlatform()) + (platform::is_steam_deck() ? "  (Steam Deck)" : ""); }));
    return p;
}
std::unique_ptr<Page> Frontend::page_text(const std::string &title, const std::string &text) {
    auto p = std::make_unique<Page>();
    p->title = title; p->wide = true;
    size_t start = 0;
    while (start <= text.size()) {
        size_t e = text.find('\n', start);
        std::string line = text.substr(start, e == std::string::npos ? std::string::npos : e - start);
        p->items.push_back(info(line, [] { return std::string(); }));
        if (e == std::string::npos) break;
        start = e + 1;
    }
    return p;
}

// =====================================================================================================
// frame + input
void Frontend::run_game_frame(bool with_input, bool audio_out) {
    if (with_input) {
        input.sample(in_full, frame_no);
        in_game.pressed.assign(in_full.pressed.begin(), in_full.pressed.begin() + n_game_actions);
    } else in_game.pressed.assign(n_game_actions, 0);
    uint64_t t0 = SDL_GetPerformanceCounter();
    ad->run_frame(in_game);
    emu_acc += (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    fps_frames++;
    disp.upload(ad->video(), gi.rotation);
    int16_t buf[8192 * 2];
    size_t n;
    while ((n = ad->read_audio(buf, 8192)) > 0) if (audio_out) audio.push(buf, n);
    frame_no++;
}

void Frontend::handle_hotkeys() {
    // edge-detect the frontend hotkey actions (bindable in Controller Settings > Frontend Hotkeys)
    if (in_full.pressed.size() < actions.size()) return;
    for (int a = 0; a < FE_COUNT; a++) {
        bool now = in_full.pressed[n_game_actions + a];
        bool edge = now && !fe_prev[a];
        fe_prev[a] = now;
        if (a == FE_FFWD) ffwd = now && f_ff;
        else if (a == FE_REWIND) rewinding = now && saves.rewind_enabled;
        else if (edge) {
            if (a == FE_QUICKSAVE) do_save("quick");
            else if (a == FE_QUICKLOAD) { if (saves.exists("quick")) do_load("quick"); else toast("Quick slot is empty", true); }
            else if (a == FE_SCREENSHOT) screenshot();
        }
    }
}

void Frontend::handle_ui(const UiEvent &e) {
    if (dlg.active) {
        if (e.key == UiKey::Left) dlg.sel = (dlg.sel + (int)dlg.buttons.size() - 1) % (int)dlg.buttons.size();
        else if (e.key == UiKey::Right || e.key == UiKey::Down || e.key == UiKey::Up) dlg.sel = (dlg.sel + 1) % (int)dlg.buttons.size();
        else if (e.key == UiKey::Accept) { dlg.active = false; auto cb = dlg.cb; int s_ = dlg.sel; if (cb) cb(s_); }
        else if (e.key == UiKey::Back || e.key == UiKey::Menu) { dlg.active = false; auto cb = dlg.cb; int n_ = (int)dlg.buttons.size(); if (cb && n_ > 1) cb(n_ - 1); }
        return;
    }
    if (mode == Mode::Arrange) {
        auto &gs = gadgets.gadgets;
        if (gs.empty()) { mode = Mode::Paused; menu.push(page_quick()); return; }
        Gadget &gd = gs[arrange_idx % gs.size()];
        double st = e.repeat ? 0.012 : 0.006;
        switch (e.key) {
        case UiKey::Up: gd.y = clampd(gd.y - st, 0, 0.95); break;
        case UiKey::Down: gd.y = clampd(gd.y + st, 0, 0.95); break;
        case UiKey::Left: gd.x = clampd(gd.x - st, 0, 0.95); break;
        case UiKey::Right: gd.x = clampd(gd.x + st, 0, 0.95); break;
        case UiKey::PageL: arrange_idx = (arrange_idx + (int)gs.size() - 1) % (int)gs.size(); break;
        case UiKey::PageR: arrange_idx = (arrange_idx + 1) % (int)gs.size(); break;
        case UiKey::Aux: { static const double sc[] = {0.75, 1.0, 1.25, 1.5, 2.0}; int k = 0; for (int i = 0; i < 5; i++) if (std::fabs(gd.scale - sc[i]) < 0.01) k = i; gd.scale = sc[(k + 1) % 5]; break; }
        case UiKey::Accept: gd.enabled = !gd.enabled; break;
        case UiKey::Back: case UiKey::Menu:
            save_settings();
            mode = loaded && disp.has_frame() && frame_no ? Mode::Paused : Mode::MainMenu;
            if (mode == Mode::Paused) menu.push(page_quick()); else go_main_menu();
            menu.push(page_gadgets());
            break;
        default: break;
        }
        return;
    }
    if (mode == Mode::Playing) return;
    bool close = false;
    menu.handle(e, close);
    if (close && mode == Mode::Paused) resume();
}

// =====================================================================================================
// drawing
void Frontend::draw_backdrop(double dim) {
    const Theme &t = theme();
    g.gradient_v(0, 0, g.w, g.h, t.bg1, t.bg0);
    double tt = time_s;
    if (t.backdrop == "grid") {
        double sp = 48 * g.s, off = fmod(tt * 18 * g.s, sp);
        for (double x = -sp + off; x < g.w; x += sp) g.rect(x, 0, 1, g.h, with_alpha(t.accent, 0.05));
        for (double y = -sp + off; y < g.h; y += sp) g.rect(0, y, g.w, 1, with_alpha(t.accent, 0.05));
    } else if (t.backdrop == "stripes") {
        double sp = 70 * g.s, off = fmod(tt * 25 * g.s, sp);
        for (double x = -g.h - sp + off; x < g.w; x += sp) {
            SDL_Color c{t.accent.r, t.accent.g, t.accent.b, 14};
            SDL_Vertex v[4] = {{{(float)x, (float)g.h}, c, {0, 0}}, {{(float)(x + sp * 0.45), (float)g.h}, c, {0, 0}},
                               {{(float)(x + sp * 0.45 + g.h), 0}, c, {0, 0}}, {{(float)(x + g.h), 0}, c, {0, 0}}};
            int idx[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(ren, nullptr, v, 4, idx, 6);
        }
    } else {
        for (int i = 0; i < 90; i++) {
            double sx = fmod(i * 7919.0 * 0.37, 1.0), sp = 0.2 + fmod(i * 0.618, 1.0);
            double x = fmod(sx * g.w + tt * 30 * sp * g.s, (double)g.w), y = fmod(i * 104729.0 * 0.0137, 1.0) * g.h;
            g.rect(x, y, 2 * g.s * sp, 2 * g.s * sp, with_alpha(i % 3 ? t.text : t.accent, 0.25 * sp));
        }
    }
    // soft accent glow + vignette
    g.gradient_v(0, 0, g.w, g.h * 0.35, with_alpha(t.accent, 0.06), with_alpha(t.accent, 0));
    g.gradient_v(0, g.h * 0.7, g.w, g.h * 0.3, with_alpha(t.bg0, 0), with_alpha(t.bg0, 0.85));
    if (dim > 0) g.rect(0, 0, g.w, g.h, rgba(0, (uint8_t)(dim * 255)));
}
void Frontend::draw_logo(double x, double y, double w) {
    const Theme &t = theme();
    std::string title = multi ? app_title : gi.title;
    for (auto &c : title) c = (char)toupper((unsigned char)c);
    double size = 46 * g.s;
    while (size > 18 * g.s && g.text_width(FontId::Display, size, title, 2 * g.s) > w) size -= 2 * g.s;
    double sh = 4 * g.s;
    g.text(FontId::Display, size, x + sh, y + sh, title, with_alpha(t.accent2, 0.9), 0, 2 * g.s);
    g.text(FontId::Display, size, x, y, title, t.text, 0, 2 * g.s);
    double tw = g.text_width(FontId::Display, size, title, 2 * g.s);
    double ph = fmod(time_s * 0.35, 1.0);
    g.gradient_h(x, y + size + 12 * g.s, tw, 4 * g.s, t.accent, with_alpha(t.accent2, 0.6));
    g.rect(x + tw * ph, y + size + 11 * g.s, 30 * g.s, 6 * g.s, with_alpha(t.text, 0.5));
    std::string sub = gi.developer + "  " + gi.year + "   |   " + gi.platform;
    double sy = y + size + 26 * g.s;
    if (multi) {
        // the selected edition, prominently: Start Game and Continue launch this one
        std::string lab = edition().label;
        for (auto &c : lab) c = (char)toupper((unsigned char)c);
        double fs = 13 * g.s, pad = 12 * g.s;
        double bw = g.text_width(FontId::Display, fs, lab, 1.5 * g.s) + pad * 2, bh = 30 * g.s;
        g.rounded(x, sy, bw, bh, 6 * g.s, t.accent);
        g.text(FontId::Display, fs, x + pad, sy + (bh - fs) / 2 - 1 * g.s, lab, t.highlight_text, 0, 1.5 * g.s);
        g.text_fit(FontId::Body, 20 * g.s, x + bw + 14 * g.s, sy + 2 * g.s, w - bw - 14 * g.s, "EDITION   " + edition().detail, t.text_dim);
        sy += bh + 8 * g.s;
    }
    g.text_fit(FontId::Body, 22 * g.s, x, sy, w, sub, t.text_dim);
}
SDL_Rect Frontend::preview_rect() const {
    // right-hand area of the main menu, respecting the game's displayed aspect
    int ax = (int)(g.w * 0.50), ay = (int)(g.h * 0.08), aw = (int)(g.w * 0.46), ah = (int)(g.h * 0.80);
    double ar = disp.rotated() ? 3.0 / 4.0 : 4.0 / 3.0;
    if (disp.has_frame() && disp.set.aspect == "square") ar = (double)disp.out_w() / disp.out_h();
    int w = aw, h = (int)(aw / ar);
    if (h > ah) { h = ah; w = (int)(ah * ar); }
    return SDL_Rect{ax + (aw - w) / 2, ay + (ah - h) / 2, w, h};
}
void Frontend::draw_prompts(const std::vector<std::pair<std::string, std::string>> &items) {
    const Theme &t = theme();
    double sz = 20 * g.s, y = g.h - 46 * g.s;
    g.gradient_v(0, y - 16 * g.s, g.w, 62 * g.s, with_alpha(t.bg0, 0), with_alpha(t.bg0, 0.9));
    double x = g.w * 0.06;
    for (auto &it : items) x += g.prompt(x, y, sz, it.first, it.second, t, input.pad_style_prompts);
}
void Frontend::draw_dialog() {
    if (!dlg.active) return;
    const Theme &t = theme();
    g.rect(0, 0, g.w, g.h, rgba(0, 170));
    double w = std::min(g.w * 0.8, 620 * g.s), x = (g.w - w) / 2;
    auto lines = g.wrap(FontId::Body, 24 * g.s, dlg.text, w - 60 * g.s);
    double h = (120 + lines.size() * 28) * g.s + 50 * g.s, y = (g.h - h) / 2;
    g.rounded(x, y, w, h, 12 * g.s, with_alpha(t.panel, 0.98));
    g.rect(x, y, w, 4 * g.s, t.accent);
    g.text(FontId::Display, 16 * g.s, x + 30 * g.s, y + 26 * g.s, dlg.title, t.accent, 0, 1.5 * g.s);
    double ly = y + 64 * g.s;
    for (auto &l : lines) { g.text(FontId::Body, 24 * g.s, x + 30 * g.s, ly, l, t.text); ly += 28 * g.s; }
    double bw = 160 * g.s, bx = x + w - 30 * g.s - bw * dlg.buttons.size() - 14 * g.s * (dlg.buttons.size() - 1), by = y + h - 62 * g.s;
    for (size_t i = 0; i < dlg.buttons.size(); i++) {
        bool s = (int)i == dlg.sel;
        g.rounded(bx, by, bw, 40 * g.s, 6 * g.s, s ? t.accent : with_alpha(t.text_dim, 0.25));
        g.text(FontId::Body, 24 * g.s, bx + bw / 2, by + 6 * g.s, dlg.buttons[i], s ? t.highlight_text : t.text, 1);
        bx += bw + 14 * g.s;
    }
}
void Frontend::draw_toasts() {
    const Theme &t = theme();
    double y = g.h * 0.06;
    for (auto it = toasts.begin(); it != toasts.end();) {
        double age = time_s - it->t0;
        if (age > 2.8) { it = toasts.erase(it); continue; }
        double a = std::min(1.0, std::min(age * 6, (2.8 - age) * 3));
        double w = g.text_width(FontId::Body, 24 * g.s, it->text) + 50 * g.s, x = (g.w - w) / 2;
        double yy = y - (1 - ease_out(std::min(1.0, age * 5))) * 20 * g.s;
        g.rounded(x, yy, w, 42 * g.s, 21 * g.s, with_alpha(t.panel, 0.95 * a));
        g.rect(x + 16 * g.s, yy + 12 * g.s, 4 * g.s, 18 * g.s, with_alpha(it->error ? t.danger : t.accent, a));
        g.text(FontId::Body, 24 * g.s, x + 30 * g.s, yy + 7 * g.s, it->text, with_alpha(t.text, a));
        y += 50 * g.s; ++it;
    }
}
void Frontend::draw_capture() {
    if (!input.capturing) return;
    const Theme &t = theme();
    g.rect(0, 0, g.w, g.h, rgba(0, 190));
    double w = 640 * g.s, h = 200 * g.s, x = (g.w - w) / 2, y = (g.h - h) / 2;
    g.rounded(x, y, w, h, 12 * g.s, t.panel);
    g.rect(x, y, w, 4 * g.s, t.accent2);
    g.text(FontId::Display, 14 * g.s, x + w / 2, y + 30 * g.s, "PRESS A BUTTON", t.accent, 1, 1.5 * g.s);
    g.text_fit(FontId::Body, 30 * g.s, x + w / 2, y + 74 * g.s, w - 40 * g.s, capture_label, t.text, 1);
    g.text(FontId::Body, 21 * g.s, x + w / 2, y + 130 * g.s, "Key, controller button, stick or trigger.  Esc or wait 6 s to cancel.", t.text_dim, 1);
}

void Frontend::draw() {
    g.begin_frame();
    const Theme &t = theme();
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    bool ingame = mode == Mode::Playing || mode == Mode::Paused || mode == Mode::Arrange;
    if (ingame) {
        if (disp.set.border == "theme") draw_backdrop(0.55);
        else if (disp.set.border == "image") {
            if (SDL_Texture *w = disp.wallpaper_tex(ren)) {
                int tw, th; SDL_QueryTexture(w, nullptr, nullptr, &tw, &th);
                double sc = std::max((double)g.w / tw, (double)g.h / th);
                SDL_FRect r{(float)((g.w - tw * sc) / 2), (float)((g.h - th * sc) / 2), (float)(tw * sc), (float)(th * sc)};
                SDL_RenderCopyF(ren, w, nullptr, &r);
            } else draw_backdrop(0.55);
        }
        // paused: the game slides into a framed preview on the right, the menu takes the left
        static double pause_anim = 0;
        pause_anim = mode == Mode::Paused ? std::min(1.0, pause_anim + 0.12) : 0;
        SDL_Rect full = disp.layout(g.w, g.h), r = full;
        if (mode == Mode::Paused) {
            SDL_Rect pr = preview_rect();
            double k = ease_out(pause_anim);
            r = SDL_Rect{(int)(full.x + (pr.x - full.x) * k), (int)(full.y + (pr.y - full.y) * k), (int)(full.w + (pr.w - full.w) * k), (int)(full.h + (pr.h - full.h) * k)};
            g.rounded(r.x - 10 * g.s, r.y - 10 * g.s, r.w + 20 * g.s, r.h + 20 * g.s, 10 * g.s, with_alpha(t.panel, 0.9 * k));
            g.rect_outline(r.x - 2, r.y - 2, r.w + 4, r.h + 4, with_alpha(t.accent, 0.55 * k), 2);
        }
        disp.draw(g, r, mode == Mode::Paused ? 0.85 : 1.0);
        if (rewinding) g.text(FontId::Display, 16 * g.s, g.w - 30 * g.s, 24 * g.s, "<< REWIND", t.accent2, 2, 1);
        else if (ffwd) g.text(FontId::Display, 16 * g.s, g.w - 30 * g.s, 24 * g.s, fmt(">> x%d", ff_speed), t.accent, 2, 1);
        if (f_gadgets && mode != Mode::Paused) {
            GadgetContext gc; gc.fps = fps; gc.emu_ms = emu_ms; gc.playtime = playtime;
            gc.input = &in_game; gc.actions = &actions; gc.adapter = ad.get();
            gadgets.draw(g, t, gc, mode == Mode::Arrange ? arrange_idx % std::max<size_t>(1, gadgets.gadgets.size()) : -1);
        }
        if (mode == Mode::Paused) {
            g.gradient_h(0, 0, g.w * 0.55, g.h, with_alpha(t.bg0, 0.7), with_alpha(t.bg0, 0.0));
            Page *p = menu.top();
            double mw = std::min(g.w * 0.40, 560 * g.s);
            if (p && p->wide) mw = g.w * 0.40;
            menu.draw(g, t, g.w * 0.06, g.h * 0.07, mw, g.h * 0.80, time_s, input.pad_style_prompts);
            if (p && p->side) { g.rect(g.w * 0.5, 0, g.w * 0.5, g.h, with_alpha(t.bg0, 0.85)); p->side(g, t, g.w * 0.52, g.h * 0.2, g.w * 0.42, g.h * 0.6); }
            draw_prompts({{input.pad_style_prompts ? "A" : "Enter", "Select"}, {input.pad_style_prompts ? "B" : "Esc", menu.stack.size() > 1 ? "Back" : "Resume"}});
        } else if (mode == Mode::Arrange) {
            g.rounded(g.w * 0.25, g.h - 96 * g.s, g.w * 0.5, 56 * g.s, 10 * g.s, with_alpha(t.panel, 0.95));
            auto &gs = gadgets.gadgets;
            std::string nm = gs.empty() ? "" : gs[arrange_idx % gs.size()].label + (gs[arrange_idx % gs.size()].enabled ? "" : " (hidden)");
            g.text(FontId::Body, 24 * g.s, g.w / 2, g.h - 84 * g.s, "Arrange: " + nm, t.text, 1);
            draw_prompts({{input.pad_style_prompts ? "LB/RB" : "Q/E", "Next gadget"}, {input.pad_style_prompts ? "D-Pad" : "Arrows", "Move"},
                          {input.pad_style_prompts ? "X" : "C", "Size"}, {input.pad_style_prompts ? "A" : "Enter", "Show/Hide"}, {input.pad_style_prompts ? "B" : "Esc", "Done"}});
        }
    } else {
        // main menu / no-content
        if (disp.set.border == "image" && disp.wallpaper_tex(ren)) {
            SDL_Texture *w = disp.wallpaper_tex(ren); int tw, th; SDL_QueryTexture(w, nullptr, nullptr, &tw, &th);
            double sc = std::max((double)g.w / tw, (double)g.h / th);
            SDL_FRect r{(float)((g.w - tw * sc) / 2), (float)((g.h - th * sc) / 2), (float)(tw * sc), (float)(th * sc)};
            SDL_RenderCopyF(ren, w, nullptr, &r); g.rect(0, 0, g.w, g.h, rgba(0, 150));
        } else draw_backdrop(0);
        double lx = g.w * 0.06;
        draw_logo(lx, g.h * 0.07, g.w * 0.42);
        Page *p = menu.top();
        bool wide = p && p->wide;
        double mw = wide ? g.w * 0.86 : std::min(g.w * 0.40, 540 * g.s);
        if (!wide && mode == Mode::MainMenu && loaded) {
            SDL_Rect pr = preview_rect();
            g.rounded(pr.x - 10 * g.s, pr.y - 10 * g.s, pr.w + 20 * g.s, pr.h + 20 * g.s, 10 * g.s, with_alpha(t.panel, 0.9));
            g.rect_outline(pr.x - 2, pr.y - 2, pr.w + 4, pr.h + 4, with_alpha(t.accent, 0.55), 2);
            if (f_attract && disp.has_frame()) {
                disp.draw(g, pr, 0.92);
                g.text(FontId::Display, 9 * g.s, pr.x + 8 * g.s, pr.y + pr.h + 16 * g.s, "ATTRACT MODE PREVIEW", with_alpha(t.text_dim, 0.8), 0, 1);
            } else {
                g.gradient_v(pr.x, pr.y, pr.w, pr.h, t.bg1, t.bg0);
                g.text(FontId::Display, 18 * g.s, pr.x + pr.w / 2, pr.y + pr.h / 2, "READY", t.accent, 1, 2);
            }
        }
        double my = g.h * 0.07 + (multi ? 158 : 120) * g.s;
        if (p) {
            // wide pages with a side panel (save slots) keep the list clear of the preview
            double lw = (wide && p->side) ? mw * 0.60 : mw;
            menu.draw(g, t, lx, my, lw, g.h - my - 70 * g.s, time_s, input.pad_style_prompts);
            if (p->side && !wide) p->side(g, t, g.w * 0.52, g.h * 0.30, g.w * 0.42, g.h * 0.55);
            else if (p->side && wide) p->side(g, t, lx + mw * 0.64, my + 20 * g.s, mw * 0.34, g.h * 0.55);
        }
        draw_prompts({{input.pad_style_prompts ? "A" : "Enter", "Select"}, {input.pad_style_prompts ? "B" : "Esc", "Back"},
                      {input.pad_style_prompts ? "D-Pad" : "Arrows", "Navigate / adjust"}});
    }
    draw_capture();
    draw_dialog();
    draw_toasts();
    if (!draw_no_present) SDL_RenderPresent(ren);
}

// =====================================================================================================
// loop
// ---- QA automation (development aid, inert unless RF_AUTOTEST names a script file)
// Script lines: "<loop-iteration> key <SDL scancode name> [hold-iterations]" | "<n> shot <png path>" |
//               "<n> state" (prints mode/edition/menu to stdout) | "<n> quit". Lines must be in order.
struct AutoStep { long at; std::string cmd, arg; int hold; };
static std::vector<AutoStep> g_auto; static size_t g_auto_i; static long g_auto_n;
static std::vector<std::pair<long, SDL_Scancode>> g_auto_release;
static void auto_load() {
    const char *f = getenv("RF_AUTOTEST");
    if (!f) return;
    if (const char *lg = getenv("RF_AUTOTEST_LOG")) { if (!freopen(lg, "w", stdout)) {} }   // GUI builds have no console
    FILE *fp = fopen(f, "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof line, fp)) {
        AutoStep st{}; char cmd[64] = "", arg[900] = ""; int hold = 2;
        int n = sscanf(line, "%ld %63s %899s %d", &st.at, cmd, arg, &hold);
        if (n < 2) continue;
        st.cmd = cmd; st.arg = arg; st.hold = hold;
        if (st.cmd == "shot" || st.cmd == "type") { const char *q = strstr(line, cmd) + strlen(cmd); while (*q == ' ') q++; st.arg = q; while (!st.arg.empty() && (st.arg.back() == '\n' || st.arg.back() == '\r')) st.arg.pop_back(); }
        g_auto.push_back(st);
    }
    fclose(fp);
}
static void push_key(SDL_Scancode sc, bool down) {
    SDL_Event e{}; e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED; e.key.keysym.scancode = sc; e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    e.key.windowID = 0;
    if (sc < SDL_NUM_SCANCODES) g_rf_injected_keys[sc] = down;
    SDL_PushEvent(&e);
}

void Frontend::loop() {
    auto_load();
    Uint64 freq = SDL_GetPerformanceFrequency();
    double frame_ticks = freq / gi.refresh_hz;
    double next = (double)SDL_GetPerformanceCounter();
    Uint64 last = SDL_GetPerformanceCounter();
    uint32_t last_native_flush = SDL_GetTicks();
    while (running) {
        Uint64 nowc = SDL_GetPerformanceCounter();
        double dt = (nowc - last) / (double)freq; last = nowc;
        time_s += dt;
        menu.tick(dt);
        std::vector<UiEvent> ui;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: quit(); break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && mode == Mode::Playing && pause_on_focus) open_quick_menu();
                break;
            case SDL_DROPFILE: {
                std::string f = e.drop.file; SDL_free(e.drop.file);
                if (mode == Mode::NoContent || mode == Mode::MainMenu) { if (load_content(f)) { toast("Loaded " + platform::filename(f)); go_main_menu(); } else message("Could not load", content_error); }
                break;
            }
            case SDL_MOUSEMOTION: mouse_t = SDL_GetTicks(); if (!mouse_visible) { SDL_ShowCursor(SDL_ENABLE); mouse_visible = true; }
                if (mode != Mode::Playing && !dlg.active) {
                    int ww_, wh_; SDL_GetWindowSize(win, &ww_, &wh_);
                    menu.mouse(e.motion.x * g.w / std::max(1, ww_), e.motion.y * g.h / std::max(1, wh_), false, 0);
                }
                input.pad_style_prompts = false; break;
            case SDL_MOUSEBUTTONDOWN: {
                int ww_, wh_; SDL_GetWindowSize(win, &ww_, &wh_);
                int mx = e.button.x * g.w / std::max(1, ww_), my = e.button.y * g.h / std::max(1, wh_);
                if (dlg.active) { if (e.button.button == SDL_BUTTON_LEFT) ui.push_back({UiKey::Accept}); else ui.push_back({UiKey::Back}); break; }
                if (mode == Mode::Playing) break;
                if (e.button.button == SDL_BUTTON_RIGHT) ui.push_back({UiKey::Back});
                else if (e.button.button == SDL_BUTTON_LEFT) menu.mouse(mx, my, true, 0);
                break;
            }
            case SDL_MOUSEWHEEL: if (mode != Mode::Playing && !dlg.active) menu.mouse(0, 0, false, e.wheel.y); break;
            case SDL_KEYDOWN:
                if (e.key.keysym.scancode == SDL_SCANCODE_F11 && !e.key.repeat) { disp.set.fullscreen = !disp.set.fullscreen; apply_display(); save_settings(); break; }
                if (mode == Mode::Playing && !e.key.repeat && (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE || e.key.keysym.scancode == SDL_SCANCODE_PAUSE)) { open_quick_menu(); continue; }
                break;
            default: break;
            }
            input.handle_event(e, ui);
        }
        if (input.capturing) {
            Binding b;
            if (input.capture_done(b)) {
                if (b.kind != Binding::None) {
                    auto &v = input.bindings[capture_action];
                    // replace the existing binding of the same family (keyboard vs controller), keep the other
                    auto fam = [](const Binding &x) { return x.kind == Binding::Key ? 0 : x.kind == Binding::Button ? 1 : 2; };
                    v.erase(std::remove_if(v.begin(), v.end(), [&](const Binding &x) { return fam(x) == fam(b); }), v.end());
                    // a physical input drives one action per player: unbind it elsewhere
                    for (size_t i = 0; i < input.bindings.size(); i++) if (i != capture_action && actions[i].player == actions[capture_action].player)
                        input.bindings[i].erase(std::remove(input.bindings[i].begin(), input.bindings[i].end(), b), input.bindings[i].end());
                    v.push_back(b);
                    save_settings();
                }
                ui.clear();
            }
        }
        if (mode != Mode::Playing) input.tick_ui(ui);
        if (mode == Mode::Playing && input.menu_combo_pressed()) open_quick_menu();
        else if (mode != Mode::Playing) input.menu_combo_pressed();   // keep latch current
        for (auto &u : ui) handle_ui(u);
        // hide the mouse in fullscreen / during play
        if (mouse_visible && SDL_GetTicks() - mouse_t > 2500 && (mode == Mode::Playing || disp.set.fullscreen)) { SDL_ShowCursor(SDL_DISABLE); mouse_visible = false; }

        // ---- emulate
        double now = (double)SDL_GetPerformanceCounter();
        if (now - next > frame_ticks * 6) next = now;
        int frames = 0;
        while (now >= next && frames < 3) {
            if (mode == Mode::Playing) {
                if (rewinding) {
                    if (saves.rewind_step(*ad)) run_game_frame(false, false);
                    else rewinding = false;
                    input.sample(in_full, frame_no);
                } else {
                    run_game_frame(true, !ffwd);
                    for (int i = 1; ffwd && i < ff_speed; i++) run_game_frame(true, false);
                    playtime += 1.0 / gi.refresh_hz;
                    saves.rewind_push(*ad, frame_no);
                }
                handle_hotkeys();
            } else if (mode == Mode::MainMenu && loaded && f_attract) {
                run_game_frame(false, preview_audio);
            }
            next += frame_ticks; frames++;
        }
        // stats
        Uint64 nc = SDL_GetPerformanceCounter();
        if (nc - fps_t0 > freq / 2) {
            double secs = (nc - fps_t0) / (double)freq;
            fps = fps_frames / secs; emu_ms = fps_frames ? emu_acc / fps_frames : 0;
            fps_frames = 0; emu_acc = 0; fps_t0 = nc;
        }
        if (SDL_GetTicks() - last_native_flush > 60000) { if (loaded) ad->flush_native_save(pref); last_native_flush = SDL_GetTicks(); }
        draw();
        if (!g_auto.empty()) {
            g_auto_n++;
            for (auto it = g_auto_release.begin(); it != g_auto_release.end();) { if (g_auto_n >= it->first) { push_key(it->second, false); it = g_auto_release.erase(it); } else ++it; }
            while (g_auto_i < g_auto.size() && g_auto[g_auto_i].at <= g_auto_n) {
                const AutoStep &st = g_auto[g_auto_i++];
                if (st.cmd == "key") {
                    SDL_Scancode sc = SDL_GetScancodeFromName(st.arg.c_str());
                    if (sc != SDL_SCANCODE_UNKNOWN) { push_key(sc, true); g_auto_release.push_back({g_auto_n + std::max(1, st.hold), sc}); }
                    else printf("[autotest] unknown key %s\n", st.arg.c_str());
                } else if (st.cmd == "shot") {
                    int w, h; SDL_GetRendererOutputSize(ren, &w, &h);
                    std::vector<uint32_t> px((size_t)w * h);
                    // the frame was presented already; re-render it to read it back deterministically
                    draw_no_present = true; draw(); draw_no_present = false;
                    if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4) == 0) write_png(st.arg, px.data(), w, h, w);
                    printf("[autotest] shot %s\n", st.arg.c_str());
                } else if (st.cmd == "state") {
                    Page *tp = menu.top();
                    printf("[autotest] n=%ld mode=%d edition=%s loaded=%d page=%s sel=%d status=", g_auto_n, (int)mode, edition().id.c_str(), (int)loaded,
                           tp ? tp->title.c_str() : "-", tp ? tp->sel : -1);
                    for (size_t i = 0; i < eds.size(); i++) printf("%s:%d ", eds[i].id.c_str(), ed_status[i].state);
#ifdef __linux__
                    { FILE *sm = fopen("/proc/self/statm", "r"); long pages = 0, rss = 0; if (sm) { if (fscanf(sm, "%ld %ld", &pages, &rss) != 2) rss = 0; fclose(sm); } printf("rss=%ldMB ", rss * 4 / 1024); }
#endif
                    printf("dialog=%s\n", dlg.active ? dlg.title.c_str() : "-");
                    fflush(stdout);
                } else if (st.cmd == "quit") quit();
            }
            fflush(stdout);
        }
        if (!disp.set.vsync && !frames) SDL_Delay(1);
    }
}
void Frontend::shutdown() {
    if (loaded) ad->flush_native_save(pref);
    save_settings();
    saves.free_textures();
    audio.close();
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

int run_frontend(std::unique_ptr<GameAdapter> adapter, const FrontendOptions &opt, int argc, char **argv) {
    // a single-game build is a one-edition build whose data folder is the game's own
    GameInfo info = adapter->info();
    auto holder = std::make_shared<std::unique_ptr<GameAdapter>>(std::move(adapter));
    MultiOptions m;
    m.app_id = info.id; m.app_title = info.title; m.version = opt.version;
    EditionEntry e;
    e.id = info.id; e.label = info.title; e.long_label = info.title; e.profile_json = opt.default_profile_json;
    e.make = [holder]() -> std::unique_ptr<GameAdapter> { return std::move(*holder); };   // created once
    m.editions.push_back(e);
    return run_frontend_multi(m, argc, argv);
}
int run_frontend_multi(const MultiOptions &mo, int argc, char **argv) {
    if (mo.editions.empty()) return 1;
    auto fe = std::make_unique<Frontend>();
    fe->eds = mo.editions;
    fe->app_id = mo.app_id; fe->app_title = mo.app_title;
    fe->opt.version = mo.version;
    int r = fe->init(argc, argv);
    if (r) return r;
    fe->loop();
    fe->shutdown();
    return 0;
}
} // namespace rf
