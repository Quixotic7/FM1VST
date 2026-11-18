// SPDX-License-Identifier: GPL-3.0-only
// Ruido (https://github.com/miguelhd/ruido, the pinned, unmodified submodule cores/ruido: GPL-3.0-only): the C++
// half of the core. As Ruido's own browser simulator (platforms/sim/wasm_bridge.cpp) it unity-includes the portable
// C++17 firmware (src/core, src/dsp, src/ui) and drives it in one place; what the FM-1 adds to it is its own device
// bridge's (platforms/fm1/ruido_fm1_bridge.cpp, fm1_main.c), whose portable parts are used unchanged from the
// submodule: fm1_panel_io.h (the 14 buttons, the 7 encoders, the 27 keys to the controller, MASTER's quadratic taper),
// fm1_storage_io.h (the scenes, the settings and the LIVE patch in the NVS blobs and their journals) and
// fm1_audio_io.h (the TPDF quantizer to Q15). One thread: the DSP renders on the audio pull (64-frame blocks, as Core 1
// renders them, at 44.1 kHz: a two-core unit's full rate), the controller, the HUD and the panel on the frame clock.
// The C unit (core_ruido.c) is the ABI and the NOR; this file gives it plain functions (ruido_glue_*).
#include <new>

#include "core/ControllerEncoder.cpp"
#include "core/ControllerPageNav.cpp"
#include "core/KeyboardCircuitLab.cpp"
#include "core/KeyboardEngine.cpp"
#include "core/KeyboardLiveMixer.cpp"
#include "core/KeyboardRhythmLab.cpp"
#include "core/LedEngine.cpp"
#include "core/RuidoController.cpp"
#include "core/SnapshotBank.cpp"
#include "core/SnapshotCodec.cpp"
#include "core/SnapshotMorph.cpp"
#include "dsp/DroneEngine.cpp"
#include "dsp/HalfBandDecimator.cpp"
#include "dsp/SallenKeyFilter.cpp"
#include "dsp/SchmittOscillatorBank.cpp"
#include "dsp/VoiceLfoBank.cpp"
#include "ui/BootIntroRender.cpp"
#include "ui/HudAmpGraph.cpp"
#include "ui/HudChrome.cpp"
#include "ui/HudGlobalViews.cpp"
#include "ui/HudGraphs.cpp"
#include "ui/HudHeader.cpp"
#include "ui/HudKeybedView.cpp"
#include "ui/HudSceneView.cpp"
#include "ui/ScopeHudRenderer.cpp"
#include "ui/ScopeTraceEngine.cpp"
#include "ui/ScopeTraceRender.cpp"

#include "fm1_audio_io.h"
#include "fm1_led_modulator.h"
#include "fm1_panel_io.h"
#include "fm1_storage_io.h"

#include <cstddef>
#include <cstdint>

namespace {

// placement-new arenas, as both bridges keep them (no static constructors)
alignas(64) uint8_t gDspArena[sizeof(ruido::DroneEngine)];
alignas(64) uint8_t gCtrlArena[sizeof(ruido::RuidoController)];
alignas(64) uint8_t gRendererArena[sizeof(ruido::ScopeHudRenderer)];
alignas(64) uint8_t gPanelArena[sizeof(ruido::fm1::Fm1PanelState)];
alignas(64) uint8_t gLedModArena[sizeof(ruido::fm1::Fm1LedModulator)];
alignas(64) uint8_t gNvsArena[sizeof(ruido::fm1::Fm1NvsRuntimeState)];
alignas(64) uint8_t gQuantArena[sizeof(ruido::Fm1TpdfQuantizer)];
ruido::DroneEngine* gDsp = nullptr;
ruido::RuidoController* gCtrl = nullptr;
ruido::ScopeHudRenderer* gRenderer = nullptr;
ruido::fm1::Fm1PanelState* gPanel = nullptr;
ruido::fm1::Fm1LedModulator* gLedMod = nullptr;
ruido::fm1::Fm1NvsRuntimeState* gNvs = nullptr;
ruido::Fm1TpdfQuantizer* gQuant = nullptr;
float gMasterGain = 0.0f;
uint32_t gMidiRtEvents = 0u;

} // namespace

extern "C" {

// the NOR's NVS functions (core_ruido.c: Dinghy's blob_load / blob_save and fm1_nvs_glue.h's journal reads / writes)
int fm1core_ru_blob_load(uint32_t idx, void* dst, uint32_t len);
int fm1core_ru_blob_save(uint32_t idx, const void* src, uint32_t len);
int fm1core_ru_tail_read(uint32_t sec, uint32_t off, void* dst, uint32_t len);
int fm1core_ru_tail_write(uint32_t sec, uint32_t off, const void* src, uint32_t len);

// ruido_fm1_construct, ruido_fm1_nvs_load_boot, ruido_fm1_restore_default_patch, ruido_fm1_arm_audio (fm1_main.c's
// order; the boot benchmarks and Core 1's start are the device's own): master_q12 is platform.c's MASTER (0..4088)
void ruido_glue_boot(uint32_t master_q12) {
    gMasterGain = ruido::fm1::masterQ12ToGain(master_q12);
    gDsp = new (gDspArena) ruido::DroneEngine();
    gDsp->init(44100.0f);
    gQuant = new (gQuantArena) ruido::Fm1TpdfQuantizer();
    gQuant->reset();
    gCtrl = new (gCtrlArena) ruido::RuidoController();
    gRenderer = new (gRendererArena) ruido::ScopeHudRenderer();
    gPanel = new (gPanelArena) ruido::fm1::Fm1PanelState();
    gLedMod = new (gLedModArena) ruido::fm1::Fm1LedModulator();
    gNvs = new (gNvsArena) ruido::fm1::Fm1NvsRuntimeState();
    gCtrl->attachControlBus(&gDsp->controlBus());
    gPanel->reset(gMasterGain);
    gPanel->syncMasterPot(*gCtrl, gMasterGain);
    gLedMod->reset();
    gDsp->snapParametersToLatest();
    gRenderer->reset();
    gRenderer->armBootIntro();
    gNvs->reset();
    gMidiRtEvents = 0u;
    // the scenes, the settings, the LIVE patch (CRC-checked, journals scanned and compacted at boot)
    (void)ruido::fm1::loadBootNvsBlobs(fm1core_ru_blob_load, *gCtrl, *gPanel, *gLedMod, gMasterGain, *gNvs,
                                       fm1core_ru_tail_read, fm1core_ru_blob_save);
    gDsp->snapParametersToLatest();
    // ruido_fm1_restore_default_patch: the restored patch published, the engine started clean on it
    gDsp->controlBus().publish(gCtrl->state());
    gDsp->init(44100.0f);
    gDsp->snapParametersToLatest();
    gPanel->reset(gMasterGain);
    gPanel->syncMasterPot(*gCtrl, gMasterGain);
    (void)gCtrl->consumeSavedSlotMask();
    (void)gCtrl->consumeSettingsDirty();
    (void)gCtrl->consumeLivePatchDirty();
    gDsp->snapParametersToLatest();
    gRenderer->reset();
    gRenderer->armBootIntro();
    gNvs->clearPendingAfterRestore();
}

// ruido_fm1_set_master_q12
void ruido_glue_master(uint32_t master_q12) {
    gMasterGain = ruido::fm1::masterQ12ToGain(master_q12);
    gPanel->syncMasterPot(*gCtrl, gMasterGain);
}

// ruido_fm1_poll_panel (the scan's edges and masks, matrix order: buttons 0..13, keys F3..G5, encoders 0..6)
void ruido_glue_panel(uint32_t now_ms, uint32_t pressed, uint32_t released, uint32_t buttons,
                      const int32_t enc_steps[7], uint32_t notes, uint32_t note_edges) {
    const bool btnOrEnc = gPanel->pollButtonsAndEncoders(*gCtrl, pressed, released, buttons, enc_steps);
    const bool keyTouched = gPanel->pollKeysAndPressure(*gCtrl, notes, note_edges);
    if (btnOrEnc || keyTouched) {
        gNvs->lastPanelTouchMs = now_ms;
        gRenderer->notifyUserInput();
    }
}

// ruido_fm1_on_midi_rt (the stamp: the device's seconds, modulo 64 s as the TIMER4 count wraps)
void ruido_glue_midi_rt(uint8_t status, float seconds) {
    gCtrl->onMidiRealtime(status, seconds);
    ++gMidiRtEvents;
}

// ruido_fm1_nvs_poll_save: 1 = nothing left pending
int ruido_glue_nvs_poll(uint32_t now_ms, uint32_t force) {
    (void)ruido::fm1::pollAndSaveNvsBlobs(now_ms, force, fm1core_ru_blob_save, nullptr, *gCtrl, *gNvs,
                                          fm1core_ru_tail_write);
    return gNvs->pendingSlotMask == 0u && gNvs->pendingSettingsSave == 0u && gNvs->pendingLiveSave == 0u;
}

// ruido_fm1_tick_and_prepare_ms, then the 15 strips (ruido_fm1_render_strip_be, native-endian here) into px
void ruido_glue_frame(uint32_t elapsed_ms, uint16_t* px) {
    const uint32_t ms = (elapsed_ms == 0u) ? 16u : ((elapsed_ms > 250u) ? 250u : elapsed_ms);
    const float dt = static_cast<float>(ms) * 0.001f;
    gCtrl->tick(dt, gDsp->telemetry());
    gRenderer->ingestScopeRing(gDsp->scopeBuffer());
    gRenderer->prepareFrame(dt, gCtrl->state(), gCtrl->hud(), gDsp->telemetry());
    for (uint16_t y = 0u; y < ruido::kDisplayHeight; y = static_cast<uint16_t>(y + ruido::kScanlineStripHeight)) {
        if ((y & 63u) == 0u && y != 0u) {
            gRenderer->ingestScopeRing(gDsp->scopeBuffer());
        }
        gRenderer->renderStrip(y, ruido::kScanlineStripHeight, px + static_cast<size_t>(y) * ruido::kDisplayWidth);
    }
}

// the LEDs as the controller wants them (LedState, perceptual 0..1) at Fm1LedModulator::updateTargets' places:
// level[col * 4 + row - 1] 0..255; PLAY's green die (col 8, row 1) is buttonLeds[PlayStop], the STOP die at
// BTN_PLAY's own place (col 10, row 2) stopLed
void ruido_glue_leds(uint8_t level[44]) {
    using namespace ruido::fm1;
    const ruido::LedState& l = gCtrl->leds();
    auto put = [&](uint8_t col, uint8_t row, float b) noexcept {
        if (col < kFm1NumCols && row >= 1u && row <= 4u) {
            const float c = ruido::clampf(b, 0.0f, 1.0f);
            level[col * 4u + row - 1u] = static_cast<uint8_t>(c * 255.0f + 0.5f);
        }
    };
    for (uint32_t i = 0u; i < 44u; ++i) {
        level[i] = 0u;
    }
    for (uint8_t b = 0u; b < ruido::kNumPanelButtons; ++b) {
        if (b == static_cast<uint8_t>(ruido::ButtonId::PlayStop)) {
            put(kPlayGreenCol, kPlayGreenRow, l.buttonLeds[b]);
        } else {
            put(kFm1KeyIdCoord[b].col, kFm1KeyIdCoord[b].row, l.buttonLeds[b]);
        }
    }
    put(kStopRedCol, kStopRedRow, l.stopLed);
    for (uint8_t w = 0u; w < ruido::kNumWhiteKeys; ++w) {
        const MatrixCoord mc = kFm1KeyIdCoord[14u + kWhiteKeyToNoteBit[w]];
        put(mc.col, mc.row, l.whiteKeyLeds[w]);
    }
    for (uint8_t k = 0u; k < ruido::kNumBlackKeys; ++k) {
        const MatrixCoord mc = kFm1KeyIdCoord[14u + kBlackKeyToNoteBit[k]];
        put(mc.col, mc.row, l.blackKeyLeds[k]);
    }
}

// Core 1's renderQuantizedBlock64, per 64 frames: the engine's float stereo, MASTER already in it, TPDF-dithered
// to Q15 (Fm1TpdfQuantizer: +-1.45 to +-32762), interleaved
void ruido_glue_audio(int16_t* out, uint32_t frames) {
    float left[ruido::kBlockFrames];
    float right[ruido::kBlockFrames];
    uint32_t off = 0u;
    while (off < frames) {
        const uint32_t n = (frames - off > ruido::kBlockFrames) ? static_cast<uint32_t>(ruido::kBlockFrames) : frames - off;
        gDsp->processBlock(left, right, n);
        for (uint32_t i = 0u; i < n; ++i) {
            out[2u * (off + i)] = gQuant->quantizeLeftUnity(left[i]);
            out[2u * (off + i) + 1u] = gQuant->quantizeRightUnity(right[i]);
        }
        off += n;
    }
}

void ruido_glue_info(char* buf, uint32_t n) {
    const ruido::HudState& h = gCtrl->hud();
    const ruido::EngineControlState& st = gCtrl->state();
    const int hz = static_cast<int>(st.voices[0].frequencyHz + 0.5f);
    const char* run = st.global.masterRun ? "1" : "0";
    // (no printf here: the C unit formats; a fixed few fields)
    uint32_t k = 0u;
    auto put = [&](const char* s) noexcept {
        while (*s && k + 1u < n) {
            buf[k++] = *s++;
        }
    };
    auto num = [&](int v) noexcept {
        char t[12];
        int i = 0;
        if (v < 0) {
            put("-");
            v = -v;
        }
        do {
            t[i++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v && i < 11);
        while (i && k + 1u < n) {
            buf[k++] = t[--i];
        }
    };
    put("run ");
    put(run);
    put(" mode ");
    num(static_cast<int>(h.mode));
    put(" model ");
    num(static_cast<int>(h.keyboardModel));
    put(" scene ");
    num(static_cast<int>(gCtrl->snapshots().activeSlot()));
    put(" osc1 ");
    num(hz);
    put(" intro ");
    num(gRenderer->isBootIntroActive() ? 1 : 0);
    put(" saves ");
    num(gNvs->savesOk);
    if (n) {
        buf[k] = 0;
    }
}

} // extern "C"
