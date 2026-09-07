/*
 * ChargeCap overlay
 *
 * A libultrahand overlay with exactly two controls: an on/off toggle and a
 * slider for the cutoff percentage, plus a minimal battery readout.
 *
 * Sysmodule-disabled handling:
 *
 * Opening the overlay never blocks on the sysmodule. Its liveness is probed
 * through pm:dmnt (pmdmntGetProcessId - fail-fast, no sm service lookup, the
 * same approach ovl-sysmodules and KeyX use), and its IPC service is only
 * contacted once that probe says the module is running. If the module is not
 * running the overlay shows a stub GUI with a warning instead of the
 * settings, so the overlay - and the host menu that runs it - always stays
 * responsive.
 *
 * SPDX-License-Identifier: MIT
 */

#define NDEBUG
#define STBTT_STATIC
#define TESLA_INIT_IMPL

#ifdef USE_EXCEPTION_WRAP
#include <exception_wrap.hpp>
#endif

#include <tesla.hpp>

#include <algorithm>
#include <cstdio>

#include "ipc_client.hpp"
#include "settings.hpp"

namespace {

    constexpr u64 kRefreshNs   = 1000000000ULL;  /* status refresh */
    constexpr u64 kPersistNs   =  750000000ULL;  /* debounce ini writes */

    void FormatPercent(char *out, size_t size, unsigned value) {
        std::snprintf(out, size, "%u%%", value);
    }

}

class ChargeCapGui : public tsl::Gui {
public:
    ChargeCapGui() {
        /* NOTE: no IPC and no sysmodule probing in the constructor on
         * purpose. The constructor runs while the overlay host expects us to
         * come up quickly; all module interaction is fail-fast and deferred
         * to createUI()/update(). */
    }

    ~ChargeCapGui() {
        /* Flush any pending slider movement on the way out. */
        if (this->m_dirty)
            settings::Save(this->m_cfg);
    }

    virtual tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame("ChargeCap", APP_VERSION);

        /* Check if sysmodule is running - cheap pmdmnt probe, never an sm
         * lookup, so it cannot stall the host while the module is disabled.
         * When it is not running we show a stub overlay and never touch the
         * module's service at all. */
        if (!ipc::ModuleRunning()) {
            this->m_hasFullUi = false;
            return CreateUnavailableUi(frame);
        }
        this->m_hasFullUi = true;

        /* The ini is the source of truth on disk, but a running sysmodule may
         * already hold a newer value (set from a previous overlay session), so
         * prefer what it reports. */
        this->m_cfg = settings::Load();

        ChargeCapConfig live{};
        if (ipc::GetConfig(&live)) {
            settings::Sanitize(live);
            this->m_cfg = live;
        }

        auto *list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Charge limit"));

        this->m_toggle = new tsl::elm::ToggleListItem("Enabled", this->m_cfg.enabled != 0);
        this->m_toggle->setStateChangedListener([this](bool state) {
            this->m_cfg.enabled = state ? 1 : 0;
            this->Apply();
        });
        list->addItem(this->m_toggle);

        this->m_limitItem = new tsl::elm::ListItem("Stop charging at");
        this->m_limitItem->setValue(this->LimitText());
        list->addItem(this->m_limitItem);

        /* ------------------------------------------------------------------
         * The slider.
         *
         * StepTrackBar is the libtesla-compatible stepped slider that
         * libultrahand keeps as a drop-in. It works in step indices, so index
         * 0 == CHARGECAP_LIMIT_MIN (50%) and the last index == CHARGECAP_LIMIT_MAX
         * (99%), giving every whole percentage in between.
         *
         * If your pinned libultrahand changes this constructor, this is the
         * one line to adapt (its TrackBarV2 takes an explicit min/max/step).
         * ------------------------------------------------------------------ */
        auto *slider = new tsl::elm::StepTrackBar("", CHARGECAP_LIMIT_STEPS);
        slider->setProgress(static_cast<u8>(this->m_cfg.limit - CHARGECAP_LIMIT_MIN));
        slider->setValueChangedListener([this](u8 step) {
            const u8 clamped = std::min<u8>(step, CHARGECAP_LIMIT_STEPS - 1);
            this->m_cfg.limit = static_cast<u8>(CHARGECAP_LIMIT_MIN + clamped);
            this->m_limitItem->setValue(this->LimitText());
            this->Apply();
        });
        list->addItem(slider);

        list->addItem(new tsl::elm::CategoryHeader("Status"));

        this->m_moduleItem = new tsl::elm::ListItem("Sysmodule");
        this->m_moduleItem->setValue("Running");
        list->addItem(this->m_moduleItem);

        this->m_batteryItem = new tsl::elm::ListItem("Battery");
        this->m_batteryItem->setValue("...");
        list->addItem(this->m_batteryItem);

        this->m_chargeItem = new tsl::elm::ListItem("Charging");
        this->m_chargeItem->setValue("...");
        list->addItem(this->m_chargeItem);

        list->addItem(new tsl::elm::CategoryHeader("Notes"));
        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            (void)w;
            (void)h;
            renderer->drawString("Off applies no limit at all.", false, x + 20, y + 20, 15, renderer->a(tsl::warningTextColor));
            renderer->drawString("Long sessions at a fixed cap can cause", false, x + 20, y + 42, 15, renderer->a(tsl::warningTextColor));
            renderer->drawString("battery desync. Discharge fully now and", false, x + 20, y + 62, 15, renderer->a(tsl::warningTextColor));
            renderer->drawString("then to keep the gauge honest.", false, x + 20, y + 82, 15, renderer->a(tsl::warningTextColor));
        }), 110);

        frame->setContent(list);
        return frame;
    }

    virtual void update() override {
        const u64 now = armGetSystemTick();

        if (this->m_dirty && armTicksToNs(now - this->m_dirtyAt) >= kPersistNs) {
            settings::Save(this->m_cfg);
            this->m_dirty = false;
        }

        /* Stub mode: no status rows exist, nothing to refresh. Do not touch
         * RefreshStatus() here - it would dereference the never-created list
         * items (this used to crash the UI thread ~1s after the stub opened
         * and freeze the host menu). */
        if (!this->m_hasFullUi)
            return;

        if (armTicksToNs(now - this->m_lastRefresh) < kRefreshNs)
            return;

        this->m_lastRefresh = now;
        this->RefreshStatus();
    }

private:
    /* Stub shown when the sysmodule is not running: warning text only, no
     * settings, no IPC. Closing and reopening the overlay re-evaluates.
     * Text is kept to the same width budget as the notes drawer (15 px,
     * <= ~41 chars per line) so nothing gets clipped. */
    static tsl::elm::Element *CreateUnavailableUi(tsl::elm::OverlayFrame *frame) {
        auto *list = new tsl::elm::List();

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            (void)w;
            (void)h;
            renderer->drawString("Sysmodule not running", false, x + 20, y + 36, 24, renderer->a(tsl::Color(0xF, 0x0, 0x0, 0xF)));
        }), 62);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            (void)w;
            (void)h;
            const tsl::Color text = renderer->a(tsl::defaultTextColor);
            renderer->drawString("The ChargeCap sysmodule is not", false, x + 20, y + 24, 15, text);
            renderer->drawString("running, so no charge limit is enforced.", false, x + 20, y + 46, 15, text);
            renderer->drawString("Enable it in ovl-sysmodules (or restore", false, x + 20, y + 88, 15, text);
            renderer->drawString("the atmosphere/contents/42000000000000C0", false, x + 20, y + 110, 15, text);
            renderer->drawString("folder), then reboot and open again.", false, x + 20, y + 132, 15, text);
        }), 152);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            (void)w;
            (void)h;
            renderer->drawString("Press B to close.", false, x + 20, y + 26, 15, renderer->a(tsl::bottomTextColor));
        }), 44);

        frame->setContent(list);
        return frame;
    }

    const char *LimitText() {
        FormatPercent(this->m_limitText, sizeof(this->m_limitText), this->m_cfg.limit);
        return this->m_limitText;
    }

    /* Push to the running module immediately, persist lazily. When the module
     * is not running the change is only persisted - it will take effect the
     * next time the module starts (it reads the ini at boot). */
    void Apply() {
        settings::Sanitize(this->m_cfg);
        if (ipc::ModuleRunning())
            ipc::SetConfig(this->m_cfg);

        this->m_dirty   = true;
        this->m_dirtyAt = armGetSystemTick();
    }

    void RefreshStatus() {
        ChargeCapStatus status{};

        /* Cheap fail-fast liveness probe before any IPC: while the module is
         * down we never touch its sm service, so it cannot stall the UI or
         * the overlay host. */
        const bool running = ipc::ModuleRunning() && ipc::GetStatus(&status);

        if (!running) {
            this->m_moduleItem->setValue("Not running");
            this->m_batteryItem->setValue("-");
            this->m_chargeItem->setValue("-");
            return;
        }

        this->m_moduleItem->setValue("Running");

        FormatPercent(this->m_batteryText, sizeof(this->m_batteryText), status.charge_percent);
        this->m_batteryItem->setValue(this->m_batteryText);

        if (!status.charger_connected)
            this->m_chargeItem->setValue("Unplugged");
        else if (status.limit_held)
            this->m_chargeItem->setValue("Held at limit");
        else if (status.charging)
            this->m_chargeItem->setValue("Charging");
        else
            this->m_chargeItem->setValue("Not charging");
    }

    ChargeCapConfig m_cfg{};

    tsl::elm::ToggleListItem *m_toggle      = nullptr;
    tsl::elm::ListItem       *m_limitItem   = nullptr;
    tsl::elm::ListItem       *m_moduleItem  = nullptr;
    tsl::elm::ListItem       *m_batteryItem = nullptr;
    tsl::elm::ListItem       *m_chargeItem  = nullptr;

    char m_limitText[8]    = {};
    char m_batteryText[8]  = {};

    u64  m_lastRefresh = 0;
    u64  m_dirtyAt     = 0;
    bool m_dirty       = false;
    bool m_hasFullUi   = false;
};

class ChargeCapOverlay : public tsl::Overlay {
public:
    /* initServices() intentionally does nothing module-related: no IPC, no
     * service probe. The first thing that touches the sysmodule is the
     * liveness probe inside ChargeCapGui::createUI(), which only runs after
     * the overlay is fully up and is fail-fast (pmdmnt, not sm). This keeps
     * opening the overlay with a disabled sysmodule from ever hanging the
     * overlay host. */
    virtual void exitServices() override {
        /* Close the ipc session and drop the pm:dmnt reference we took. */
        ipc::Exit();
    }

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<ChargeCapGui>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<ChargeCapOverlay>(argc, argv);
}
