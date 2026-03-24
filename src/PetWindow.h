#pragma once

#include <QPoint>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidget>

class AnimationEngine;

/**
 * PetWindow
 *
 * The 240×240 transparent, frameless, always-on-top pet window.
 *
 * Key features:
 *  - WA_TranslucentBackground + setMask(QBitmap) for pixel-accurate
 *    click-through on transparent areas (cross-platform, no platform #ifdefs)
 *  - Left-click  → "hello" animation
 *  - 4× left-click within 1 s → "angry" animation
 *  - Left-drag   → window dragging with lift/fall animations
 *  - Shaking during drag (> 3 s) → "dizzy" after drop
 *  - Right-click → shy / question (random)
 *  - System tray with feed / dance / sleep / toggle / quit
 *  - Random idle animations every 30–120 s
 *  - "cry" triggered after 3 s idle following a fall, or after 5 angry events
 *  - Window position persisted via QSettings
 */
class PetWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr);
    ~PetWindow() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onFrameChanged(const QPixmap &pixmap);
    void onRandomTimer();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void doFeed();
    void doDance();
    void doSleep();
    void toggleVisibility();
    void onFallLingerTimeout();
    void onShakeCheckTimeout();

private:
    enum class State { Idle, Animating, Dragging };

    void goIdle();
    void scheduleRandom();
    void cancelRandom();
    void updateMask();
    void setupTray();
    void savePosition();
    void restorePosition();
    void triggerAngry();
    void triggerCry();

    // ── Sub-objects ────────────────────────────────────────────────────────
    AnimationEngine   *m_engine{nullptr};
    QSystemTrayIcon   *m_tray{nullptr};
    QTimer            *m_randomTimer{nullptr};
    /// Fires 3 s after a fall animation completes to trigger cry
    QTimer            *m_fallLingerTimer{nullptr};
    /// Samples cursor position every 200 ms while dragging to detect shaking
    QTimer            *m_shakeCheckTimer{nullptr};

    // ── Animation state ────────────────────────────────────────────────────
    State   m_state{State::Idle};
    QPixmap m_currentFrame;

    // ── Drag tracking ──────────────────────────────────────────────────────
    QPoint  m_dragOffset;           ///< cursor offset from window top-left
    QPoint  m_pressGlobalPos;       ///< global pos at mousePress
    qint64  m_pressTimestamp{0};
    bool    m_movedDuringPress{false};
    /// Set when dizzy should replace fall (shake during drag detected)
    bool    m_pendingDizzy{false};

    // ── Shake detection ─────────────────────────────────────────────────────
    QPoint  m_lastShakePos;         ///< window pos at previous shake sample
    qint64  m_dragStartTimestamp{0};///< when the current drag began
    double  m_shakeAccum{0.0};      ///< accumulated shake distance (px)
    int     m_shakeReverseCount{0}; ///< direction reversals detected
    QPoint  m_shakeLastDelta;       ///< delta at previous sample

    // ── Rapid-click (angry) tracking ────────────────────────────────────────
    QVector<qint64> m_clickTimestamps; ///< timestamps of recent left-clicks
    static constexpr int    kAngryClickCount = 4;   ///< clicks required
    static constexpr qint64 kAngryWindowMs  = 1000; ///< within this window

    // ── Angry-count (cry) tracking ───────────────────────────────────────────
    int m_angryTriggerCount{0}; ///< how many times angry has been triggered
    static constexpr int kCryAfterAngryCount = 5;

    // ── Random pool ─────────────────────────────────────────────────────────
    static const QStringList &randomPool();
};
