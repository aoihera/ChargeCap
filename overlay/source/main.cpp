/*
 * ChargeCap overlay
 *
 * A libultrahand overlay with charge limit controls, optional sleep mode
 * limiting, and real-time status readouts.
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

    constexpr u64 kRefreshNs   = 1000000000ULL;  /* status refresh (1s) */
    constexpr u64 kPersistNs   =  300000000ULL;  /* debounce slider/ini writes (300ms) */

    /* Formats charging state and rounded percentage: "Held at limit (80%)", "Charging (75%)", etc. */
    void FormatBatteryStatus(char *out, size_t size, const ChargeCapStatus &status) {
        const char *state = "Not charging";
        if (!status.charger_connected)
            state = "Unplugged";
        else if (status.limit_held)
            state = "Held at limit";
        else if (status.charging)
            state = "Charging";

        std::snprintf(out, size, "%s (%u%%)", state, status.charge_percent);
    }

    /* Formats raw fuel-gauge permille (psm cmd 12) and battery voltage (psm cmd 17) into "Percentage / Voltage" */
    void FormatRawCombined(char *out, size_t size, u16 permille, u16 mv) {
        char permilleBuf[16];
        char mvBuf[16];

        if (permille == 0xFFFF)
            std::snprintf(permilleBuf, sizeof(permilleBuf), "-");
        else
            std::snprintf(permilleBuf, sizeof(permilleBuf), "%u.%u%%", permille / 10u, permille % 10u);

        if (mv == 0xFFFF)
            std::snprintf(mvBuf, sizeof(mvBuf), "-");
        else
            std::snprintf(mvBuf, sizeof(mvBuf), "%u.%03u V", mv / 1000u, mv % 1000u);

        std::snprintf(out, size, "%s / %s", permilleBuf, mvBuf);
    }

}

/* Custom TrackBar that properly scales handlePos across custom [min, max] ranges */
class ChargeLimitTrackBar : public tsl::elm::TrackBar {
public:
    ChargeLimitTrackBar(const std::string &label)
        : tsl::elm::TrackBar("", false, false, true, label, "%", true) {
        this->setRange(CHARGECAP_LIMIT_MIN, CHARGECAP_LIMIT_MAX);
    }

    virtual void draw(tsl::gfx::Renderer *renderer) override {
        if (this->touchInSliderBounds) {
            this->m_drawFrameless = true;
            this->drawHighlight(renderer);
        } else {
            this->m_drawFrameless = false;
        }

        const s32 xPos = this->getX() + 59;
        const s32 yPos = this->getY() + 40 + 16 - 3;
        const s32 width = this->getWidth() - 95;
        const s16 span = this->m_maxValue - this->m_minValue;
        const u16 handlePos = (span > 0) ? static_cast<u16>((width * this->m_value) / span) : 0;

        // Draw track bar background
        this->drawBar(renderer, xPos, yPos - 3, width, tsl::trackBarEmptyColor, true);

        const bool isEffectivelyUnlocked = this->m_unlockedTrackbar || ult::allowSlide.load(std::memory_order_acquire);

        if (!this->m_focused) {
            this->drawBar(renderer, xPos, yPos - 3, handlePos, tsl::trackBarFullColor, true);
            renderer->drawCircle(xPos + handlePos, yPos, 16, true, renderer->a(this->m_drawFrameless ? tsl::s_highlightColor : tsl::trackBarSliderBorderColor));
            renderer->drawCircle(xPos + handlePos, yPos, 13, true, renderer->a((isEffectivelyUnlocked || this->touchInSliderBounds) ? tsl::trackBarSliderMalleableColor : tsl::trackBarSliderColor));
        } else {
            this->touchInSliderBounds = false;
            if (this->m_unlockedTrackbar != ult::unlockedSlide.load(std::memory_order_acquire))
                ult::unlockedSlide.store(this->m_unlockedTrackbar, std::memory_order_release);
            this->drawBar(renderer, xPos, yPos - 3, handlePos, tsl::trackBarFullColor, true);
            renderer->drawCircle(xPos + this->x + handlePos, yPos + this->y, 16, true, renderer->a(tsl::s_highlightColor));
            renderer->drawCircle(xPos + this->x + handlePos, yPos + this->y, 12, true, renderer->a(isEffectivelyUnlocked ? tsl::trackBarSliderMalleableColor : tsl::trackBarSliderColor));
        }

        // Draw label + value
        std::string labelPart = this->m_label;
        ult::removeTag(labelPart);

        const int dispVal = static_cast<int>(this->m_minValue) + static_cast<int>(this->m_value);
        const std::string valuePart = ult::to_string(dispVal) + "%";

        const auto valueWidth = renderer->getTextDimensions(valuePart, false, 16).first;
        const s32 labelX = xPos;
        const s32 valueX = xPos + width - valueWidth;

        renderer->drawString(labelPart, false, labelX, this->getY() + 14 + 16, 16,
                             ((!this->m_focused || !ult::useSelectionText) ? tsl::defaultTextColor : tsl::selectedTextColor));
        renderer->drawString(valuePart, false, valueX, this->getY() + 14 + 16, 16,
                             (this->m_focused && ult::useSelectionValue) ? tsl::selectedValueTextColor : tsl::onTextColor);

        if (this->m_lastBottomBound != this->getTopBound())
            renderer->drawRect(this->getX() + 4 + 20 - 1, this->getTopBound(), this->getWidth() + 6 + 10 + 20 + 4, 1, renderer->a(tsl::separatorColor));
        renderer->drawRect(this->getX() + 4 + 20 - 1, this->getBottomBound(), this->getWidth() + 6 + 10 + 20 + 4, 1, renderer->a(tsl::separatorColor));
        this->m_lastBottomBound = this->getBottomBound();
    }
};

class ChargeCapGui : public tsl::Gui {
public:
    ChargeCapGui() {}

    ~ChargeCapGui() {
        if (this->m_dirty) {
            settings::Sanitize(this->m_cfg);
            settings::Save(this->m_cfg);
            if (ipc::ModuleRunning())
                ipc::SetConfig(this->m_cfg);
        }
    }

    virtual tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame("ChargeCap", APP_VERSION);

        if (!ipc::ModuleRunning()) {
            this->m_hasFullUi = false;
            return CreateUnavailableUi(frame);
        }
        this->m_hasFullUi = true;

        this->m_cfg = settings::Load();

        ChargeCapConfig live{};
        if (ipc::GetConfig(&live)) {
            settings::Sanitize(live);
            this->m_cfg = live;
        }

        auto *list = new tsl::elm::List();

        // -------------------------------------------------------------
        // Limit Configuration
        // -------------------------------------------------------------
        list->addItem(new tsl::elm::CategoryHeader("Charge Limit"));

        this->m_toggle = new tsl::elm::ToggleListItem("Enabled", this->m_cfg.enabled != 0);
        this->m_toggle->setStateChangedListener([this](bool state) {
            this->m_cfg.enabled = state ? 1 : 0;
            this->UpdateToggleText();
            this->ApplyImmediate();
        });
        this->UpdateToggleText();
        list->addItem(this->m_toggle);

        this->m_slider = new ChargeLimitTrackBar("Stop charging at");
        this->m_slider->setProgress(static_cast<u16>(this->m_cfg.limit - CHARGECAP_LIMIT_MIN));
        this->m_slider->setValueChangedListener([this](u16 val) {
            const u8 clamped = std::min<u8>((u8)val, CHARGECAP_LIMIT_STEPS - 1);
            this->m_cfg.limit = static_cast<u8>(CHARGECAP_LIMIT_MIN + clamped);
            this->UpdateToggleText();
            this->m_dirty   = true;
            this->m_dirtyAt = armGetSystemTick();
        });
        list->addItem(this->m_slider);

        this->m_sleepToggle = new tsl::elm::ToggleListItem("Limit in Sleep Mode", this->m_cfg.sleep_limit_enabled != 0);
        this->m_sleepToggle->setStateChangedListener([this](bool state) {
            this->m_cfg.sleep_limit_enabled = state ? 1 : 0;
            this->ApplyImmediate();
        });
        list->addItem(this->m_sleepToggle);

        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            (void)w;
            (void)h;
            const tsl::Color text = renderer->a(tsl::bottomTextColor);
            renderer->drawString("If console enters Sleep Mode while charging,", false, x + 20, y + 16, 15, text);
            renderer->drawString("will periodically wake the console in the", false, x + 20, y + 36, 15, text);
            renderer->drawString("background to be able to apply the limit.", false, x + 20, y + 56, 15, text);
        }), 76);

        // -------------------------------------------------------------
        // Status Indicators
        // -------------------------------------------------------------
        list->addItem(new tsl::elm::CategoryHeader("Status"));

        this->m_chargeItem = new tsl::elm::ListItem("Charging");
        this->m_chargeItem->setValue("...");
        list->addItem(this->m_chargeItem);

        this->m_rawItem = new tsl::elm::ListItem("Raw gauge");
        this->m_rawItem->setValue("...");
        list->addItem(this->m_rawItem);

        // -------------------------------------------------------------
        // Notes & Advisories (Non-red standard text)
        // -------------------------------------------------------------
        list->addItem(new tsl::elm::CategoryHeader("Notes"));
        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            (void)w;
            (void)h;
            const tsl::Color text = renderer->a(tsl::bottomTextColor);
            renderer->drawString("Off applies no limit at all.", false, x + 20, y + 20, 15, text);
            renderer->drawString("Long sessions at a fixed cap can cause", false, x + 20, y + 42, 15, text);
            renderer->drawString("battery desync. Discharge fully now and", false, x + 20, y + 62, 15, text);
            renderer->drawString("then to keep the gauge honest.", false, x + 20, y + 82, 15, text);
        }), 110);

        frame->setContent(list);
        return frame;
    }

    virtual void update() override {
        const u64 now = armGetSystemTick();

        if (this->m_dirty && armTicksToNs(now - this->m_dirtyAt) >= kPersistNs) {
            settings::Sanitize(this->m_cfg);
            settings::Save(this->m_cfg);
            if (ipc::ModuleRunning())
                ipc::SetConfig(this->m_cfg);
            this->m_dirty = false;
        }

        if (!this->m_hasFullUi)
            return;

        if (armTicksToNs(now - this->m_lastRefresh) < kRefreshNs)
            return;

        this->m_lastRefresh = now;
        this->RefreshStatus();
    }

private:
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

    void UpdateToggleText() {
        if (!this->m_toggle)
            return;
        if (this->m_cfg.enabled) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "On (%u%%)", this->m_cfg.limit);
            this->m_toggle->setValue(buf, false);
        } else {
            this->m_toggle->setValue("Off", true);
        }
    }

    void ApplyImmediate() {
        settings::Sanitize(this->m_cfg);
        if (ipc::ModuleRunning())
            ipc::SetConfig(this->m_cfg);

        this->m_dirty   = true;
        this->m_dirtyAt = armGetSystemTick();
    }

    void RefreshStatus() {
        ChargeCapStatus status{};

        const bool running = ipc::ModuleRunning() && ipc::GetStatus(&status);

        if (!running) {
            this->m_chargeItem->setValue("-");
            this->m_rawItem->setValue("-");
            return;
        }

        FormatBatteryStatus(this->m_chargeText, sizeof(this->m_chargeText), status);
        this->m_chargeItem->setValue(this->m_chargeText);

        FormatRawCombined(this->m_rawText, sizeof(this->m_rawText), status.raw_permille, status.cell_mv);
        this->m_rawItem->setValue(this->m_rawText);
    }

    ChargeCapConfig m_cfg{};

    tsl::elm::ToggleListItem *m_toggle      = nullptr;
    ChargeLimitTrackBar      *m_slider      = nullptr;
    tsl::elm::ToggleListItem *m_sleepToggle = nullptr;
    tsl::elm::ListItem       *m_chargeItem  = nullptr;
    tsl::elm::ListItem       *m_rawItem     = nullptr;

    char m_chargeText[32] = {};
    char m_rawText[32]    = {};

    u64  m_lastRefresh = 0;
    u64  m_dirtyAt     = 0;
    bool m_dirty       = false;
    bool m_hasFullUi   = false;
};

class ChargeCapOverlay : public tsl::Overlay {
public:
    virtual void exitServices() override {
        ipc::Exit();
    }

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<ChargeCapGui>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<ChargeCapOverlay>(argc, argv);
}
