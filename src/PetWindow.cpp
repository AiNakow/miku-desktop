#include "PetWindow.h"
#include "AnimationEngine.h"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QBitmap>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QScreen>
#include <QSettings>
#include <QTime>
#include <QWindow>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
QPoint clampToVisibleArea(const QPoint &candidate, const QSize &windowSize)
{
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return candidate;

    const QPoint center = candidate + QPoint(windowSize.width() / 2, windowSize.height() / 2);
    QScreen *targetScreen = QGuiApplication::screenAt(center);
    if (!targetScreen)
        targetScreen = QGuiApplication::screenAt(candidate);
    if (!targetScreen)
        targetScreen = QGuiApplication::primaryScreen();
    if (!targetScreen)
        return candidate;

    const QRect avail = targetScreen->availableGeometry();
    const int minX = avail.left();
    const int minY = avail.top();
    const int maxX = std::max(minX, avail.right() - windowSize.width() + 1);
    const int maxY = std::max(minY, avail.bottom() - windowSize.height() + 1);

    return {
        std::clamp(candidate.x(), minX, maxX),
        std::clamp(candidate.y(), minY, maxY)
    };
}

QRect opaqueBounds(const QImage &image)
{
    if (image.isNull())
        return {};

    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    int minX = argb.width();
    int minY = argb.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < argb.height(); ++y)
    {
        const QRgb *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x)
        {
            if (qAlpha(row[x]) <= 3)
                continue;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY)
        return {};
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}
}

// ── Random animation pool (idle candidates) ────────────────────────────────
static const QStringList s_randomPool = {
    QStringLiteral("dizzy"),
    QStringLiteral("drink"),
    QStringLiteral("eatcake"),
    QStringLiteral("excited"),
    QStringLiteral("hairflip"),
    QStringLiteral("heart"),
    QStringLiteral("hello"),
    QStringLiteral("jump"),
    QStringLiteral("listenmusic"),
    QStringLiteral("notlistenmusic"),
    QStringLiteral("rolling"),
    QStringLiteral("rotate"),
    QStringLiteral("shy"),
    QStringLiteral("watch"),
};

static const QStringList s_clickRandomPool = {
    QStringLiteral("excited"),
    QStringLiteral("heart"),
    QStringLiteral("jump"),
    QStringLiteral("rotate"),
    QStringLiteral("hairflip"),
    QStringLiteral("watch"),
    QStringLiteral("shy"),
    QStringLiteral("question"),
    QStringLiteral("drink"),
    QStringLiteral("listenmusic"),
    QStringLiteral("notlistenmusic"),
    QStringLiteral("punching"),
};

const QStringList &PetWindow::randomPool()
{
    return s_randomPool;
}

const QStringList &PetWindow::clickRandomPool()
{
    return s_clickRandomPool;
}

bool PetWindow::isAngryLocked() const
{
    return m_angryLocked;
}

// ── Constructor ─────────────────────────────────────────────────────────────

PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
{
    m_startupTimestamp = QDateTime::currentMSecsSinceEpoch();
    m_lastClickTimestamp = m_startupTimestamp;

    // Window: frameless, always-on-top, no taskbar entry
    setWindowFlags(Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint
                   | Qt::Tool);

    // Transparent background (OS-level compositing)
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    // Keep alive when "closed" so the tray can re-show us
    setAttribute(Qt::WA_DeleteOnClose, false);
    // Receive mouseMoveEvent even without a button pressed (needed for mask)
    setMouseTracking(true);

    setFixedSize(kBasePetSize, kBasePetSize);
    setupDpiTracking();

    // ── Animation engine ──────────────────────────────────────────────────
    m_engine = new AnimationEngine(this);
    connect(m_engine, &AnimationEngine::frameChanged,
            this,     &PetWindow::onFrameChanged);
        refreshDpr();
    m_engine->preload();

    // ── Random idle timer ─────────────────────────────────────────────────
    m_randomTimer = new QTimer(this);
    m_randomTimer->setSingleShot(true);
    connect(m_randomTimer, &QTimer::timeout,
            this,          &PetWindow::onRandomTimer);

        m_sleepCheckTimer = new QTimer(this);
        m_sleepCheckTimer->setInterval(30000);
        connect(m_sleepCheckTimer, &QTimer::timeout,
            this,              &PetWindow::onSleepCheckTimeout);
        m_sleepCheckTimer->start();

    // ── Fall-linger timer (cry after 3 s resting post-fall) ──────────────────
    m_fallLingerTimer = new QTimer(this);
    m_fallLingerTimer->setSingleShot(true);
    connect(m_fallLingerTimer, &QTimer::timeout,
            this,              &PetWindow::onFallLingerTimeout);

    // ── Shake-check timer (samples position every 200 ms during drag) ────────
    m_shakeCheckTimer = new QTimer(this);
    m_shakeCheckTimer->setInterval(200);
    connect(m_shakeCheckTimer, &QTimer::timeout,
            this,              &PetWindow::onShakeCheckTimeout);
    setupTray();
    restorePosition();
    m_state = State::Animating;
    m_engine->play(QStringLiteral("hello"), [this] { goIdle(); });

    QTimer::singleShot(350, this, [this] { ensureVisibleNow(); });
    QTimer::singleShot(1200, this, [this] { ensureVisibleNow(); });
}

PetWindow::~PetWindow()
{
    savePosition();
}

void PetWindow::setupDpiTracking()
{
    winId();

    if (windowHandle())
    {
        connect(windowHandle(), &QWindow::screenChanged,
                this, [this](QScreen *screen) {
                    bindScreenSignals(screen);
                    refreshDpr();
                });
        bindScreenSignals(windowHandle()->screen());
    }
    else
    {
        bindScreenSignals(QGuiApplication::primaryScreen());
    }
}

void PetWindow::bindScreenSignals(QScreen *screen)
{
    if (m_boundScreen == screen)
        return;

    if (m_boundScreen)
        disconnect(m_boundScreen, nullptr, this, nullptr);

    m_boundScreen = screen;
    if (!m_boundScreen)
        return;

    connect(m_boundScreen, &QScreen::logicalDotsPerInchChanged,
            this, [this](qreal) { refreshDpr(); });
    connect(m_boundScreen, &QScreen::physicalDotsPerInchChanged,
            this, [this](qreal) { refreshDpr(); });
    connect(m_boundScreen, &QScreen::geometryChanged,
            this, [this](const QRect &) { refreshDpr(); });
}

void PetWindow::refreshDpr(bool force)
{
    QScreen *screen = nullptr;
    if (windowHandle())
        screen = windowHandle()->screen();
    if (!screen)
        screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    bindScreenSignals(screen);

    const qreal dpr = std::max(1.0, screen->devicePixelRatio());
    const qreal effectiveScale = std::max(1.0, dpr * m_userScale);
    if (!force
        && std::abs(dpr - m_lastDpr) < 0.01
        && std::abs(effectiveScale - m_lastEffectiveScale) < 0.01)
        return;

    m_lastDpr = dpr;
    m_lastEffectiveScale = effectiveScale;
    m_anchorReady = false;
    m_anchorOpaqueRect = {};
    m_frameDrawOffset = {};

    if (m_engine)
        m_engine->setDevicePixelRatio(effectiveScale);

    updateMask();
    update();
}

// ── System tray ─────────────────────────────────────────────────────────────

void PetWindow::setupTray()
{
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(QIcon(QStringLiteral(":/sprites/def01.png")));
    m_tray->setToolTip(QStringLiteral("MikuPet"));

    auto *menu      = new QMenu(this);
    auto *actToggle = menu->addAction(tr("显示 / 隐藏  Show/Hide"));
    menu->addSeparator();
    auto *sizeMenu = menu->addMenu(tr("大小  Size"));
    m_sizeActionGroup = new QActionGroup(sizeMenu);
    m_sizeActionGroup->setExclusive(true);

    auto addScaleAction = [this, sizeMenu](const QString &label, qreal scale) {
        QAction *action = sizeMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(std::abs(m_userScale - scale) < 0.01);
        m_sizeActionGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, scale] {
            applyUserScale(scale, true);
        });
    };

    addScaleAction(tr("50%"), 0.5);
    addScaleAction(tr("75%"), 0.75);
    addScaleAction(tr("100%"), 1.0);
    addScaleAction(tr("125%"), 1.25);
    addScaleAction(tr("150%"), 1.5);
    addScaleAction(tr("200%"), 2.0);

    sizeMenu->addSeparator();
    auto *actScaleReset = sizeMenu->addAction(tr("恢复默认  Reset"));
    connect(actScaleReset, &QAction::triggered, this, [this] { applyUserScale(1.0, true); });

    auto *actFeed  = menu->addAction(tr("喂食  Feed"));
    auto *actDance = menu->addAction(tr("跳舞  Dance"));
    auto *actSleep = menu->addAction(tr("睡觉  Sleep"));
    menu->addSeparator();
    auto *actQuit  = menu->addAction(tr("退出  Quit"));

    connect(actToggle, &QAction::triggered, this, &PetWindow::toggleVisibility);
    connect(actFeed,   &QAction::triggered, this, &PetWindow::doFeed);
    connect(actDance,  &QAction::triggered, this, &PetWindow::doDance);
    connect(actSleep,  &QAction::triggered, this, &PetWindow::doSleep);
    connect(actQuit,   &QAction::triggered, this, &PetWindow::doQuit);

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated,
            this,   &PetWindow::onTrayActivated);
    m_tray->show();
}

// ── Paint ────────────────────────────────────────────────────────────────────

void PetWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!m_currentFrame.isNull())
    {
        p.save();
        p.scale(m_userScale, m_userScale);
        p.drawPixmap(m_frameDrawOffset, m_currentFrame);
        p.restore();
    }
}

// ── Frame update + mask ──────────────────────────────────────────────────────

void PetWindow::onFrameChanged(const QPixmap &pixmap)
{
    m_currentFrame = pixmap;

    m_frameDrawOffset = {};
    if (!m_currentFrame.isNull())
    {
        const QImage frameImage = m_currentFrame.toImage();
        const QRect bounds = opaqueBounds(frameImage);
        if (!bounds.isNull())
        {
            if (!m_anchorReady)
            {
                m_anchorOpaqueRect = bounds;
                m_anchorReady = true;
            }
            else
            {
                const int anchorCenterX = m_anchorOpaqueRect.left() + (m_anchorOpaqueRect.width() - 1) / 2;
                const int frameCenterX  = bounds.left() + (bounds.width() - 1) / 2;
                const int dx = anchorCenterX - frameCenterX;
                const int dy = m_anchorOpaqueRect.bottom() - bounds.bottom();

                m_frameDrawOffset = {
                    std::clamp(dx, -80, 80),
                    std::clamp(dy, -80, 80)
                };
            }
        }
    }

    updateMask();
    update();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_startupTimestamp < 3000 && !isVisible())
        ensureVisibleNow();
}

void PetWindow::updateMask()
{
    if (m_currentFrame.isNull())
    {
        m_alphaCanvas = QImage();
#ifndef Q_OS_WIN
        clearMask();
#endif
        return;
    }

    QImage canvas(size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.scale(m_userScale, m_userScale);
        painter.drawPixmap(m_frameDrawOffset, m_currentFrame);
    }

    m_alphaCanvas = canvas;

#ifndef Q_OS_WIN
    const QBitmap mask = QBitmap::fromImage(canvas.createAlphaMask());
    if (mask.isNull())
        clearMask();
    else
        setMask(mask);
#endif
}

bool PetWindow::isOpaqueAt(const QPoint &localPos) const
{
    if (m_alphaCanvas.isNull() || !rect().contains(localPos))
        return false;

    return qAlpha(m_alphaCanvas.pixel(localPos)) > 3;
}

#ifdef Q_OS_WIN
bool PetWindow::nativeEvent(const QByteArray &, void *message, qintptr *result)
{
    MSG *msg = static_cast<MSG *>(message);
    if (!msg)
        return false;

    if (msg->message == WM_NCHITTEST)
    {
        if (m_alphaCanvas.isNull())
        {
            *result = HTCLIENT;
            return true;
        }

        const QPoint localPos = mapFromGlobal(QCursor::pos());
        if (!rect().contains(localPos))
            return false;

        if (!isOpaqueAt(localPos))
        {
            *result = HTTRANSPARENT;
            return true;
        }

        *result = HTCLIENT;
        return true;
    }

    return QWidget::nativeEvent("windows_generic_MSG", message, result);
}
#endif

// ── Mouse interaction ────────────────────────────────────────────────────────

void PetWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
    {
        recordClickActivity();
        if (m_sleepLooping)
            stopSleepLoop(false);
    }

    if (event->button() == Qt::LeftButton)
    {
        if (isAngryLocked())
        {
            event->accept();
            return;
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();

        // ── Rapid-click detection (angry) ─────────────────────────────────
        m_clickTimestamps.append(now);
        // Drop clicks older than the 1-second window
        while (!m_clickTimestamps.isEmpty()
               && now - m_clickTimestamps.first() > kAngryWindowMs)
        {
            m_clickTimestamps.removeFirst();
        }

        if (m_clickTimestamps.size() >= kAngryClickCount)
        {
            m_clickTimestamps.clear();
            triggerAngry();
            event->accept();
            return;
        }

        // ── Normal press: begin drag / click tracking ─────────────────────
        m_dragOffset       = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_pressGlobalPos   = event->globalPosition().toPoint();
        m_pressTimestamp   = now;
        m_movedDuringPress = false;
        m_pendingDizzy     = false;
        m_dragStartTimestamp = now;
        m_shakeAccum        = 0.0;
        m_shakeReverseCount = 0;
        m_shakeLastDelta    = {};
        m_lastShakePos      = pos();

        cancelRandom();
        m_fallLingerTimer->stop();
        m_state = State::Dragging;
        m_engine->play(QStringLiteral("lift"));
        m_shakeCheckTimer->start();
        event->accept();
    }
}

void PetWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        const QPoint delta = event->globalPosition().toPoint() - m_pressGlobalPos;
        if (delta.manhattanLength() > 4)
        {
            m_movedDuringPress = true;
            move(event->globalPosition().toPoint() - m_dragOffset);
        }
        event->accept();
    }
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_state == State::Dragging)
    {
        m_shakeCheckTimer->stop();
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_pressTimestamp;

        if (!m_movedDuringPress && elapsed < 300)
        {
            // Short tap with no movement → random interaction animation
            const auto &pool = clickRandomPool();
            const QString name = pool.at(static_cast<int>(
                QRandomGenerator::global()->bounded(static_cast<uint>(pool.size()))));
            m_state = State::Animating;
            m_engine->play(name, [this] { goIdle(); });
        }
        else if (m_pendingDizzy)
        {
            // Shake detected during drag → dizzy instead of fall
            m_pendingDizzy = false;
            m_state = State::Animating;
            m_engine->play(QStringLiteral("dizzy"), [this] { goIdle(); });
        }
        else
        {
            // Normal drag ended → fall, then start fall-linger timer
            m_state = State::Animating;
            m_engine->play(QStringLiteral("fall"), [this] {
                // After fall completes: go idle but arm a 3-second cry timer
                goIdle();
                m_fallLingerTimer->start(3000);
            });
        }
        event->accept();
    }
}

void PetWindow::contextMenuEvent(QContextMenuEvent *event)
{
    recordClickActivity();
    if (m_sleepLooping)
        stopSleepLoop(false);

    if (m_tray && m_tray->contextMenu())
        m_tray->contextMenu()->popup(event->globalPos());
    event->accept();
}

// ── State transitions ────────────────────────────────────────────────────────

void PetWindow::goIdle()
{
    if (m_sleepLooping)
        return;

    m_fallLingerTimer->stop();
    m_state = State::Idle;
    m_engine->play(QStringLiteral("def"));
    scheduleRandom();
}

void PetWindow::scheduleRandom()
{
    cancelRandom();
    // Random delay: 30 000 – 120 000 ms
    const int delay = 30000 + static_cast<int>(
        QRandomGenerator::global()->bounded(90001u));
    m_randomTimer->start(delay);
}

void PetWindow::cancelRandom()
{
    m_randomTimer->stop();
}

void PetWindow::onRandomTimer()
{
    if (isAngryLocked() || m_sleepLooping)
    {
        scheduleRandom();
        return;
    }

    if (m_state != State::Idle)
    {
        scheduleRandom(); // Try again later
        return;
    }
    const auto &pool = randomPool();
    const QString name =
        pool.at(static_cast<int>(
            QRandomGenerator::global()->bounded(static_cast<uint>(pool.size()))));

    m_state = State::Animating;
    m_engine->play(name, [this] { goIdle(); });
}

// ── New trigger helpers ─────────────────────────────────────────────────────────────

void PetWindow::triggerAngry()
{
    if (m_sleepLooping)
        stopSleepLoop(false);

    if (isAngryLocked())
        return;

    cancelRandom();
    m_fallLingerTimer->stop();
    m_state = State::Animating;
    ++m_angryTriggerCount;

    if (m_angryTriggerCount >= kCryAfterAngryCount)
    {
        // Too many angry events in this session → cry instead
        m_angryTriggerCount = 0;
        triggerCry();
        return;
    }

    m_angryLocked = true;
    m_engine->play(QStringLiteral("angry"), [this] {
        m_angryLocked = false;
        goIdle();
    });
}

void PetWindow::triggerCry()
{
    if (m_sleepLooping)
        stopSleepLoop(false);

    if (isAngryLocked())
        return;

    cancelRandom();
    m_fallLingerTimer->stop();
    m_state = State::Animating;
    // Reset angry counter so the cycle can restart
    m_angryTriggerCount = 0;
    m_engine->play(QStringLiteral("cry"), [this] { goIdle(); });
}

void PetWindow::onFallLingerTimeout()
{
    if (isAngryLocked())
        return;

    // Only fire if the pet is still quietly idle after a fall
    if (m_state != State::Idle)
        return;
    triggerCry();
}

void PetWindow::onShakeCheckTimeout()
{
    const QPoint currentPos = pos();
    const QPoint delta      = currentPos - m_lastShakePos;
    m_lastShakePos = currentPos;

    if (!m_shakeLastDelta.isNull())
    {
        // Count a reversal if horizontal and/or vertical direction flipped
        const bool xFlip = (delta.x() != 0 && m_shakeLastDelta.x() != 0
                            && ((delta.x() > 0) != (m_shakeLastDelta.x() > 0)));
        const bool yFlip = (delta.y() != 0 && m_shakeLastDelta.y() != 0
                            && ((delta.y() > 0) != (m_shakeLastDelta.y() > 0)));
        if (xFlip || yFlip)
            ++m_shakeReverseCount;
    }

    m_shakeAccum    += std::hypot(delta.x(), delta.y());
    m_shakeLastDelta = delta;

    // Dizzy condition: dragging for > 3 s AND significant back-and-forth motion
    const qint64 dragDuration =
        QDateTime::currentMSecsSinceEpoch() - m_dragStartTimestamp;
    // At least 3 s of dragging, >=6 direction reversals, >=200 px total travel
    if (dragDuration >= 3000 && m_shakeReverseCount >= 6 && m_shakeAccum >= 200.0)
    {
        m_pendingDizzy = true;
        // Stop further sampling – condition already satisfied
        m_shakeCheckTimer->stop();
    }
}

// ── Tray callbacks ───────────────────────────────────────────────────────────

void PetWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    recordClickActivity();
    if (m_sleepLooping)
        stopSleepLoop(true);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_startupTimestamp < 1500)
        return;

    // Left-click on tray icon: toggle visibility
    if (reason == QSystemTrayIcon::Trigger)
        toggleVisibility();
}

void PetWindow::ensureVisibleNow()
{
    move(clampToVisibleArea(pos(), size()));
    if (!isVisible())
        show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

void PetWindow::toggleVisibility()
{
    if (isVisible())
        hide();
    else
    {
        move(clampToVisibleArea(pos(), size()));
        show();
        raise();
        activateWindow();
    }
}

void PetWindow::doFeed()
{
    if (m_state == State::Dragging || isAngryLocked()) return;
    if (m_sleepLooping)
        stopSleepLoop(false);
    cancelRandom();
    m_state = State::Animating;
    m_engine->play(QStringLiteral("eatcake"), [this] { goIdle(); });
}

void PetWindow::doDance()
{
    if (m_state == State::Dragging || isAngryLocked()) return;
    if (m_sleepLooping)
        stopSleepLoop(false);
    cancelRandom();
    m_state = State::Animating;
    m_engine->play(QStringLiteral("rolling"), [this] { goIdle(); });
}

void PetWindow::doSleep()
{
    if (m_state == State::Dragging || isAngryLocked()) return;
    startSleepLoop();
}

void PetWindow::setScale50()  { applyUserScale(0.5,  true); }
void PetWindow::setScale75()  { applyUserScale(0.75, true); }
void PetWindow::setScale100() { applyUserScale(1.0,  true); }
void PetWindow::setScale125() { applyUserScale(1.25, true); }
void PetWindow::setScale150() { applyUserScale(1.5,  true); }
void PetWindow::setScale200() { applyUserScale(2.0,  true); }

void PetWindow::applyUserScale(qreal scale, bool persist)
{
    const qreal clamped = std::clamp(scale, kMinUserScale, kMaxUserScale);
    if (std::abs(clamped - m_userScale) < 0.01)
        return;

    const QSize oldSize = size();
    const QPoint oldPos = pos();
    const QPoint oldAnchor(oldPos.x() + oldSize.width() / 2,
                           oldPos.y() + oldSize.height());

    m_userScale = clamped;

    const int edge = std::max(1, static_cast<int>(std::lround(kBasePetSize * m_userScale)));
    setFixedSize(edge, edge);

    if (m_sizeActionGroup)
    {
        for (QAction *action : m_sizeActionGroup->actions())
        {
            const QString text = action->text();
            if (!text.endsWith(QLatin1Char('%')))
                continue;

            bool ok = false;
            const qreal pct = text.left(text.size() - 1).toDouble(&ok);
            if (!ok)
                continue;

            action->setChecked(std::abs((pct / 100.0) - m_userScale) < 0.01);
        }
    }

    const QPoint nextPos(oldAnchor.x() - width() / 2,
                         oldAnchor.y() - height());
    move(clampToVisibleArea(nextPos, size()));

    if (m_sizeActionGroup)
    {
        for (QAction *action : m_sizeActionGroup->actions())
        {
            const QString text = action->text();
            if (!text.endsWith(QLatin1Char('%')))
                continue;

            bool ok = false;
            const qreal pct = text.left(text.size() - 1).toDouble(&ok);
            if (!ok)
                continue;

            const qreal actionScale = pct / 100.0;
            action->setChecked(std::abs(actionScale - m_userScale) < 0.01);
        }
    }

    refreshDpr(true);

    if (persist)
    {
        QSettings s(QStringLiteral("MikuPet"), QStringLiteral("MikuPet"));
        s.setValue(QStringLiteral("petScale"), m_userScale);
    }
}

void PetWindow::doQuit()
{
    if (m_quitRequested)
        return;

    m_quitRequested = true;
    cancelRandom();
    m_fallLingerTimer->stop();
    m_shakeCheckTimer->stop();
    if (m_sleepCheckTimer)
        m_sleepCheckTimer->stop();
    m_angryLocked = false;
    m_sleepLooping = false;

    if (m_tray)
        m_tray->hide();

    ensureVisibleNow();
    m_state = State::Animating;
    m_engine->play(QStringLiteral("bye"), [this] {
        savePosition();
        qApp->quit();
    });
}

void PetWindow::recordClickActivity()
{
    m_lastClickTimestamp = QDateTime::currentMSecsSinceEpoch();
}

bool PetWindow::isNightSleepWindow() const
{
    const int hour = QTime::currentTime().hour();
    return hour >= kNightSleepStartHour || hour < kNightSleepEndHour;
}

void PetWindow::playSleepLoopOnce()
{
    if (!m_sleepLooping || m_quitRequested)
        return;

    cancelRandom();
    m_fallLingerTimer->stop();
    m_state = State::Animating;
    m_engine->play(QStringLiteral("sleep"), [this] {
        if (!m_sleepLooping || m_quitRequested)
            return;
        playSleepLoopOnce();
    });
}

void PetWindow::startSleepLoop()
{
    if (m_sleepLooping || m_quitRequested)
        return;

    m_sleepLooping = true;
    playSleepLoopOnce();
}

void PetWindow::stopSleepLoop(bool resumeIdle)
{
    if (!m_sleepLooping)
        return;

    m_sleepLooping = false;
    if (resumeIdle && !m_quitRequested)
        goIdle();
}

void PetWindow::onSleepCheckTimeout()
{
    if (m_quitRequested || m_sleepLooping || isAngryLocked())
        return;

    if (!isNightSleepWindow())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastClickTimestamp < kSleepInactivityMs)
        return;

    if (m_state == State::Dragging)
        return;

    startSleepLoop();
}

// ── Window lifecycle ─────────────────────────────────────────────────────────

void PetWindow::closeEvent(QCloseEvent *event)
{
    savePosition();

    if (m_quitRequested)
    {
        event->accept();
        return;
    }

    // If the tray is still visible, hide instead of destroying
    if (m_tray && m_tray->isVisible())
    {
        hide();
        event->ignore();
    }
    else
    {
        event->accept();
    }
}

// ── Settings persistence ─────────────────────────────────────────────────────

void PetWindow::savePosition()
{
    QSettings s(QStringLiteral("MikuPet"), QStringLiteral("MikuPet"));
    s.setValue(QStringLiteral("pos"), pos());
    s.setValue(QStringLiteral("petScale"), m_userScale);
}

void PetWindow::restorePosition()
{
    QSettings s(QStringLiteral("MikuPet"), QStringLiteral("MikuPet"));

    const qreal savedScale = s.value(QStringLiteral("petScale"), 1.0).toDouble();
    m_userScale = std::clamp(savedScale, kMinUserScale, kMaxUserScale);

    const int edge = std::max(1, static_cast<int>(std::lround(kBasePetSize * m_userScale)));
    setFixedSize(edge, edge);

    // Default: bottom-right corner of the primary screen
    QPoint defaultPos(0, 0);
    if (const auto *screen = QApplication::primaryScreen())
    {
        const QRect avail = screen->availableGeometry();
        defaultPos = {avail.right() - width() - 24, avail.bottom() - height() - 64};
    }

    const QPoint savedPos = s.value(QStringLiteral("pos"), defaultPos).toPoint();
    move(clampToVisibleArea(savedPos, size()));

    refreshDpr(true);
}
