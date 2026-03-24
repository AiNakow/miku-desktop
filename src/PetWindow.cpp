#include "PetWindow.h"
#include "AnimationEngine.h"

#include <QApplication>
#include <QBitmap>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QScreen>
#include <QSettings>
#include <cmath>

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

const QStringList &PetWindow::randomPool()
{
    return s_randomPool;
}

// ── Constructor ─────────────────────────────────────────────────────────────

PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
{
    // Window: frameless, always-on-top, no taskbar entry
    setWindowFlags(Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint
                   | Qt::Tool);

    // Transparent background (OS-level compositing)
    setAttribute(Qt::WA_TranslucentBackground);
    // Keep alive when "closed" so the tray can re-show us
    setAttribute(Qt::WA_DeleteOnClose, false);
    // Receive mouseMoveEvent even without a button pressed (needed for mask)
    setMouseTracking(true);

    setFixedSize(240, 240);

    // ── Animation engine ──────────────────────────────────────────────────
    m_engine = new AnimationEngine(this);
    connect(m_engine, &AnimationEngine::frameChanged,
            this,     &PetWindow::onFrameChanged);
    m_engine->preload();

    // ── Random idle timer ─────────────────────────────────────────────────
    m_randomTimer = new QTimer(this);
    m_randomTimer->setSingleShot(true);
    connect(m_randomTimer, &QTimer::timeout,
            this,          &PetWindow::onRandomTimer);
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
    goIdle();
}

PetWindow::~PetWindow()
{
    savePosition();
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
    auto *actFeed  = menu->addAction(tr("喂食  Feed"));
    auto *actDance = menu->addAction(tr("跳舞  Dance"));
    auto *actSleep = menu->addAction(tr("睡觉  Sleep"));
    menu->addSeparator();
    auto *actQuit  = menu->addAction(tr("退出  Quit"));

    connect(actToggle, &QAction::triggered, this, &PetWindow::toggleVisibility);
    connect(actFeed,   &QAction::triggered, this, &PetWindow::doFeed);
    connect(actDance,  &QAction::triggered, this, &PetWindow::doDance);
    connect(actSleep,  &QAction::triggered, this, &PetWindow::doSleep);
    connect(actQuit,   &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated,
            this,   &PetWindow::onTrayActivated);
    m_tray->show();
}

// ── Paint ────────────────────────────────────────────────────────────────────

void PetWindow::paintEvent(QPaintEvent *)
{
    if (m_currentFrame.isNull())
        return;

    QPainter p(this);
    // CompositionMode_Source: fully replace destination incl. alpha channel
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawPixmap(rect(), m_currentFrame);
}

// ── Frame update + mask ──────────────────────────────────────────────────────

void PetWindow::onFrameChanged(const QPixmap &pixmap)
{
    m_currentFrame = pixmap;
    updateMask();
    update();
}

void PetWindow::updateMask()
{
    if (m_currentFrame.isNull())
        return;

    // MaskOutColor: bit = 1 where pixel != Qt::transparent  →  mouse-clickable
    // bit = 0  →  mouse events fall through to windows below
    const QBitmap mask =
        m_currentFrame.createMaskFromColor(Qt::transparent, Qt::MaskOutColor);
    setMask(mask);
}

// ── Mouse interaction ────────────────────────────────────────────────────────

void PetWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
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
            // Short tap with no movement → hello
            m_state = State::Animating;
            m_engine->play(QStringLiteral("hello"), [this] { goIdle(); });
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
    if (m_state == State::Idle)
    {
        m_state = State::Animating;
        cancelRandom();
        const bool useShy = (QRandomGenerator::global()->bounded(2) == 0);
        m_engine->play(useShy ? QStringLiteral("shy") : QStringLiteral("question"),
                       [this] { goIdle(); });
    }
    event->accept();
}

// ── State transitions ────────────────────────────────────────────────────────

void PetWindow::goIdle()
{
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

    m_engine->play(QStringLiteral("angry"), [this] { goIdle(); });
}

void PetWindow::triggerCry()
{
    cancelRandom();
    m_fallLingerTimer->stop();
    m_state = State::Animating;
    // Reset angry counter so the cycle can restart
    m_angryTriggerCount = 0;
    m_engine->play(QStringLiteral("cry"), [this] { goIdle(); });
}

void PetWindow::onFallLingerTimeout()
{
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
    // Left-click on tray icon: toggle visibility
    if (reason == QSystemTrayIcon::Trigger)
        toggleVisibility();
}

void PetWindow::toggleVisibility()
{
    if (isVisible())
        hide();
    else
    {
        show();
        raise();
        activateWindow();
    }
}

void PetWindow::doFeed()
{
    if (m_state == State::Dragging) return;
    cancelRandom();
    m_state = State::Animating;
    m_engine->play(QStringLiteral("eatcake"), [this] { goIdle(); });
}

void PetWindow::doDance()
{
    if (m_state == State::Dragging) return;
    cancelRandom();
    m_state = State::Animating;
    m_engine->play(QStringLiteral("rolling"), [this] { goIdle(); });
}

void PetWindow::doSleep()
{
    if (m_state == State::Dragging) return;
    cancelRandom();
    m_state = State::Animating;
    m_engine->play(QStringLiteral("sleep"), [this] { goIdle(); });
}

// ── Window lifecycle ─────────────────────────────────────────────────────────

void PetWindow::closeEvent(QCloseEvent *event)
{
    savePosition();
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
}

void PetWindow::restorePosition()
{
    QSettings s(QStringLiteral("MikuPet"), QStringLiteral("MikuPet"));

    // Default: bottom-right corner of the primary screen
    QPoint defaultPos(0, 0);
    if (const auto *screen = QApplication::primaryScreen())
    {
        const QRect avail = screen->availableGeometry();
        defaultPos = {avail.right() - 240 - 24, avail.bottom() - 240 - 64};
    }

    move(s.value(QStringLiteral("pos"), defaultPos).toPoint());
}
