#include "virtual_input.h"
#include "full_sys.h"

#include "imgui.h"
#include "helpers/physical_io.h"
#include "helpers/enums.h"

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

static const ImVec4 COLOR_LIT    {0.90f, 0.68f, 0.05f, 1.0f};
static const ImVec4 COLOR_LIT_HV {0.98f, 0.78f, 0.18f, 1.0f};
static const ImVec4 COLOR_LIT_AC {1.00f, 0.88f, 0.30f, 1.0f};

// Global scale for all virtual controllers and keyboards.
// 1.0 = original sizes; 0.70 ≈ two-thirds (compact default).
static float g_virt_scale = 0.70f;

// Set a first-use-ever default position for a virtual input window.
// Controllers: P1 bottom-left, P2 bottom-right of the viewport.
// Keyboards: wider, positioned a bit higher.
static void vi_first_pos(int port, bool kbd = false)
{
    ImVec2 vp = ImGui::GetMainViewport()->Size;
    float x = kbd ? vp.x * 0.05f : (port <= 1 ? vp.x * 0.05f : vp.x * 0.55f);
    float y = kbd ? vp.y * 0.55f : vp.y * 0.65f;
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
}

void set_virtual_scale(float s) { g_virt_scale = s; }
float get_virtual_scale()       { return g_virt_scale; }

// Render one pressable key/button.
//   label        – displayed text (may include ##id suffix for uniqueness)
//   sz           – button size in pixels
//   lit          – true when the button is pressed from any real input source
//   virtual_state – reference to the persistent virtual-press flag for this key;
//                   updated from IsItemActive() so held mouse clicks persist
static void vkey(const char *label, ImVec2 sz, bool lit, u32 &virtual_state)
{
    bool show_lit = lit || (virtual_state != 0);
    if (show_lit) {
        ImGui::PushStyleColor(ImGuiCol_Button,        COLOR_LIT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_LIT_HV);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COLOR_LIT_AC);
    }
    ImGui::Button(label, sz);
    virtual_state = ImGui::IsItemActive() ? 1u : 0u;
    // Also apply to key_states immediately so the core sees it this frame
    // (caller is responsible for ORing virtual_state into state after this call)
    if (show_lit) ImGui::PopStyleColor(3);
}

// Toggle variant for modifier keys (Shift, Ctrl, C=).
// One click locks the key down; a second click releases it.
// Shows as lit whenever virtual_state is set, even between clicks.
static void vkey_toggle(const char *label, ImVec2 sz, bool lit, u32 &virtual_state)
{
    bool show_lit = lit || (virtual_state != 0);
    if (show_lit) {
        ImGui::PushStyleColor(ImGuiCol_Button,        COLOR_LIT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_LIT_HV);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COLOR_LIT_AC);
    }
    ImGui::Button(label, sz);
    if (ImGui::IsItemClicked())
        virtual_state = virtual_state ? 0u : 1u;
    if (show_lit) ImGui::PopStyleColor(3);
}

// Invisible spacer of given size (SameLine-compatible)
static void vgap(float w, float h = 0) {
    ImGui::Dummy(ImVec2(w, h > 0 ? h : ImGui::GetFrameHeight()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Controller helpers  (shared by all gamepad renderers)
// ─────────────────────────────────────────────────────────────────────────────

// Find a digital button by common_id (DBCID_co_* / DBCID_ch_*).
static HID_digital_button* find_btn(JSM_CONTROLLER &ctr, JKEYS id)
{
    for (auto &b : ctr.digital_buttons)
        if (b.common_id == id) return &b;
    return nullptr;
}

// Render a virtual controller button.
// btn == nullptr → invisible spacer of the same size (keeps layout intact).
// Automatically ORs virtual_state → state so the core sees it this frame.
static void vbtn(const char *label, ImVec2 sz, HID_digital_button *btn, int port)
{
    if (!btn) { vgap(sz.x, sz.y); return; }
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "%s##vb_%d_%d", label, port, (int)btn->common_id);
    vkey(lbl, sz, btn->state != 0, btn->virtual_state);
    btn->state |= btn->virtual_state;
}

// Render a chassis button (DBK_BUTTON or DBK_SWITCH).
// Routes through chassis_gui_pulse so the application input loop handles state.
//   DBK_SWITCH  → toggle on click  (chassis_gui_pulse fires once)
//   DBK_BUTTON  → held while mouse is down (chassis_gui_pulse fires every frame)
static void vchassis_btn(const char *label, ImVec2 sz,
                         HID_digital_button *btn, full_system *fsys)
{
    if (!btn) { vgap(sz.x, sz.y); return; }
    bool lit = btn->state != 0;
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "%s##vch_%d", label, (int)btn->common_id);
    if (lit) {
        ImGui::PushStyleColor(ImGuiCol_Button,        COLOR_LIT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_LIT_HV);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COLOR_LIT_AC);
    }
    ImGui::Button(lbl, sz);
    if (btn->kind == DBK_SWITCH) {
        if (ImGui::IsItemClicked())
            fsys->chassis_gui_pulse[(int)btn->common_id] = true;
    } else {
        if (ImGui::IsItemActive())
            fsys->chassis_gui_pulse[(int)btn->common_id] = true;
    }
    if (lit) ImGui::PopStyleColor(3);
}

// Render one row of the d-pad cross.
//   row 0 → top row    (indent + UP)
//   row 1 → middle row (LEFT + centre gap + RIGHT)
//   row 2 → bottom row (indent + DOWN)
// When trail_sl is true the call ends with ImGui::SameLine() so the caller
// can append extra buttons on the same horizontal line.
// Arrow labels use ASCII (^/v/</>)  so they render on any ImGui font.
static void render_dpad_row(JSM_CONTROLLER &ctr, int port, float sz,
                             int row, bool trail_sl = false)
{
    ImVec2 dsz{sz, sz};
    if (row == 0) {
        vgap(sz);  ImGui::SameLine();
        vbtn("^", dsz, find_btn(ctr, DBCID_co_up), port);
    } else if (row == 1) {
        vbtn("<", dsz, find_btn(ctr, DBCID_co_left),  port);  ImGui::SameLine();
        vgap(sz);  ImGui::SameLine();
        vbtn(">", dsz, find_btn(ctr, DBCID_co_right), port);
    } else {
        vgap(sz);  ImGui::SameLine();
        vbtn("v", dsz, find_btn(ctr, DBCID_co_down), port);
    }
    if (trail_sl) ImGui::SameLine();
}

// ─────────────────────────────────────────────────────────────────────────────
// NES controller
// ─────────────────────────────────────────────────────────────────────────────

static void render_nes_controller(JSM_CONTROLLER &ctr, int port)
{
    // Find buttons by common_id
    HID_digital_button *up     = nullptr;
    HID_digital_button *dn     = nullptr;
    HID_digital_button *lt     = nullptr;
    HID_digital_button *rt     = nullptr;
    HID_digital_button *a      = nullptr;
    HID_digital_button *b      = nullptr;
    HID_digital_button *start  = nullptr;
    HID_digital_button *select = nullptr;

    for (auto &db : ctr.digital_buttons) {
        switch (db.common_id) {
            case DBCID_co_up:     up     = &db; break;
            case DBCID_co_down:   dn     = &db; break;
            case DBCID_co_left:   lt     = &db; break;
            case DBCID_co_right:  rt     = &db; break;
            case DBCID_co_fire1:  a      = &db; break;
            case DBCID_co_fire2:  b      = &db; break;
            case DBCID_co_start:  start  = &db; break;
            case DBCID_co_select: select = &db; break;
            default: break;
        }
    }

    char title[32];
    snprintf(title, sizeof(title), "NES Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    // Button sizes – all scaled by g_virt_scale
    const float sc = g_virt_scale;
    const ImVec2 dpad_sz  {30*sc, 30*sc};
    const ImVec2 ab_sz    {36*sc, 36*sc};
    const ImVec2 mid_sz   {46*sc, 20*sc};

    // Layout (controller face-on):
    //   Row 0:        [UP]
    //   Row 1:  [LT] [   ] [RT]   [SEL][STA]   [B][A]
    //   Row 2:        [DN]

    float pad = ImGui::GetStyle().ItemSpacing.x;

    char up_id[16];  snprintf(up_id,  sizeof(up_id),  "##nes_up%d",  port);
    char dn_id[16];  snprintf(dn_id,  sizeof(dn_id),  "##nes_dn%d",  port);
    char lt_id[16];  snprintf(lt_id,  sizeof(lt_id),  "##nes_lt%d",  port);
    char rt_id[16];  snprintf(rt_id,  sizeof(rt_id),  "##nes_rt%d",  port);
    char a_id[16];   snprintf(a_id,   sizeof(a_id),   "A##nes_a%d",  port);
    char b_id[16];   snprintf(b_id,   sizeof(b_id),   "B##nes_b%d",  port);
    char sel_id[16]; snprintf(sel_id, sizeof(sel_id), "SEL##nes_sel%d", port);
    char sta_id[16]; snprintf(sta_id, sizeof(sta_id), "STA##nes_sta%d", port);

    // ── Row 0: UP only (centred over the d-pad cross) ────────────────────────
    vgap(dpad_sz.x);
    ImGui::SameLine();
    if (up) { vkey(up_id, dpad_sz, up->state != 0, up->virtual_state); }

    // ── Row 1: LT  [centre gap]  RT   |gap|  SEL STA  |gap|  B A ───────────
    if (lt) { vkey(lt_id, dpad_sz, lt->state != 0, lt->virtual_state); ImGui::SameLine(); }
    vgap(dpad_sz.x); ImGui::SameLine();
    if (rt) { vkey(rt_id, dpad_sz, rt->state != 0, rt->virtual_state); ImGui::SameLine(); }
    vgap(pad * 3); ImGui::SameLine();
    if (select) { vkey(sel_id, mid_sz, select->state != 0, select->virtual_state); ImGui::SameLine(); }
    if (start)  { vkey(sta_id, mid_sz, start->state  != 0, start->virtual_state);  ImGui::SameLine(); }
    vgap(pad * 3); ImGui::SameLine();
    if (b) { vkey(b_id, ab_sz, b->state != 0, b->virtual_state); ImGui::SameLine(); }
    if (a) { vkey(a_id, ab_sz, a->state != 0, a->virtual_state); }

    // ── Row 2: DN only ───────────────────────────────────────────────────────
    vgap(dpad_sz.x); ImGui::SameLine();
    if (dn) { vkey(dn_id, dpad_sz, dn->state != 0, dn->virtual_state); }

    // After all buttons, OR virtual_state into state so the core sees clicks
    for (auto &db : ctr.digital_buttons)
        db.state |= db.virtual_state;

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// SMS / Game Gear controller
// ─────────────────────────────────────────────────────────────────────────────
// SMS  : up/down/left/right + 1 + 2  (pause is a chassis button, not here)
// GG   : same + START on the controller itself (DBCID_co_start)
// SG-1000 : treated as SMS (no pause button variant)
//
// Layout:
//   row 0:  gap ↑
//   row 1:  ← gap →   gap   [1] [2]
//   row 2:  gap ↓           [START]  (GG only)

static void render_sms_controller(JSM_CONTROLLER &ctr, int port, bool is_gg)
{
    char title[32];
    snprintf(title, sizeof(title), is_gg ? "GG Controller %d" : "SMS Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float sc  = g_virt_scale;
    const float dp  = 30.0f * sc;
    const float bp  = 34.0f * sc;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 3;
    ImVec2 bsz{bp, bp};

    auto *b1  = find_btn(ctr, DBCID_co_fire1);
    auto *b2  = find_btn(ctr, DBCID_co_fire2);
    auto *sta = find_btn(ctr, DBCID_co_start);   // nullptr for SMS

    render_dpad_row(ctr, port, dp, 0);
    render_dpad_row(ctr, port, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("1", bsz, b1, port); ImGui::SameLine();
    vbtn("2", bsz, b2, port);
    render_dpad_row(ctr, port, dp, 2, is_gg && sta != nullptr);
    if (is_gg && sta) {
        // START sits below the 1/2 cluster
        ImVec2 stsz{bp * 2.0f + ImGui::GetStyle().ItemSpacing.x, bp * 0.7f};
        vgap(gap); ImGui::SameLine();
        vbtn("START", stsz, sta, port);
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Atari 2600 joystick
// ─────────────────────────────────────────────────────────────────────────────
// Simple: d-pad + single FIRE button.
// Chassis switches (difficulty, game select) are shown in the application's
// existing chassis panel — no need to duplicate them here.
//
// Layout:
//   row 0:  gap ↑
//   row 1:  ← gap →   gap   [FIRE]
//   row 2:  gap ↓

static void render_atari2600_controller(JSM_CONTROLLER &ctr, int port)
{
    char title[32];
    snprintf(title, sizeof(title), "Atari Joystick %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float sc  = g_virt_scale;
    const float dp  = 32.0f * sc;
    const float bp  = 38.0f * sc;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 3;
    ImVec2 fsz{bp, bp};

    render_dpad_row(ctr, port, dp, 0);
    render_dpad_row(ctr, port, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("FIRE", fsz, find_btn(ctr, DBCID_co_fire1), port);
    render_dpad_row(ctr, port, dp, 2);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Neo Geo controller  (AES / MVS)
// ─────────────────────────────────────────────────────────────────────────────
// Buttons: up/dn/lt/rt + A(fire1) B(fire2) C(fire3) D(fire4) + start + select
//
// Physical layout: C and D sit above A and B (top row / bottom row).
//
// Layout:
//   row 0:  gap ↑   [face_indent]   [C][D]
//   row 1:  ← gap →   [SEL][STA]   gap   [A][B]
//   row 2:  gap ↓
//
// face_indent aligns C/D directly above A/B by accounting for the dpad width
// difference between row 0 (2dp) and row 1 (3dp), plus the SEL/STA columns.

static void render_neogeo_controller(JSM_CONTROLLER &ctr, int port)
{
    char title[32];
    snprintf(title, sizeof(title), "Neo Geo Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float sc   = g_virt_scale;
    const float dp   = 30.0f * sc;
    const float bp   = 34.0f * sc;
    const float sp   = ImGui::GetStyle().ItemSpacing.x;
    const float gap  = sp * 3.0f;
    ImVec2 bsz{bp, bp};
    ImVec2 mid{46.0f * sc, 20.0f * sc};

    // face_indent: row 0 dpad is (2dp) vs row 1 (3dp) → correct by dp+sp,
    // then skip the gap+SEL+STA+gap that row 1 has before A/B.
    const float face_indent = 2.0f*gap + 2.0f*mid.x + 3.0f*sp;

    // row 0: C D directly above A B
    render_dpad_row(ctr, port, dp, 0, true);
    vgap(dp);          ImGui::SameLine(); // row 0 vs row 1 width correction
    vgap(face_indent); ImGui::SameLine(); // skip SEL/STA column
    vbtn("C", bsz, find_btn(ctr, DBCID_co_fire3), port); ImGui::SameLine();
    vbtn("D", bsz, find_btn(ctr, DBCID_co_fire4), port);

    // row 1
    render_dpad_row(ctr, port, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("SEL", mid, find_btn(ctr, DBCID_co_select), port); ImGui::SameLine();
    vbtn("STA", mid, find_btn(ctr, DBCID_co_start),  port); ImGui::SameLine();
    vgap(gap); ImGui::SameLine();
    vbtn("A", bsz, find_btn(ctr, DBCID_co_fire1), port); ImGui::SameLine();
    vbtn("B", bsz, find_btn(ctr, DBCID_co_fire2), port);

    render_dpad_row(ctr, port, dp, 2);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sega Genesis 6-button controller
// ─────────────────────────────────────────────────────────────────────────────
// Physical layout: top row X Y Z | MODE, bottom row A B C | START
// Mapped buttons: fire1=A fire2=B fire3=C fire4=X fire5=Y fire6=Z
//                 start=START  select=MODE
//
// Layout:
//   row 0:  gap ↑   [dp_fix]   gap   [X][Y][Z]  [MODE]
//   row 1:  ← gap →            gap   [A][B][C]  [STA]
//   row 2:  gap ↓
//
// dp_fix corrects the row 0 dpad width (2dp) to match row 1 (3dp) so that
// X/Y/Z/MODE land directly above A/B/C/STA.

static void render_genesis6_controller(JSM_CONTROLLER &ctr, int port)
{
    char title[32];
    snprintf(title, sizeof(title), "Genesis Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float sc  = g_virt_scale;
    const float dp  = 30.0f * sc;
    const float bp  = 32.0f * sc;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 3;
    const float sp  = ImGui::GetStyle().ItemSpacing.x;
    ImVec2 bsz{bp, bp};
    ImVec2 mid{46.0f * sc, 20.0f * sc};

    // row 0: X Y Z MODE — corrected to align with row 1
    render_dpad_row(ctr, port, dp, 0, true);
    vgap(dp); ImGui::SameLine(); // row 0 vs row 1 dpad width correction
    vgap(gap); ImGui::SameLine();
    vbtn("X", bsz, find_btn(ctr, DBCID_co_fire4), port); ImGui::SameLine();
    vbtn("Y", bsz, find_btn(ctr, DBCID_co_fire5), port); ImGui::SameLine();
    vbtn("Z", bsz, find_btn(ctr, DBCID_co_fire6), port); ImGui::SameLine();
    vgap(sp); ImGui::SameLine();
    vbtn("MODE", mid, find_btn(ctr, DBCID_co_select), port);

    // row 1: A B C STA
    render_dpad_row(ctr, port, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("A", bsz, find_btn(ctr, DBCID_co_fire1), port); ImGui::SameLine();
    vbtn("B", bsz, find_btn(ctr, DBCID_co_fire2), port); ImGui::SameLine();
    vbtn("C", bsz, find_btn(ctr, DBCID_co_fire3), port); ImGui::SameLine();
    vgap(sp); ImGui::SameLine();
    vbtn("STA", mid, find_btn(ctr, DBCID_co_start), port);

    // row 2: gap ↓
    render_dpad_row(ctr, port, dp, 2);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Game Boy / Game Boy Color controller
// ─────────────────────────────────────────────────────────────────────────────
// Buttons: up/dn/lt/rt + A(fire1) B(fire2) + start + select
// Physical layout mirrors NES: B A on the right, SEL STA in the centre.
//
// Layout:
//   row 0:  gap ↑
//   row 1:  ← gap →   gap   [SEL][STA]   gap   [B][A]
//   row 2:  gap ↓

static void render_gb_controller(JSM_CONTROLLER &ctr, int port)
{
    char title[32];
    snprintf(title, sizeof(title), "Game Boy Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float sc  = g_virt_scale;
    const float dp  = 30.0f * sc;
    const float bp  = 36.0f * sc;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 3;
    ImVec2 bsz{bp, bp};
    ImVec2 mid{46.0f * sc, 20.0f * sc};

    render_dpad_row(ctr, port, dp, 0);
    render_dpad_row(ctr, port, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("SEL", mid, find_btn(ctr, DBCID_co_select), port); ImGui::SameLine();
    vbtn("STA", mid, find_btn(ctr, DBCID_co_start),  port); ImGui::SameLine();
    vgap(gap); ImGui::SameLine();
    vbtn("B", bsz, find_btn(ctr, DBCID_co_fire2), port); ImGui::SameLine();
    vbtn("A", bsz, find_btn(ctr, DBCID_co_fire1), port);
    render_dpad_row(ctr, port, dp, 2);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Game Boy Advance controller
// ─────────────────────────────────────────────────────────────────────────────
// Buttons: up/dn/lt/rt + A(fire2) B(fire1) + L/R shoulders + start + select
// NOTE: GBA gba_controller.cpp maps A→fire2, B→fire1 (reversed from GB).
//
// Layout:
//   row 0:  gap ↑
//   row 1:  ← gap →   gap   [SEL][STA]   gap   [B][A]
//   row 2:  gap ↓
//   row 3:  [L]   ────(auto gap)────   [R]

static void render_gba_controller(JSM_CONTROLLER &ctr, int port)
{
    vi_first_pos(port);
    if (!ImGui::Begin("GBA Controller", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float sc  = g_virt_scale;
    const float dp  = 30.0f * sc;
    const float bp  = 36.0f * sc;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 3;
    ImVec2 bsz{bp, bp};
    ImVec2 mid{46.0f * sc, 20.0f * sc};
    ImVec2 sh_sz{50.0f * sc, 22.0f * sc};

    // GBA: A = fire2, B = fire1
    render_dpad_row(ctr, port, dp, 0);
    render_dpad_row(ctr, port, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("SEL", mid, find_btn(ctr, DBCID_co_select), port); ImGui::SameLine();
    vbtn("STA", mid, find_btn(ctr, DBCID_co_start),  port); ImGui::SameLine();
    vgap(gap); ImGui::SameLine();
    vbtn("B", bsz, find_btn(ctr, DBCID_co_fire1), port); ImGui::SameLine();
    vbtn("A", bsz, find_btn(ctr, DBCID_co_fire2), port);
    render_dpad_row(ctr, port, dp, 2);

    // Shoulder row: L on the far left, R on the far right.
    // Compute the gap from the known layout so it's correct on the first frame.
    // Row 1 width = dpad(3*dp + 2*sp) + gap(3*sp) + SEL + sp + STA + gap(3*sp) + B + sp + A
    {
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        float row_w = 3.0f*dp + 2.0f*sp
                    + gap
                    + mid.x + sp + mid.x
                    + gap
                    + bsz.x + sp + bsz.x;
        float sh_gap = row_w - 2.0f*sh_sz.x - sp;
        if (sh_gap < sp) sh_gap = sp;
        vbtn("L", sh_sz, find_btn(ctr, DBCID_co_shoulder_left),  port); ImGui::SameLine();
        vgap(sh_gap); ImGui::SameLine();
        vbtn("R", sh_sz, find_btn(ctr, DBCID_co_shoulder_right), port);
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Apple IIe joystick
// ─────────────────────────────────────────────────────────────────────────────
// Layout:
//   row 0:  gap ↑
//   row 1:  ← gap →   gap   [●] [○]
//   row 2:  gap ↓           OA  CA
//
// OA = Open Apple  (fire1 / PB0)
// CA = Closed Apple (fire2 / PB1)

static void render_apple2_joystick(JSM_CONTROLLER &ctr, int port)
{
    vi_first_pos(port);
    if (!ImGui::Begin("Apple IIe Joystick", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float sc  = g_virt_scale;
    const float dp  = 32.0f * sc;
    const float bp  = 38.0f * sc;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 3;
    ImVec2 fsz{bp, bp};

    render_dpad_row(ctr, port, dp, 0);
    render_dpad_row(ctr, port, dp, 1, true);         // trailing SameLine after →
    vgap(gap); ImGui::SameLine();
    vbtn("OA",  fsz, find_btn(ctr, DBCID_co_fire1), port); ImGui::SameLine();
    vbtn("CA",  fsz, find_btn(ctr, DBCID_co_fire2), port);
    render_dpad_row(ctr, port, dp, 2);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// C64 joystick
// ─────────────────────────────────────────────────────────────────────────────
// Standard 9-pin Atari-style joystick: d-pad + single FIRE button.
// The C64 has two ports; port_num is 1 or 2 (from physical_io_device::id).
//
// Layout:
//   row 0:  gap ↑
//   row 1:  ← gap →   gap   [FIRE]
//   row 2:  gap ↓

static void render_c64_joystick(JSM_CONTROLLER &ctr, int port_num)
{
    char title[40];
    snprintf(title, sizeof(title), "C64 Joystick Port %d", port_num);
    vi_first_pos(port_num);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float sc  = g_virt_scale;
    const float dp  = 32.0f * sc;
    const float bp  = 38.0f * sc;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 3;
    ImVec2 fsz{bp, bp};

    render_dpad_row(ctr, port_num, dp, 0);
    render_dpad_row(ctr, port_num, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("FIRE", fsz, find_btn(ctr, DBCID_co_fire1), port_num);
    render_dpad_row(ctr, port_num, dp, 2);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// C64 keyboard
// ─────────────────────────────────────────────────────────────────────────────

// ── Generic virtual-keyboard key descriptor ───────────────────────────────────
// Shared by all keyboard renderers (C64, ZX Spectrum, Apple IIe, …).
//   idx           – index into kbd.key_states[] / kbd.virtual_key_states[]
//   label         – text shown on the key face
//   width_mul     – key width as a multiple of the base key width
//   toggle        – true for modifier keys (click to lock; click again to release)
//   shifted_label – label shown when shift is active (nullptr = unchanged)
struct VKey {
    int        idx;
    const char *label;
    float      width_mul;
    bool       toggle        = false;
    const char *shifted_label = nullptr;  // symbol-shift / regular-shift label
    const char *caps_label    = nullptr;  // caps-shift label (ZX Spectrum only)
};

// Render one row of virtual keys.
// caps_active/sym_active control which label variant is shown (caps_label takes
// priority over shifted_label).  Pass false,false for keyboards with only one
// shift key — use false,shift_active to drive shifted_label the usual way.
// port is used to make ImGui IDs unique across simultaneous keyboard windows.
// IDs are window-scoped in ImGui, so idx+port uniqueness is sufficient.
static void vkbd_row(JSM_KEYBOARD &kbd,
                     const VKey *keys, int n,
                     float key_w, float key_h, int port,
                     bool caps_active, bool sym_active)
{
    for (int i = 0; i < n; i++) {
        const VKey &k = keys[i];
        if (i > 0) ImGui::SameLine();
        ImVec2 sz{key_w * k.width_mul, key_h};
        if (k.idx < 0) {           // negative index = invisible spacer
            vgap(sz.x, sz.y);
            continue;
        }
        bool lit = kbd.key_states[k.idx] != 0;
        // caps_label takes priority over shifted_label (symbol/shift).
        const char *display_label = k.label;
        if (caps_active && k.caps_label)        display_label = k.caps_label;
        else if (sym_active && k.shifted_label) display_label = k.shifted_label;
        char lbl[32];
        snprintf(lbl, sizeof(lbl), "%s##vk_%d_%d", display_label, port, k.idx);
        if (k.toggle)
            vkey_toggle(lbl, sz, lit, kbd.virtual_key_states[k.idx]);
        else
            vkey(lbl, sz, lit, kbd.virtual_key_states[k.idx]);
        kbd.key_states[k.idx] |= kbd.virtual_key_states[k.idx];
    }
}

static void render_c64_keyboard(JSM_KEYBOARD &kbd, int port)
{
    char title[32];
    snprintf(title, sizeof(title), "C64 Keyboard %d", port);
    vi_first_pos(port, true);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    const float KEY_W = 28.0f * g_virt_scale;
    const float KEY_H = 26.0f * g_virt_scale;

    // Shift active when either physical key or virtual toggle is on.
    // Matrix: left SHIFT = index 15, right SHIFT = index 52.
    // key_states already incorporates virtual_key_states from the input loop,
    // but OR both explicitly to be safe when called mid-frame.
    bool shift_active = (kbd.key_states[15] | kbd.virtual_key_states[15] |
                         kbd.key_states[52] | kbd.virtual_key_states[52]) != 0;

    // On a real C64 the F-keys form a vertical column on the far right:
    //   F1 aligned with the number row, F3 with CTRL row, F5 with STOP/A row, F7 with C=/Z row.
    // We render each main row, then SameLine + SetCursorPos to jump to the F-column.
    // f_key_x is captured from row 0 so all F keys share the same X.
    float f_key_x = 0.0f;

    // ── Row 0: ← 1 2 3 4 5 6 7 8 9 0 + - £ CLR DEL │ F1 ────────────────
    // Matrix indices: see C64_keyboard_keymap in c64_keyboard.cpp
    // (7,1)=57:← (7,0)=56:1 (7,3)=59:2 (1,0)=8:3 (1,3)=11:4
    // (2,0)=16:5 (2,3)=19:6 (3,0)=24:7 (3,3)=27:8 (4,0)=32:9
    // (4,3)=35:0 (5,0)=40:+ (5,3)=43:- (6,0)=48:£ (6,3)=51:CLR (0,0)=0:DEL
    // F1=(0,4)=4  F3=(0,5)=5  F5=(0,6)=6  F7=(0,3)=3
    {
        static const VKey row[] = {
            {57,"←",   1},
            {56,"1",   1, false, "!"},
            {59,"2",   1, false, "\""},
            { 8,"3",   1, false, "#"},
            {11,"4",   1, false, "$"},
            {16,"5",   1, false, "%"},
            {19,"6",   1, false, "&"},
            {24,"7",   1, false, "'"},
            {27,"8",   1, false, "("},
            {32,"9",   1, false, ")"},
            {35,"0",   1},
            {40,"+",   1},
            {43,"-",   1},
            {48,"£",   1},
            {51,"CLR", 1.3f, false, "HOME"},
            { 0,"DEL", 1.3f, false, "INST"},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, false, shift_active);
        ImGui::SameLine();
        vgap(KEY_W * 0.5f, KEY_H);
        ImGui::SameLine();
        f_key_x = ImGui::GetCursorPos().x; // anchor column for all F keys
        static const VKey fkey[] = {{4,"F1",1,false,"F2"}};
        vkbd_row(kbd, fkey, 1, KEY_W, KEY_H, port, false, shift_active);
    }

    // ── Row 1: CTRL Q W E R T Y U I O P @ * ↑ │ F3 ──────────────────────
    // (7,2)=58:CTRL (7,6)=62:Q (1,1)=9:W (1,6)=14:E (2,1)=17:R
    // (2,6)=22:T (3,1)=25:Y (3,6)=30:U (4,1)=33:I (4,6)=38:O
    // (5,1)=41:P (5,6)=46:@ (6,1)=49:* (6,6)=54:↑
    {
        static const VKey row[] = {
            {58,"CTRL",1.5f, true},
            {62,"Q",   1, false, "q"},
            { 9,"W",   1, false, "w"},
            {14,"E",   1, false, "e"},
            {17,"R",   1, false, "r"},
            {22,"T",   1, false, "t"},
            {25,"Y",   1, false, "y"},
            {30,"U",   1, false, "u"},
            {33,"I",   1, false, "i"},
            {38,"O",   1, false, "o"},
            {41,"P",   1, false, "p"},
            {46,"@",   1, false, "♠"},
            {49,"*",   1},
            {54,"↑",   1, false, "π"},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, false, shift_active);
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(f_key_x, ImGui::GetCursorPos().y));
        static const VKey fkey[] = {{5,"F3",1,false,"F4"}};
        vkbd_row(kbd, fkey, 1, KEY_W, KEY_H, port, false, shift_active);
    }

    // ── Row 2: STOP A S D F G H J K L : ; = RETURN │ F5 ─────────────────
    // (7,7)=63:STOP (1,2)=10:A (1,5)=13:S (2,2)=18:D (2,5)=21:F
    // (3,2)=26:G (3,5)=29:H (4,2)=34:J (4,5)=37:K (5,2)=42:L
    // (5,5)=45:: (6,2)=50:; (6,5)=53:= (0,1)=1:RETURN
    {
        static const VKey row[] = {
            {63,"STOP",1.5f},
            {10,"A",   1, false, "a"},
            {13,"S",   1, false, "s"},
            {18,"D",   1, false, "d"},
            {21,"F",   1, false, "f"},
            {26,"G",   1, false, "g"},
            {29,"H",   1, false, "h"},
            {34,"J",   1, false, "j"},
            {37,"K",   1, false, "k"},
            {42,"L",   1, false, "l"},
            {45,":",   1, false, "["},
            {50,";",   1, false, "]"},
            {53,"=",   1},
            { 1,"RET", 1.8f},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, false, shift_active);
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(f_key_x, ImGui::GetCursorPos().y));
        static const VKey fkey[] = {{6,"F5",1,false,"F6"}};
        vkbd_row(kbd, fkey, 1, KEY_W, KEY_H, port, false, shift_active);
    }

    // ── Row 3: C= SHIFT Z X C V B N M , . / SHIFT │ F7 ─────────────────
    // (7,5)=61:C= (1,7)=15:LSHIFT Z=(1,4)=12 X=(2,7)=23 C=(2,4)=20
    // V=(3,7)=31 B=(3,4)=28 N=(4,7)=39 M=(4,4)=36
    // ","=(5,7)=47 "."=(5,4)=44 "/"=(6,7)=55 RSHIFT=(6,4)=52
    {
        static const VKey row[] = {
            {61,"C=",  1.3f, true},
            {15,"SHFT",1.5f, true},
            {12,"Z",   1, false, "z"},
            {23,"X",   1, false, "x"},
            {20,"C",   1, false, "c"},
            {31,"V",   1, false, "v"},
            {28,"B",   1, false, "b"},
            {39,"N",   1, false, "n"},
            {36,"M",   1, false, "m"},
            {47,",",   1, false, "<"},
            {44,".",   1, false, ">"},
            {55,"/",   1, false, "?"},
            {52,"SHFT",1.7f, true},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, false, shift_active);
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(f_key_x, ImGui::GetCursorPos().y));
        static const VKey fkey[] = {{3,"F7",1,false,"F8"}};
        vkbd_row(kbd, fkey, 1, KEY_W, KEY_H, port, false, shift_active);
    }

    // ── Row 4: SPACE + cursor cluster ────────────────────────────────────
    // SPACE=(7,4)=60, CRSR↓=(0,7)=7, CRSR→=(0,2)=2
    // Cursor keys reverse direction when shifted (↓→↑, →→←)
    {
        static const VKey spc[] = {{60,"SPACE",7.0f}};
        vkbd_row(kbd, spc, 1, KEY_W, KEY_H, port, false, shift_active);
        ImGui::SameLine();
        vgap(KEY_W * 3.5f, KEY_H); // gap to align cursor keys roughly to the right
        ImGui::SameLine();
        static const VKey csr[] = {{7,"v",1,false,"^"},{2,">",1,false,"<"}};
        vkbd_row(kbd, csr, 2, KEY_W, KEY_H, port, false, shift_active);
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// ZX Spectrum keyboard
// ─────────────────────────────────────────────────────────────────────────────
// The 40 keys are stored as 8 half-rows of 5.  The physical display puts the
// left half (indices increasing) on the left and the right half (indices
// decreasing) on the right, separated by a small visual gap.
//
// Keymap layout (from ZXSpectrum_keyboard_keymap[]):
//   [0-4]  : 1 2 3 4 5       [5-9]  : 0 9 8 7 6  (stored reversed)
//   [10-14]: Q W E R T       [15-19]: P O I U Y  (stored reversed)
//   [20-24]: A S D F G       [25-29]: ENT L K J H (stored reversed)
//   [30-34]: CAPS Z X C V    [35-39]: SPC SYM M N B (stored reversed)
//
// idx 30 = CAPS SHIFT (toggle), idx 36 = SYMBOL SHIFT (toggle)

static void render_zxspectrum_keyboard(JSM_KEYBOARD &kbd, int port)
{
    char title[32];
    snprintf(title, sizeof(title), "ZX Spectrum Keyboard %d", port);
    vi_first_pos(port, true);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    const float KEY_W = 28.0f * g_virt_scale;
    const float KEY_H = 26.0f * g_virt_scale;

    // CAPS SHIFT (idx 30) and SYMBOL SHIFT (idx 36) control labels independently:
    //   caps_active → show caps_label when defined (editing/cursor functions on number keys)
    //   sym_active  → show shifted_label when defined (magenta symbols on keycaps)
    bool caps_active = (kbd.key_states[30] | kbd.virtual_key_states[30]) != 0;
    bool sym_active  = (kbd.key_states[36] | kbd.virtual_key_states[36]) != 0;

    // ── Row 0: 1 2 3 4 5 | 6 7 8 9 0 ──────────────────────────────────────
    // SYMBOL SHIFT: ! @ # $ %  &  '  (  )  _
    // CAPS  SHIFT : EDIT CAPLK TVID IVID ←  ↓  ↑  →  GFX DEL
    {
        static const VKey row[] = {
            {0,"1",1,false,"!","EDIT"},
            {1,"2",1,false,"@","CAPLK"},
            {2,"3",1,false,"#","TVID"},
            {3,"4",1,false,"$","IVID"},
            {4,"5",1,false,"%","←"},
            {-1,"",0.4f},
            {9,"6",1,false,"&","↓"},
            {8,"7",1,false,"'","↑"},
            {7,"8",1,false,"(","→"},
            {6,"9",1,false,")","GFX"},
            {5,"0",1,false,"_","DEL"},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, caps_active, sym_active);
    }

    // ── Row 1: Q W E R T | Y U I O P ───────────────────────────────────────
    // SYMBOL SHIFT: <= <> >= <  >  [  ]     ;  "
    {
        static const VKey row[] = {
            {10,"Q",1,false,"<="},
            {11,"W",1,false,"<>"},
            {12,"E",1,false,">="},
            {13,"R",1,false,"<"},
            {14,"T",1,false,">"},
            {-1,"",0.4f},
            {19,"Y",1,false,"["},
            {18,"U",1,false,"]"},
            {17,"I",1},
            {16,"O",1,false,";"},
            {15,"P",1,false,"\""},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, caps_active, sym_active);
    }

    // ── Row 2: A S D F G | H J K L ENTER ───────────────────────────────────
    // SYMBOL SHIFT: ~  |  \  {  }  ^  -  +  =
    {
        static const VKey row[] = {
            {20,"A",1,false,"~"},
            {21,"S",1,false,"|"},
            {22,"D",1,false,"\\"},
            {23,"F",1,false,"{"},
            {24,"G",1,false,"}"},
            {-1,"",0.4f},
            {29,"H",1,false,"^"},
            {28,"J",1,false,"-"},
            {27,"K",1,false,"+"},
            {26,"L",1,false,"="},
            {25,"ENT",1.3f},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, caps_active, sym_active);
    }

    // ── Row 3: CAPS Z X C V | B N M SYM SPACE ──────────────────────────────
    // SYMBOL SHIFT: :  £  ?  /  *  ,  .
    // CAPS  SHIFT : SPACE → BRK
    {
        static const VKey row[] = {
            {30,"CAP",1.3f,true},
            {31,"Z",1,false,":"},
            {32,"X",1,false,"£"},
            {33,"C",1,false,"?"},
            {34,"V",1,false,"/"},
            {-1,"",0.4f},
            {39,"B",1,false,"*"},
            {38,"N",1,false,","},
            {37,"M",1,false,"."},
            {36,"SYM",1.3f,true},
            {35,"SPC",1.3f,false,nullptr,"BRK"},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, caps_active, sym_active);
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Apple IIe keyboard
// ─────────────────────────────────────────────────────────────────────────────
// Keymap (from apple2_keyboard_keymap[]) uses the same half-row mirroring as
// ZX Spectrum for the alpha keys, with extra punctuation/modifier keys at the
// end of the array.
//
// Number row indices (left half forward, right half reversed):
//   1(0) 2(1) 3(2) 4(3) 5(4)  |  6(9) 7(8) 8(7) 9(6) 0(5)
// Top row:  Q(10)..T(14) | Y(19)..P(15)
// Home row: A(20)..G(24) | H(28)..L(25)
// Bottom:   Z(29)..V(32) | B(35) N(34) M(33)
// Punctuation: ;(45) -(46) =(47) ,(48) /(49) .(50) '(51) [(53) ](54) \(55)
// Modifiers: SPC(36) ESC(37) CTRL(38) SHFT(39) RSHFT(40) ←(41) →(42)
//            RET(43) DEL(44) ↑(56) ↓(57) TAB(58) CAPS(60) OA(61) CA(62)

static void render_apple2_keyboard(JSM_KEYBOARD &kbd, int port)
{
    char title[32];
    snprintf(title, sizeof(title), "Apple IIe Keyboard %d", port);
    vi_first_pos(port, true);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    const float KEY_W = 26.0f * g_virt_scale;
    const float KEY_H = 26.0f * g_virt_scale;

    bool shift_active = (kbd.key_states[39] | kbd.virtual_key_states[39] |
                         kbd.key_states[40] | kbd.virtual_key_states[40]) != 0;

    // ── Row 0: ESC 1 2 3 4 5 6 7 8 9 0 - = DEL ─────────────────────────────
    {
        static const VKey row[] = {
            {37,"ESC", 1},
            { 0,"1",   1, false, "!"},
            { 1,"2",   1, false, "@"},
            { 2,"3",   1, false, "#"},
            { 3,"4",   1, false, "$"},
            { 4,"5",   1, false, "%"},
            { 9,"6",   1, false, "^"},
            { 8,"7",   1, false, "&"},
            { 7,"8",   1, false, "*"},
            { 6,"9",   1, false, "("},
            { 5,"0",   1, false, ")"},
            {46,"-",   1, false, "_"},
            {47,"=",   1, false, "+"},
            {44,"DEL", 1.5f},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, false, shift_active);
    }

    // ── Row 1: TAB Q W E R T Y U I O P [ ] \ ───────────────────────────────
    {
        static const VKey row[] = {
            {58,"TAB", 1.5f},
            {10,"Q",   1},
            {11,"W",   1},
            {12,"E",   1},
            {13,"R",   1},
            {14,"T",   1},
            {19,"Y",   1},
            {18,"U",   1},
            {17,"I",   1},
            {16,"O",   1},
            {15,"P",   1},
            {53,"[",   1, false, "{"},
            {54,"]",   1, false, "}"},
            {55,"\\",  1, false, "|"},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, false, shift_active);
    }

    // ── Row 2: CTRL A S D F G H J K L ; ' RET ──────────────────────────────
    {
        static const VKey row[] = {
            {38,"CTRL",1.5f, true},
            {20,"A",   1},
            {21,"S",   1},
            {22,"D",   1},
            {23,"F",   1},
            {24,"G",   1},
            {28,"H",   1},
            {27,"J",   1},
            {26,"K",   1},
            {25,"L",   1},
            {45,";",   1, false, ":"},
            {51,"'",   1, false, "\""},
            {43,"RET", 2.0f},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, false, shift_active);
    }

    // ── Row 3: SHIFT Z X C V B N M , . / RSHIFT UP ─────────────────────────
    {
        static const VKey row[] = {
            {39,"SHFT",1.7f, true},
            {29,"Z",   1},
            {30,"X",   1},
            {31,"C",   1},
            {32,"V",   1},
            {35,"B",   1},
            {34,"N",   1},
            {33,"M",   1},
            {48,",",   1, false, "<"},
            {50,".",   1, false, ">"},
            {49,"/",   1, false, "?"},
            {40,"SHFT",1.5f, true},
            {56,"^",   1},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, false, shift_active);
    }

    // ── Row 4: CAPS OA SPACE CA ← ↓ → ─────────────────────────────────────
    {
        static const VKey row[] = {
            {60,"CAPS",1.5f, true},
            {61,"OA",  1.3f, true},
            {36,"SPACE",6.0f},
            {62,"CA",  1.3f, true},
            {41,"<",   1},
            {57,"v",   1},
            {42,">",   1},
        };
        vkbd_row(kbd, row, (int)(sizeof(row)/sizeof(row[0])), KEY_W, KEY_H, port, false, shift_active);
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// TurboGrafx-16 controller
// ─────────────────────────────────────────────────────────────────────────────
// Buttons: d-pad  +  II (fire2)  I (fire1)  SELECT (select)  RUN (start)
// I is the rightmost face button; II is to its left (mirrors the physical pad).
//
// Layout:
//   row 0:  gap ^
//   row 1:  ← gap →   [SEL][RUN]   [II][I]
//   row 2:  gap ↓

static void render_tg16_controller(JSM_CONTROLLER &ctr, int port)
{
    const float sc  = g_virt_scale;
    const float dp  = 30.0f * sc;
    const ImVec2 dsz{dp, dp};
    const ImVec2 msz{46.0f*sc, 20.0f*sc};
    const ImVec2 bsz{36.0f*sc, 36.0f*sc};

    char title[32]; snprintf(title, sizeof(title), "TG16 Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float gap = ImGui::GetStyle().ItemSpacing.x * 3.0f;

    render_dpad_row(ctr, port, dp, 0);                                    // row 0: ^
    render_dpad_row(ctr, port, dp, 1, /*trail_sl=*/true);                 // row 1: < _ >
    vgap(gap); ImGui::SameLine();
    vbtn("SEL", msz, find_btn(ctr, DBCID_co_select), port); ImGui::SameLine();
    vbtn("RUN", msz, find_btn(ctr, DBCID_co_start),  port); ImGui::SameLine();
    vgap(gap); ImGui::SameLine();
    vbtn("II",  bsz, find_btn(ctr, DBCID_co_fire2),  port); ImGui::SameLine();
    vbtn("I",   bsz, find_btn(ctr, DBCID_co_fire1),  port);
    render_dpad_row(ctr, port, dp, 2);                                    // row 2: v

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// SNES controller
// ─────────────────────────────────────────────────────────────────────────────
// Face buttons: B (fire2)  Y (fire4)  A (fire1)  X (fire3)
// Shoulders:    L (shoulder_left)  R (shoulder_right)
//
// Layout:
//   shoulder:  [L]  gap  [R]
//   row 0:  gap ^  [dp_fix][face_indent]  [Y][X]
//   row 1:  ← gap →  [SEL][STA]  gap  [B][A]
//   row 2:  gap ↓
//
// dp_fix + face_indent align Y/X directly above B/A.

static void render_snes_controller(JSM_CONTROLLER &ctr, int port)
{
    const float sc  = g_virt_scale;
    const float dp  = 30.0f * sc;
    const ImVec2 msz {46.0f*sc, 20.0f*sc};
    const ImVec2 bsz {36.0f*sc, 36.0f*sc};
    const ImVec2 shsz{40.0f*sc, 18.0f*sc};
    const float  sp  = ImGui::GetStyle().ItemSpacing.x;
    const float  gap = sp * 3.0f;
    // face_indent: advances past the SEL+STA columns so Y lands above B
    const float  face_indent = 2.0f*gap + 2.0f*msz.x + 3.0f*sp;

    char title[32]; snprintf(title, sizeof(title), "SNES Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    // shoulder row: [L] ... [R]
    {
        float row_w = 3.0f*dp + 2.0f*sp + gap + msz.x + sp + msz.x + gap + bsz.x + sp + bsz.x;
        float sh_gap = row_w - 2.0f*shsz.x - sp;
        if (sh_gap < sp) sh_gap = sp;
        vbtn("L", shsz, find_btn(ctr, DBCID_co_shoulder_left),  port); ImGui::SameLine();
        vgap(sh_gap); ImGui::SameLine();
        vbtn("R", shsz, find_btn(ctr, DBCID_co_shoulder_right), port);
    }

    // row 0: Y X above B A
    render_dpad_row(ctr, port, dp, 0, true);
    vgap(dp);          ImGui::SameLine(); // row 0 dpad (2dp) → row 1 dpad (3dp) correction
    vgap(face_indent); ImGui::SameLine(); // skip SEL/STA column
    vbtn("Y", bsz, find_btn(ctr, DBCID_co_fire4), port); ImGui::SameLine();
    vbtn("X", bsz, find_btn(ctr, DBCID_co_fire3), port);

    // row 1
    render_dpad_row(ctr, port, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("SEL", msz, find_btn(ctr, DBCID_co_select), port); ImGui::SameLine();
    vbtn("STA", msz, find_btn(ctr, DBCID_co_start),  port); ImGui::SameLine();
    vgap(gap); ImGui::SameLine();
    vbtn("B", bsz, find_btn(ctr, DBCID_co_fire2), port); ImGui::SameLine();
    vbtn("A", bsz, find_btn(ctr, DBCID_co_fire1), port);

    render_dpad_row(ctr, port, dp, 2);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Nintendo DS controller
// ─────────────────────────────────────────────────────────────────────────────
// Same button set as SNES but A/B are swapped (A=fire2, B=fire1, X=fire4, Y=fire3).
// Touch screen is handled separately; only the digital buttons are shown here.
//
// Layout:
//   shoulder:  [L]  gap  [R]
//   row 0:  gap ^  [dp_fix][face_indent]  [Y][X]
//   row 1:  ← gap →  [SEL][STA]  gap  [B][A]
//   row 2:  gap ↓

static void render_nds_controller(JSM_CONTROLLER &ctr, int port)
{
    const float sc  = g_virt_scale;
    const float dp  = 30.0f * sc;
    const ImVec2 msz {46.0f*sc, 20.0f*sc};
    const ImVec2 bsz {36.0f*sc, 36.0f*sc};
    const ImVec2 shsz{40.0f*sc, 18.0f*sc};
    const float  sp  = ImGui::GetStyle().ItemSpacing.x;
    const float  gap = sp * 3.0f;
    const float  face_indent = 2.0f*gap + 2.0f*msz.x + 3.0f*sp;

    char title[32]; snprintf(title, sizeof(title), "NDS Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    // shoulder row: [L] ... [R]
    {
        float row_w = 3.0f*dp + 2.0f*sp + gap + msz.x + sp + msz.x + gap + bsz.x + sp + bsz.x;
        float sh_gap = row_w - 2.0f*shsz.x - sp;
        if (sh_gap < sp) sh_gap = sp;
        vbtn("L", shsz, find_btn(ctr, DBCID_co_shoulder_left),  port); ImGui::SameLine();
        vgap(sh_gap); ImGui::SameLine();
        vbtn("R", shsz, find_btn(ctr, DBCID_co_shoulder_right), port);
    }

    // row 0: Y X above B A
    render_dpad_row(ctr, port, dp, 0, true);
    vgap(dp);          ImGui::SameLine();
    vgap(face_indent); ImGui::SameLine();
    vbtn("Y", bsz, find_btn(ctr, DBCID_co_fire3), port); ImGui::SameLine();  // Y=fire3
    vbtn("X", bsz, find_btn(ctr, DBCID_co_fire4), port);                     // X=fire4

    // row 1
    render_dpad_row(ctr, port, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("SEL", msz, find_btn(ctr, DBCID_co_select), port); ImGui::SameLine();
    vbtn("STA", msz, find_btn(ctr, DBCID_co_start),  port); ImGui::SameLine();
    vgap(gap); ImGui::SameLine();
    vbtn("B", bsz, find_btn(ctr, DBCID_co_fire1), port); ImGui::SameLine();  // B=fire1
    vbtn("A", bsz, find_btn(ctr, DBCID_co_fire2), port);                     // A=fire2

    render_dpad_row(ctr, port, dp, 2);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// PlayStation 1 digital pad
// ─────────────────────────────────────────────────────────────────────────────
// Face: △ (fire4 top)  ○ (fire5 right)  × (fire2 bottom)  □ (fire1 left)
// Shoulders: L1/R1 (shoulder_left/right)  L2/R2 (shoulder_left2/right2)
//
// Proper diamond layout:
//   L2/R2 row:  [L2]  gap  [R2]
//   L1/R1 row:  [L1]  gap  [R1]
//   row 0:  gap ^  [dp_fix][tri_gap]  [△]          ← triangle at top
//   row 1:  ← gap →  [SEL][STA]  gap  [□][○]       ← square left, circle right
//   row 2:  gap ↓  [dp_fix][tri_gap]  [×]          ← cross at bottom
//
// tri_gap centres △ and × over the midpoint of the □/○ pair.

static void render_ps1_controller(JSM_CONTROLLER &ctr, int port)
{
    const float sc   = g_virt_scale;
    const float dp   = 30.0f * sc;
    const ImVec2 msz {46.0f*sc, 20.0f*sc};
    const ImVec2 bsz {34.0f*sc, 34.0f*sc};
    const float  sp  = ImGui::GetStyle().ItemSpacing.x;
    const float  gap = sp * 3.0f;

    // tri_gap: from common dpad end, advance past SEL/STA and half the □○ pair
    // so △ and × are centred over the □○ pair.
    const float tri_gap = 2.0f*gap + 2.0f*msz.x + 3.0f*sp + 0.5f*(bsz.x + sp);

    char title[32]; snprintf(title, sizeof(title), "PS1 Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    // Shoulder buttons: use frame height so text is never clipped
    const ImVec2 shsz{50.0f*sc, ImGui::GetFrameHeight()};

    float row_w = 3.0f*dp + 2.0f*sp + gap + msz.x + sp + msz.x + gap + bsz.x + sp + bsz.x;
    float sh_gap = row_w - 2.0f*shsz.x - sp;
    if (sh_gap < sp) sh_gap = sp;

    // L2 / R2 row
    vbtn("L2", shsz, find_btn(ctr, DBCID_co_shoulder_left2),  port); ImGui::SameLine();
    vgap(sh_gap); ImGui::SameLine();
    vbtn("R2", shsz, find_btn(ctr, DBCID_co_shoulder_right2), port);

    // L1 / R1 row
    vbtn("L1", shsz, find_btn(ctr, DBCID_co_shoulder_left),  port); ImGui::SameLine();
    vgap(sh_gap); ImGui::SameLine();
    vbtn("R1", shsz, find_btn(ctr, DBCID_co_shoulder_right), port);

    // row 0: △ centred above □○
    render_dpad_row(ctr, port, dp, 0, true);
    vgap(dp);      ImGui::SameLine(); // row 0 dpad correction
    vgap(tri_gap); ImGui::SameLine();
    vbtn("^", bsz, find_btn(ctr, DBCID_co_fire4), port); // △

    // row 1: SEL STA | □ ○
    render_dpad_row(ctr, port, dp, 1, true);
    vgap(gap); ImGui::SameLine();
    vbtn("SEL", msz, find_btn(ctr, DBCID_co_select), port); ImGui::SameLine();
    vbtn("STA", msz, find_btn(ctr, DBCID_co_start),  port); ImGui::SameLine();
    vgap(gap); ImGui::SameLine();
    vbtn("[]", bsz, find_btn(ctr, DBCID_co_fire1), port); ImGui::SameLine(); // □
    vbtn("O",  bsz, find_btn(ctr, DBCID_co_fire5), port);                    // ○

    // row 2: × centred below □○
    render_dpad_row(ctr, port, dp, 2, true);
    vgap(dp);      ImGui::SameLine(); // row 2 dpad correction (same as row 0)
    vgap(tri_gap); ImGui::SameLine();
    vbtn("X", bsz, find_btn(ctr, DBCID_co_fire2), port); // ×

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Casio PV-1000 controller
// ─────────────────────────────────────────────────────────────────────────────
// d-pad + 1 (fire1) + 2 (fire2) + START (start) + SELECT (select)
//
// Layout:
//   row 0:  gap ^
//   row 1:  ← gap →   [SEL][STA]   [2][1]
//   row 2:  gap ↓

static void render_pv1000_controller(JSM_CONTROLLER &ctr, int port)
{
    const float sc  = g_virt_scale;
    const float dp  = 30.0f * sc;
    const ImVec2 msz{46.0f*sc, 20.0f*sc};
    const ImVec2 bsz{36.0f*sc, 36.0f*sc};
    const float  gap = ImGui::GetStyle().ItemSpacing.x * 3.0f;

    char title[32]; snprintf(title, sizeof(title), "PV-1000 Controller %d", port);
    vi_first_pos(port);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    render_dpad_row(ctr, port, dp, 0);
    render_dpad_row(ctr, port, dp, 1, /*trail_sl=*/true);
    vgap(gap); ImGui::SameLine();
    vbtn("SEL", msz, find_btn(ctr, DBCID_co_select), port); ImGui::SameLine();
    vbtn("STA", msz, find_btn(ctr, DBCID_co_start),  port); ImGui::SameLine();
    vgap(gap); ImGui::SameLine();
    vbtn("2",   bsz, find_btn(ctr, DBCID_co_fire2), port); ImGui::SameLine();
    vbtn("1",   bsz, find_btn(ctr, DBCID_co_fire1), port);
    render_dpad_row(ctr, port, dp, 2);

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Cosmac VIP hex keypad
// ─────────────────────────────────────────────────────────────────────────────
// 4×4 hex grid, displayed in the original VIP layout:
//   1 2 3 C
//   4 5 6 D
//   7 8 9 E
//   A 0 B F
//
// Each VKey idx maps directly to the keyboard's key_states[idx] slot.
// Default PC bindings (CHIP-8 standard):  1234 / QWER / ASDF / ZXCV

static void render_cosmac_vip_keypad(JSM_KEYBOARD &kbd, int port)
{
    vi_first_pos(port, true);
    if (!ImGui::Begin("VIP Keypad", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }

    const float sc    = g_virt_scale;
    const float KEY_W = 32.0f * sc;
    const float KEY_H = 32.0f * sc;

    // No shift on a hex keypad.
    const bool shift_active = false;

    {
        static const VKey row[] = {{1,"1",1},{2,"2",1},{3,"3",1},{12,"C",1}};
        vkbd_row(kbd, row, 4, KEY_W, KEY_H, port, false, shift_active);
    }
    {
        static const VKey row[] = {{4,"4",1},{5,"5",1},{6,"6",1},{13,"D",1}};
        vkbd_row(kbd, row, 4, KEY_W, KEY_H, port, false, shift_active);
    }
    {
        static const VKey row[] = {{7,"7",1},{8,"8",1},{9,"9",1},{14,"E",1}};
        vkbd_row(kbd, row, 4, KEY_W, KEY_H, port, false, shift_active);
    }
    {
        static const VKey row[] = {{10,"A",1},{0,"0",1},{11,"B",1},{15,"F",1}};
        vkbd_row(kbd, row, 4, KEY_W, KEY_H, port, false, shift_active);
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void render_virtual_inputs(full_system *fsys, bool show,
                           bool show_controller, bool show_keyboard)
{
    if (!show || !fsys || !fsys->sys) return;
    jsm::systems kind = fsys->sys->kind;

    // ── Apple IIe: has both a joystick AND a keyboard ────────────────────────
    if (kind == jsm::systems::APPLEIIe) {
        if (show_controller && fsys->io.controller1.vec) {
            physical_io_device &pio = fsys->io.controller1.get();
            if (pio.connected && pio.enabled)
                render_apple2_joystick(pio.controller, 1);
        }
        if (show_keyboard && fsys->io.keyboard.vec) {
            physical_io_device &pio = fsys->io.keyboard.get();
            if (pio.connected && pio.enabled)
                render_apple2_keyboard(pio.keyboard, 1);
        }
        return;
    }

    // ── C64: has both joystick ports AND a keyboard — handle before the
    // is_keyboard_system check so each half can be toggled independently. ──────
    if (kind == jsm::systems::COMMODORE64) {
        // Joystick ports (full_sys assigns them as controller1 / controller2
        // in the order they appear in the IOs vector; use pio.id for the label)
        if (show_controller) {
            cvec_ptr<physical_io_device> *ports[2] = {
                &fsys->io.controller1, &fsys->io.controller2
            };
            for (auto *pp : ports) {
                if (!pp->vec) continue;
                physical_io_device &pio = pp->get();
                if (!pio.connected || !pio.enabled) continue;
                render_c64_joystick(pio.controller, (int)pio.id);
            }
        }
        // Keyboard
        if (show_keyboard && fsys->io.keyboard.vec) {
            physical_io_device &pio = fsys->io.keyboard.get();
            if (pio.connected && pio.enabled)
                render_c64_keyboard(pio.keyboard, 1);
        }
        return;
    }

    // Keyboards / keypads are toggled independently from gamepad controllers.
    // Classify the current system so we can bail early if that type is hidden.
    bool is_keyboard_system =
        kind == jsm::systems::ZX_SPECTRUM_48K  ||
        kind == jsm::systems::ZX_SPECTRUM_128K ||
        kind == jsm::systems::COSMAC_VIP_2k    ||
        kind == jsm::systems::COSMAC_VIP_4k;

    if (is_keyboard_system && !show_keyboard) return;
    if (!is_keyboard_system && !show_controller) return;

    // ── NES ──────────────────────────────────────────────────────────────────
    if (kind == jsm::systems::NES) {
        cvec_ptr<physical_io_device> *controllers[2] = {
            &fsys->io.controller1, &fsys->io.controller2
        };
        for (int p = 0; p < 2; p++) {
            if (!controllers[p]->vec) continue;
            physical_io_device &pio = controllers[p]->get();
            if (!pio.connected || !pio.enabled) continue;
            render_nes_controller(pio.controller, p + 1);
        }
        return;
    }

    // ── SMS / Game Gear / SG-1000 ────────────────────────────────────────────
    if (kind == jsm::systems::SMS1 || kind == jsm::systems::SMS2 ||
        kind == jsm::systems::GG   || kind == jsm::systems::SG1000) {
        bool is_gg = (kind == jsm::systems::GG);
        cvec_ptr<physical_io_device> *ports[2] = {
            &fsys->io.controller1, &fsys->io.controller2
        };
        int n = is_gg ? 1 : 2;
        for (int p = 0; p < n; p++) {
            if (!ports[p]->vec) continue;
            physical_io_device &pio = ports[p]->get();
            if (!pio.connected || !pio.enabled) continue;
            render_sms_controller(pio.controller, p + 1, is_gg);
        }
        return;
    }

    // ── Atari 2600 ───────────────────────────────────────────────────────────
    if (kind == jsm::systems::ATARI2600) {
        cvec_ptr<physical_io_device> *ports[2] = {
            &fsys->io.controller1, &fsys->io.controller2
        };
        for (int p = 0; p < 2; p++) {
            if (!ports[p]->vec) continue;
            physical_io_device &pio = ports[p]->get();
            if (!pio.connected || !pio.enabled) continue;
            render_atari2600_controller(pio.controller, p + 1);
        }
        return;
    }

    // ── Neo Geo AES / MVS ────────────────────────────────────────────────────
    if (kind == jsm::systems::NEOGEO_AES || kind == jsm::systems::NEOGEO_MVS) {
        cvec_ptr<physical_io_device> *ports[2] = {
            &fsys->io.controller1, &fsys->io.controller2
        };
        for (int p = 0; p < 2; p++) {
            if (!ports[p]->vec) continue;
            physical_io_device &pio = ports[p]->get();
            if (!pio.connected || !pio.enabled) continue;
            render_neogeo_controller(pio.controller, p + 1);
        }
        return;
    }

    // ── Sega Genesis / Mega Drive ─────────────────────────────────────────────
    if (kind == jsm::systems::GENESIS_USA  ||
        kind == jsm::systems::MEGADRIVE_PAL ||
        kind == jsm::systems::GENESIS_JAP) {
        cvec_ptr<physical_io_device> *ports[2] = {
            &fsys->io.controller1, &fsys->io.controller2
        };
        for (int p = 0; p < 2; p++) {
            if (!ports[p]->vec) continue;
            physical_io_device &pio = ports[p]->get();
            if (!pio.connected || !pio.enabled) continue;
            render_genesis6_controller(pio.controller, p + 1);
        }
        return;
    }

    // ── Game Boy / Game Boy Color ─────────────────────────────────────────────
    if (kind == jsm::systems::DMG || kind == jsm::systems::GBC) {
        if (!fsys->io.controller1.vec) return;
        physical_io_device &pio = fsys->io.controller1.get();
        if (!pio.connected || !pio.enabled) return;
        render_gb_controller(pio.controller, 1);
        return;
    }

    // ── Game Boy Advance ──────────────────────────────────────────────────────
    if (kind == jsm::systems::GBA) {
        if (!fsys->io.controller1.vec) return;
        physical_io_device &pio = fsys->io.controller1.get();
        if (!pio.connected || !pio.enabled) return;
        render_gba_controller(pio.controller, 1);
        return;
    }

    // ── ZX Spectrum (48K and 128K share the same 40-key keyboard layout) ─────
    if (kind == jsm::systems::ZX_SPECTRUM_48K ||
        kind == jsm::systems::ZX_SPECTRUM_128K) {
        if (!fsys->io.keyboard.vec) return;
        physical_io_device &pio = fsys->io.keyboard.get();
        if (!pio.connected || !pio.enabled) return;
        render_zxspectrum_keyboard(pio.keyboard, 1);
        return;
    }

    // ── TurboGrafx-16 ────────────────────────────────────────────────────────
    if (kind == jsm::systems::TURBOGRAFX16) {
        if (!fsys->io.controller1.vec) return;
        physical_io_device &pio = fsys->io.controller1.get();
        if (!pio.connected || !pio.enabled) return;
        render_tg16_controller(pio.controller, 1);
        return;
    }

    // ── SNES ─────────────────────────────────────────────────────────────────
    if (kind == jsm::systems::SNES) {
        cvec_ptr<physical_io_device> *ports[2] = {
            &fsys->io.controller1, &fsys->io.controller2
        };
        for (int p = 0; p < 2; p++) {
            if (!ports[p]->vec) continue;
            physical_io_device &pio = ports[p]->get();
            if (!pio.connected || !pio.enabled) continue;
            render_snes_controller(pio.controller, p + 1);
        }
        return;
    }

    // ── Nintendo DS ───────────────────────────────────────────────────────────
    if (kind == jsm::systems::NDS) {
        if (!fsys->io.controller1.vec) return;
        physical_io_device &pio = fsys->io.controller1.get();
        if (!pio.connected || !pio.enabled) return;
        render_nds_controller(pio.controller, 1);
        return;
    }

    // ── PlayStation 1 ────────────────────────────────────────────────────────
    if (kind == jsm::systems::PS1) {
        cvec_ptr<physical_io_device> *ports[2] = {
            &fsys->io.controller1, &fsys->io.controller2
        };
        for (int p = 0; p < 2; p++) {
            if (!ports[p]->vec) continue;
            physical_io_device &pio = ports[p]->get();
            if (!pio.connected || !pio.enabled) continue;
            render_ps1_controller(pio.controller, p + 1);
        }
        return;
    }

    // ── Casio PV-1000 ────────────────────────────────────────────────────────
    if (kind == jsm::systems::CASIO_PV1000) {
        cvec_ptr<physical_io_device> *ports[2] = {
            &fsys->io.controller1, &fsys->io.controller2
        };
        for (int p = 0; p < 2; p++) {
            if (!ports[p]->vec) continue;
            physical_io_device &pio = ports[p]->get();
            if (!pio.connected || !pio.enabled) continue;
            render_pv1000_controller(pio.controller, p + 1);
        }
        return;
    }

    // ── Cosmac VIP hex keypad ────────────────────────────────────────────────
    if (kind == jsm::systems::COSMAC_VIP_2k ||
        kind == jsm::systems::COSMAC_VIP_4k) {
        if (!fsys->io.keyboard.vec) return;
        physical_io_device &pio = fsys->io.keyboard.get();
        if (!pio.connected || !pio.enabled) return;
        render_cosmac_vip_keypad(pio.keyboard, 1);
        return;
    }
}
