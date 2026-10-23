// SPDX-License-Identifier: GPL-3.0-only
// The editor (see Editor.h).
#include "Editor.h"

#include "ScreenCapture.h"
#include "StandaloneHooks.h"
#include "VideoCapture.h"

#include <cmath>

using juce::Rectangle;

// ========================================================== screen view ===
ScreenView::ScreenView(PanelComponent &panel) : panel_(panel)
{
    setWantsKeyboardFocus(true);
    setOpaque(true);
}

Rectangle<float> ScreenView::bezelArea() const
{
    const float w = (float)juce::jmin(getWidth(), getHeight());
    return getLocalBounds().toFloat().withSizeKeepingCentre(w, w).reduced(juce::jmax(6.0f, w * 0.028f));
}

Rectangle<float> ScreenView::lcdAreaFor(float scale) const
{
    const float w = (float)juce::jmin(getWidth(), getHeight());
    const Rectangle<float> avail = bezelArea().reduced(juce::jmax(8.0f, w * 0.032f));
    const float phys = std::floor(avail.getWidth() * scale);
    float size = phys;
    const int n = (int)(phys / EMU_LCD_W);
    if (n >= 1 && (float)(n * EMU_LCD_W) >= 0.94f * phys)   // (a whole multiple within 6 %: pixel for pixel)
        size = (float)(n * EMU_LCD_W);
    const float x = std::round((avail.getCentreX() * scale - size / 2)), y = std::round((avail.getCentreY() * scale - size / 2));
    return {x / scale, y / scale, size / scale, size / scale};
}

Rectangle<float> ScreenView::lcdArea() const { return lcdAreaFor(lastScale_); }

void ScreenView::paint(juce::Graphics &g)
{
    paintBody(g);
    paintLcd(g);
}

void ScreenView::paintBody(juce::Graphics &g)
{
    float s = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (!(s > 0))
        s = 1;
    lastScale_ = s;
    const Palette &p = panel_.palette();
    // the body's colour around a black bezel, as on the unit
    g.fillAll(p.plate);
    const Rectangle<float> bz = bezelArea();
    const float rad = juce::jmax(8.0f, bz.getWidth() * 0.035f);
    g.setColour(p.edge);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.5f), rad + 3, 1.0f);
    juce::Path bezel;
    bezel.addRoundedRectangle(bz, rad);
    juce::DropShadow(juce::Colours::black.withAlpha(0.35f), juce::roundToInt(rad), {0, 2}).drawForPath(g, bezel);
    g.setColour(p.screenBezel);
    g.fillPath(bezel);
    g.setColour(p.outline);
    g.strokePath(bezel, juce::PathStrokeType(1.0f));
}

void ScreenView::paintLcd(juce::Graphics &g)
{
    float s = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (!(s > 0))
        s = 1;
    // the LCD in physical pixels
    const Rectangle<float> l = lcdAreaFor(s);
    const juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::scale(1.0f / s));
    const Rectangle<int> px(juce::roundToInt(l.getX() * s), juce::roundToInt(l.getY() * s), juce::roundToInt(l.getWidth() * s),
                            juce::roundToInt(l.getHeight() * s));
    const juce::Image &src = panel_.lcdImage();
    const int side = px.getWidth();
    if (side % EMU_LCD_W == 0) {
        lastN_ = side / EMU_LCD_W;
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImageTransformed(src, juce::AffineTransform::scale((float)lastN_).translated((float)px.getX(), (float)px.getY()));
        return;
    }
    // up to the next whole multiple nearest-neighbour (at most 4x: past that the smoothing is under a pixel), then
    // smoothed down: sharp pixel edges at any size
    lastN_ = 0;
    const int m = juce::jlimit(1, 4, (side + EMU_LCD_W - 1) / EMU_LCD_W);
    if (!cache_.isValid() || cacheN_ != m || cacheSerial_ != serial_ || cacheSrc_ != (const void *)&src) {
        cache_ = juce::Image(juce::Image::ARGB, EMU_LCD_W * m, EMU_LCD_H * m, false, juce::SoftwareImageType());
        const juce::Image::BitmapData sd(src, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dd(cache_, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < cache_.getHeight(); y++) {
            const uint32_t *sr = (const uint32_t *)(const void *)sd.getLinePointer(y / m);
            uint32_t *dr = (uint32_t *)(void *)dd.getLinePointer(y);
            for (int x = 0; x < cache_.getWidth(); x++)
                dr[x] = sr[x / m];
        }
        cacheN_ = m;
        cacheSerial_ = serial_;
        cacheSrc_ = &src;
    }
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(cache_, px.toFloat());
}

void ScreenView::mouseDown(const juce::MouseEvent &e)
{
    if (isShowing())
        grabKeyboardFocus();
    if (!e.mods.isPopupMenu())
        return;
    if (onScreenMenu && bezelArea().contains(e.position))   // (the screen: copy it; the body around it: the settings)
        onScreenMenu(e);
    else if (onContextMenu)
        onContextMenu();
}

// ================================================================ toast ===
void Toast::show(const juce::String &text)
{
    text_ = text;
    shownAt_ = juce::Time::getMillisecondCounterHiRes();
    alpha_ = 1.0f;
    setVisible(true);
    repaint();
    startTimerHz(30);
}

int Toast::preferredWidth() const { return juce::roundToInt(Look::textWidth(Look::font(13.0f, Look::SemiBold), text_)) + 32; }

void Toast::timerCallback()
{
    const double t = juce::Time::getMillisecondCounterHiRes() - shownAt_;
    alpha_ = t < 1400.0 ? 1.0f : juce::jmax(0.0f, 1.0f - (float)(t - 1400.0) / 400.0f);
    if (alpha_ <= 0.0f) {
        stopTimer();
        setVisible(false);
    }
    repaint();
}

void Toast::paint(juce::Graphics &g)
{
    const Rectangle<float> r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(Look::col(Look::C::raised).withMultipliedAlpha(0.96f * alpha_));
    g.fillRoundedRectangle(r, r.getHeight() / 2);
    g.setColour(Look::col(Look::C::border).withMultipliedAlpha(alpha_));
    g.drawRoundedRectangle(r, r.getHeight() / 2, 1.0f);
    g.setColour(Look::col(Look::C::text).withMultipliedAlpha(alpha_));
    g.setFont(Look::font(13.0f, Look::SemiBold));
    g.drawText(text_, getLocalBounds(), juce::Justification::centred, false);
}

// =============================================================== editor ===
void FM1Editor::Constrainer::checkBounds(Rectangle<int> &b, const Rectangle<int> &previous, const Rectangle<int> &limits, bool top,
                                         bool left, bool bottom, bool right)
{
    const int m = mode ? mode() : FM1Processor::kViewDevice;
    const bool vertical = (top || bottom) && !(left || right);
    if (m == FM1Processor::kViewScreen) {        // square
        const int s = juce::jlimit(getMinimumWidth(), getMaximumWidth(), vertical ? b.getHeight() : b.getWidth());
        b.setSize(s, s);
    } else if (!(bigLcd && bigLcd())) {          // the panel's aspect (with the big LCD: free, letterboxed)
        const double a = PanelComponent::frameAspect(false);
        int w = vertical ? juce::roundToInt(b.getHeight() * a) : b.getWidth();
        w = juce::jlimit(getMinimumWidth(), getMaximumWidth(), w);
        b.setSize(w, juce::roundToInt(w / a));
    }
    juce::ComponentBoundsConstrainer::checkBounds(b, previous, limits, top, left, bottom, right);
}

FM1Editor::FM1Editor(FM1Processor &p)
    : juce::AudioProcessorEditor(p), proc_(p), store_(FM1Processor::home()), panel_(p), screen_(panel_), corner_(look_), video_(p.videoSession()), sheet_(*this)
{
    setLookAndFeel(&look_);
    addAndMakeVisible(panel_);
    addChildComponent(screen_);
    addAndMakeVisible(corner_);
    if (video_) {                                // (the Standalone: the recorder, behind the sheet)
        addAndMakeVisible(rec_);
        rec_.seconds = [this] { return video_ ? video_->seconds() : 0.0; };
        rec_.onClick = [this] {
            if (rec_.state() == RecordButton::kSaved && !video_->recording()) {
                video_->lastFile().revealToUser();
                savedUntil_ = 0;
                rec_.setState(RecordButton::kIdle);
                layoutChrome();
            } else {
                toggleRecording();
            }
        };
        rec_.setTooltip(juce::String::fromUTF8("Record a video of the firmware playing (\xe2\x87\xa7\xe2\x8c\x98" "R)"));
        video_->onChange = [this] { videoChanged(); };
        videoSerial_ = video_->serial();
        if (video_->recording())
            rec_.setState(RecordButton::kRecording);
    }
    addChildComponent(sheet_);

    panel_.setButtonNames(proc_.buttonNames());
    panel_.setTheme(proc_.getTheme());
    look_.setAccentFrom(proc_.getTheme());
    themeSerial_ = proc_.themeSerial();
    panel_.setBigLcd(proc_.getBigLcd());
    panel_.onBigLcdToggled = [this](bool on) { setBigLcd(on); };
    panel_.onContextMenu = [this] { openSettings(); };
    panel_.onLcdChanged = [this] {
        if (screen_.isVisible()) {
            screen_.lcdChanged();
        }
    };
    screen_.onContextMenu = [this] { openSettings(); };
    panel_.onScreenMenu = [this](const juce::MouseEvent &e) { showScreenMenu(e); };
    panel_.onShiftHint = [this] {                // (once per user: settings.json remembers it)
        if (!sheet_.isOpen() && !store_.flag("shiftLatchHint") && store_.setFlag("shiftLatchHint", true))
            showToast("Shift-click to hold");
    };
    screen_.onScreenMenu = [this](const juce::MouseEvent &e) { showScreenMenu(e); };
    addChildComponent(toast_);
    corner_.onClick = [this] { openSettings(); };
    corner_.setTooltip("Settings (or right-click the panel)");

    addMouseListener(&watcher_, true);
    chrome_.fn = [this] { chromeTick(); };

    constrainer_.mode = [this] { return mode_; };
    constrainer_.bigLcd = [this] { return panel_.bigLcd(); };
    mode_ = proc_.getViewMode();
    panel_.setVisible(mode_ == FM1Processor::kViewDevice);
    screen_.setVisible(mode_ == FM1Processor::kViewScreen);
    updateLimits();
    setConstrainer(&constrainer_);
    setResizable(true, true);
    sheet_.toFront(false);
    if (mode_ == FM1Processor::kViewScreen) {
        const int s = proc_.getScreenSize() > 0 ? proc_.getScreenSize() : defaultScreenSize();
        setSize(juce::jlimit(kMinScreen, kMaxScreen, s), juce::jlimit(kMinScreen, kMaxScreen, s));
    } else {
        juce::Point<int> sz = proc_.getEditorSize();
        if (sz.x <= 0 || sz.y <= 0)
            sz = defaultSize();
        const int w = juce::jlimit(kMinW, kMaxW, sz.x);
        setSize(w, panel_.bigLcd() ? juce::jlimit(300, 4000, sz.y) : juce::roundToInt((float)w / PanelComponent::frameAspect(false)));
    }
    lastCore_ = juce::String(proc_.getCoreInfo().info.id);
    refreshChrome();
    startTimerHz(4);
}

FM1Editor::~FM1Editor()
{
    if (video_)
        video_->onChange = nullptr;
    stopTimer();
    chrome_.stopTimer();
    removeMouseListener(&watcher_);
    setLookAndFeel(nullptr);
}

void FM1Editor::paint(juce::Graphics &g) { g.fillAll(Look::col(Look::C::window)); }

void FM1Editor::resized()
{
    const Rectangle<int> r = getLocalBounds();
    panel_.setBounds(r);
    screen_.setBounds(r);
    sheet_.setBounds(r);
    layoutChrome();
    if (mode_ == FM1Processor::kViewScreen)
        proc_.setScreenSize(getWidth());
    else
        proc_.setEditorSize(getWidth(), getHeight());
}

void FM1Editor::focusGained(FocusChangeType)
{
    if (sheet_.isOpen())
        sheet_.grabKeyboardFocus();
    else
        focusActiveView();
}

void FM1Editor::focusActiveView()
{
    juce::Component &v = activeView();
    if (v.isShowing())
        v.grabKeyboardFocus();
}

void FM1Editor::parentHierarchyChanged()
{
    if (isStandalone() && !standaloneDone_ && getParentComponent()) {
        standaloneDone_ = true;
        juce::Component::SafePointer<FM1Editor> self(this);
        juce::MessageManager::callAsync([self] {
            if (self)
                fm1standalone::tweakWindow(*self);
        });
    }
}

void FM1Editor::updateLimits()
{
    if (mode_ == FM1Processor::kViewScreen) {
        constrainer_.setSizeLimits(kMinScreen, kMinScreen, kMaxScreen, kMaxScreen);
    } else if (panel_.bigLcd()) {
        constrainer_.setSizeLimits(kMinW, 300, kMaxW, 4000);
    } else {
        const float a = PanelComponent::frameAspect(false);
        constrainer_.setSizeLimits(kMinW, juce::roundToInt((float)kMinW / a), kMaxW, juce::roundToInt((float)kMaxW / a));
    }
}

void FM1Editor::setViewMode(int m)
{
    m = juce::jlimit(0, 1, m);
    proc_.setViewMode(m);
    if (m == mode_)
        return;
    mode_ = m;
    panel_.setVisible(m == FM1Processor::kViewDevice);
    screen_.setVisible(m == FM1Processor::kViewScreen);
    updateLimits();
    if (m == FM1Processor::kViewScreen) {
        const int s = juce::jlimit(kMinScreen, kMaxScreen, proc_.getScreenSize() > 0 ? proc_.getScreenSize() : defaultScreenSize());
        setSize(s, s);
    } else {
        juce::Point<int> sz = proc_.getEditorSize();
        if (sz.x <= 0 || sz.y <= 0)
            sz = defaultSize();
        const int w = juce::jlimit(kMinW, kMaxW, sz.x);
        setSize(w, panel_.bigLcd() ? juce::jlimit(300, 4000, sz.y) : juce::roundToInt((float)w / PanelComponent::frameAspect(false)));
    }
    resized();
    if (!sheet_.isOpen())
        focusActiveView();
}

void FM1Editor::setBigLcd(bool on)
{
    panel_.setBigLcd(on);
    proc_.setBigLcd(on);
    if (mode_ == FM1Processor::kViewDevice) {
        updateLimits();
        if (!on)                                 // back to the panel's aspect
            setSize(getWidth(), juce::roundToInt((float)getWidth() / PanelComponent::frameAspect(false)));
    }
    layoutChrome();
    sheet_.refresh();
}

void FM1Editor::applyTheme(const Theme &t)
{
    proc_.setTheme(t);
    themeSerial_ = proc_.themeSerial();
    panel_.setTheme(t);
    look_.setAccentFrom(t);
    screen_.repaint();
    sheet_.repaint();
}

void FM1Editor::coreChanged()
{
    lastCore_ = juce::String(proc_.getCoreInfo().info.id);
    panel_.setButtonNames(proc_.buttonNames());
    refreshChrome();
    sheet_.refresh();
}

void FM1Editor::openSettings(bool animate)
{
    hideChromeNow();
    sheet_.open(animate);
}

// ------------------------------------------------------------- chrome ---
// the button's place: on the body's empty top-right corner (the drawing's units 860, 46: clear of KNOB4, its printed
// name and caption at every size; ui_render_test checks it), 17.5 units wide within 22 .. 28 points; with the big LCD
// shown, the window's corner (the panel is below the LCD's band); in the screen view, on the bezel's corner
Rectangle<float> FM1Editor::cornerCircle() const
{
    if (mode_ == FM1Processor::kViewScreen) {
        // (centred on the bezel's rounded corner, in the body's margin)
        const float d = juce::jlimit(22.0f, 28.0f, std::round((float)juce::jmin(getWidth(), getHeight()) * 0.045f));
        const float c = juce::jmax(d / 2 + 3, screen_.bezelArea().getX());
        return Rectangle<float>(d, d).withCentre({(float)getWidth() - c, c});
    }
    const float d = juce::jlimit(22.0f, 28.0f, std::round(17.5f * panel_.pointsPerUnit()));
    if (panel_.bigLcd())                         // (the window's corner: above the panel, over the big LCD's band)
        return Rectangle<float>(d, d).withCentre({(float)getWidth() - d / 2 - 8, d / 2 + 8});
    return Rectangle<float>(d, d).withCentre(panel_.unitToLocal(860.0f, 46.0f) + panel_.getPosition().toFloat());
}

void FM1Editor::layoutChrome()
{
    const Rectangle<float> c = cornerCircle();
    corner_.setBounds(c.expanded((float)CornerButton::kPad).getSmallestIntegerContainer());
    if (video_) {                                // (the record button: from its circle rightwards)
        const Rectangle<float> rc = recordCircle();
        rec_.setDiameter(rc.getWidth());
        const Rectangle<float> r(rc.getX(), rc.getY(), (float)rec_.preferredWidth(), rc.getHeight());
        rec_.setBounds(r.expanded((float)RecordButton::kPad).getSmallestIntegerContainer());
    }
}

// the record button: the corner button's mirror image (the body's empty top-left corner, units 44, 44: clear of MASTER
// and its name; ui_render_test checks it, the recording pill too), the window's corner over the big LCD, the bezel's
// top-left corner in the screen view
Rectangle<float> FM1Editor::recordCircle() const
{
    const Rectangle<float> c = cornerCircle();
    const float d = c.getWidth();
    if (mode_ == FM1Processor::kViewScreen || panel_.bigLcd())
        return c.withX((float)getWidth() - c.getRight());
    // (above MASTER's name, which starts at y 59: at most 26 units tall, so a small window gets a smaller button)
    const float dr = juce::jmin(d, 26.0f * panel_.pointsPerUnit());
    return Rectangle<float>(dr, dr).withCentre(panel_.unitToLocal(44.0f, 44.0f) + panel_.getPosition().toFloat());
}

void FM1Editor::toggleRecording()
{
    if (!video_)
        return;
    if (video_->recording()) {
        video_->stop();
        return;
    }
    if (!video_->start())
        showToast(video_->lastError());
}

void FM1Editor::videoChanged()
{
    if (!video_ || video_->serial() == videoSerial_)
        return;
    videoSerial_ = video_->serial();
    if (video_->recording()) {
        rec_.setState(RecordButton::kRecording);
        showToast("Recording " + video_->currentFile().getFileName());
    } else {
        const fm1video::Recorder::Result &r = video_->lastResult();
        const bool kept = r.file.existsAsFile();
        rec_.setState(kept ? RecordButton::kSaved : RecordButton::kIdle);
        savedUntil_ = kept ? juce::Time::getMillisecondCounterHiRes() + 8000.0 : 0.0;
        showToast(r.error.isNotEmpty() ? r.error : "Saved " + r.file.getFileName() + " (" + fm1video::formatTime(r.seconds) + ")");
    }
    layoutChrome();
    if (sheet_.isOpen())
        sheet_.refresh();
}

void FM1Editor::activity()
{
    lastMove_ = juce::Time::getMillisecondCounterHiRes();
    if (!chrome_.isTimerRunning() || chrome_.getTimerInterval() > 20) {
        lastTick_ = lastMove_;
        chrome_.startTimerHz(60);
    }
}

void FM1Editor::chromeTick()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float)juce::jlimit(0.0, 100.0, now - lastTick_);
    lastTick_ = now;
    const bool want = !sheet_.isOpen() && (now - lastMove_ < 1500.0 || corner_.hovered() || corner_.isMouseButtonDown() || rec_.isMouseOver());
    const bool alarm = halted_;
    float f = corner_.fade();
    f = want || alarm ? juce::jmin(1.0f, f + dt / 150.0f) : juce::jmax(0.0f, f - dt / 350.0f);
    corner_.setFade(f);
    rec_.setFade(f);
    if (f <= 0.0f && !want)
        chrome_.stopTimer();
    else if (f >= 1.0f && (want || alarm) && chrome_.getTimerInterval() < 50)
        chrome_.startTimer(100);                 // (shown: only the timeout to watch)
}

void FM1Editor::showChromeNow()
{
    lastMove_ = juce::Time::getMillisecondCounterHiRes();
    corner_.setFade(1.0f);
    lastTick_ = lastMove_;
    chrome_.startTimer(100);
}

void FM1Editor::hideChromeNow()
{
    lastMove_ = -1e9;
    corner_.setFade(0.0f);
    chrome_.stopTimer();
}

void FM1Editor::refreshChrome()
{
    const RemoteLink::Status st = proc_.remoteStatus();
    juce::Colour led = juce::Colours::transparentBlack;
    if (st.state == RemoteLink::kConnected)
        led = Look::col(Look::C::green);
    else if (proc_.getRemoteEnabled() || st.state != RemoteLink::kOff)
        led = Look::col(Look::C::amber);
    corner_.setLed(led);
    const bool halted = proc_.getCoreInfo().halted;
    halted_ = halted;
    corner_.setAlarm(halted);
    if (halted && corner_.fade() < 1.0f && !chrome_.isTimerRunning() && !sheet_.isOpen()) {
        lastTick_ = juce::Time::getMillisecondCounterHiRes();
        chrome_.startTimerHz(60);
    }
}

void FM1Editor::timerCallback()
{
    refreshChrome();
    if (savedUntil_ > 0 && juce::Time::getMillisecondCounterHiRes() > savedUntil_) {   // (the take's "Show in Finder" ends)
        savedUntil_ = 0;
        if (rec_.state() == RecordButton::kSaved)
            rec_.setState(RecordButton::kIdle);
        layoutChrome();
    }
    if (juce::String(proc_.getCoreInfo().info.id) != lastCore_)   // (a core switch: its button labels)
        coreChanged();
    if (proc_.themeSerial() != themeSerial_) {   // (a state restore set another theme)
        themeSerial_ = proc_.themeSerial();
        const Theme t = proc_.getTheme();
        panel_.setTheme(t);
        look_.setAccentFrom(t);
        screen_.repaint();
        sheet_.repaint();
    }
    if (proc_.getBigLcd() != panel_.bigLcd())
        setBigLcd(proc_.getBigLcd());
    if (proc_.getViewMode() != mode_)             // (a state restore)
        setViewMode(proc_.getViewMode());
    layoutChrome();                               // (the panel snaps to the screen's pixel scale at its first paint)
    if (sheet_.isOpen())
        sheet_.refresh();
}

// ------------------------------------------------------- copy the screen ---
static juce::String px(int scale)
{
    const int n = fm1screen::kW * scale;
    return juce::String(n) + juce::String::fromUTF8("\xc3\x97") + juce::String(n);
}

juce::PopupMenu FM1Editor::screenMenu()
{
    const juce::String times = juce::String::fromUTF8("\xc3\x97"), ellipsis = juce::String::fromUTF8("\xe2\x80\xa6");
    juce::PopupMenu m;
    m.setLookAndFeel(&look_);
    for (int s : {1, 2, 4}) {
        juce::PopupMenu::Item it(s == 1 ? juce::String("Copy screen") : "Copy screen " + times + juce::String(s));
        it.itemID = s == 1 ? kCopy1 : s == 2 ? kCopy2 : kCopy4;
        if (s == copyScale_)
            it.shortcutKeyDescription = juce::String::fromUTF8("\xe2\x87\xa7\xe2\x8c\x98" "C");   // (shift, cmd)
        m.addItem(it);
    }
    juce::PopupMenu save;
    save.setLookAndFeel(&look_);
    save.addItem(kSave1, px(1) + ellipsis);
    save.addItem(kSave2, px(2) + " (" + times + "2)" + ellipsis);
    save.addItem(kSave4, px(4) + " (" + times + "4)" + ellipsis);
    m.addSubMenu("Save screen as PNG", save);
    juce::PopupMenu adv;
    adv.setLookAndFeel(&look_);
    adv.addItem(kCopyRaw, "Copy raw RGB565 (.bin)");
    adv.addItem(kSaveRaw, "Save raw RGB565 as .bin" + ellipsis);
    m.addSubMenu("Advanced", adv);
    m.addSeparator();
    m.addItem(kFreeze, "Freeze screen", true, panel_.frozen());
    m.addSeparator();
    m.addItem(kSettings, "Settings" + ellipsis);
    return m;
}

void FM1Editor::showScreenMenu(const juce::MouseEvent &e)
{
    if (e.mods.isAltDown()) {                     // (Option + right-click: the shown frame at once, no menu)
        copyScreen(1);
        return;
    }
    hideChromeNow();
    juce::Component::SafePointer<FM1Editor> self(this);
    // (inside the editor, not a window of its own: a host's window keeps the focus, and the tests can draw it)
    screenMenu().showMenuAsync(juce::PopupMenu::Options()
                                   .withParentComponent(this)
                                   .withTargetScreenArea(juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1)),
                               [self](int r) {
                                   if (self && r > 0)
                                       self->screenCommand(r);
                               });
}

void FM1Editor::screenCommand(int cmd)
{
    switch (cmd) {
    case kCopy1: copyScreen(1); break;
    case kCopy2: copyScreen(2); break;
    case kCopy4: copyScreen(4); break;
    case kSave1: saveScreen(1); break;
    case kSave2: saveScreen(2); break;
    case kSave4: saveScreen(4); break;
    case kCopyRaw: copyScreenRaw(); break;
    case kSaveRaw: saveScreenRaw(); break;
    case kFreeze: setScreenFrozen(!panel_.frozen()); break;
    case kSettings: openSettings(); break;
    default: break;
    }
}

bool FM1Editor::copyScreen(int scale)
{
    scale = scale == 2 || scale == 4 ? scale : 1;
    const bool ok = fm1screen::copyImage(panel_.shownLcd().data(), scale);
    showToast(ok ? "Screen copied (" + px(scale) + ")" : juce::String("Couldn't copy the screen"));
    return ok;
}

bool FM1Editor::copyScreenRaw()
{
    const bool ok = fm1screen::copyRaw(panel_.shownLcd().data());
    showToast(ok ? juce::String("Raw RGB565 copied (115,200 bytes)") : juce::String("Couldn't copy the screen"));
    return ok;
}

juce::String FM1Editor::screenFileName(const juce::String &ext) const
{
    return fileNameFor(proc_, ext);
}

juce::String FM1Editor::fileNameFor(const FM1Processor &p, const juce::String &ext)
{
    juce::String id, version;
    if (p.isEmulated()) {
        const FM1Processor::EmuFirmware fw = p.getEmuFirmware();
        id = fw.id;
        version = fw.version;
    } else {
        const FM1Processor::CoreStatus st = p.getCoreInfo();
        id = juce::String(st.info.id);
        version = juce::String(st.info.version);
    }
    return fm1screen::defaultFileName(id, version, juce::Time::getCurrentTime(), ext);
}

void FM1Editor::saveScreen(int scale)
{
    scale = scale == 2 || scale == 4 ? scale : 1;
    // (the frame as shown now: the chooser is open for a while)
    auto frame = std::make_shared<std::array<uint16_t, EMU_LCD_W * EMU_LCD_H>>(panel_.shownLcd());
    const juce::File dir = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Save the screen as PNG", dir.getChildFile(screenFileName(".png")), "*.png");
    juce::Component::SafePointer<FM1Editor> self(this);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [self, frame, scale](const juce::FileChooser &fc) {
                              juce::File f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              if (!f.hasFileExtension("png"))
                                  f = f.withFileExtension("png");
                              const bool ok = fm1screen::savePng(f, frame->data(), scale);
                              if (self)
                                  self->showToast(ok ? "Saved " + f.getFileName() : "Couldn't save " + f.getFileName());
                          });
}

void FM1Editor::saveScreenRaw()
{
    auto frame = std::make_shared<std::array<uint16_t, EMU_LCD_W * EMU_LCD_H>>(panel_.shownLcd());
    const juce::File dir = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Save the screen's raw RGB565 (little-endian)", dir.getChildFile(screenFileName(".bin")),
                                                   "*.bin");
    juce::Component::SafePointer<FM1Editor> self(this);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [self, frame](const juce::FileChooser &fc) {
                              juce::File f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              if (!f.hasFileExtension("bin"))
                                  f = f.withFileExtension("bin");
                              const bool ok = fm1screen::saveRaw(f, frame->data());
                              if (self)
                                  self->showToast(ok ? "Saved " + f.getFileName() : "Couldn't save " + f.getFileName());
                          });
}

void FM1Editor::setScreenFrozen(bool on)
{
    panel_.setFrozen(on);
    showToast(on ? juce::String("Screen frozen (the firmware runs on)") : juce::String("Screen live"));
    sheet_.refresh();
}

void FM1Editor::showToast(const juce::String &text)
{
    // over the LCD: the screen view's, the big one, or the panel's own
    Rectangle<float> lcd;
    if (mode_ == FM1Processor::kViewScreen) {
        lcd = screen_.lcdArea() + screen_.getPosition().toFloat();
    } else {
        const PanelComponent::Layout &L = panel_.layout();
        if (L.scale > 0)
            lcd = (panel_.bigLcd() ? L.lcdBig : L.lcdSmall).toFloat().transformedBy(juce::AffineTransform::scale(1.0f / L.scale)) +
                  panel_.getPosition().toFloat();
    }
    if (lcd.isEmpty())
        lcd = getLocalBounds().toFloat();
    toast_.show(text);
    const int w = juce::jmin(toast_.preferredWidth(), getWidth() - 16), h = 32;
    toast_.setBounds(Rectangle<int>(w, h).withCentre(lcd.getCentre().toInt()).constrainedWithin(getLocalBounds()));
    toast_.toFront(false);
}

bool FM1Editor::keyPressed(const juce::KeyPress &k)
{
    const juce::ModifierKeys m = k.getModifiers();
    const int c = k.getKeyCode();
    if (m.isCommandDown() && m.isShiftDown() && !m.isAltDown() && (c == 'C' || c == 'c')) {   // Cmd+Shift+C
        copyScreen(copyScale_);
        return true;
    }
    if (m.isCommandDown() && m.isShiftDown() && !m.isAltDown() && (c == 'R' || c == 'r') && video_) {   // Cmd+Shift+R
        toggleRecording();
        return true;
    }
    return false;
}
