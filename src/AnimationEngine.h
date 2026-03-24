#pragma once

#include <QMap>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QVector>
#include <functional>

/// Metadata for one animation clip.
struct AnimDef
{
    int  frames; ///< Total frame count (frames named 01…NN)
    int  fps;    ///< Playback speed
    bool loop;   ///< Whether the clip loops indefinitely
};

/**
 * AnimationEngine
 *
 * Loads all Miku sprite frames from Qt resources at startup, then drives
 * frame-accurate playback via a QTimer. Emits frameChanged() each tick and
 * animationFinished() when a non-looping clip completes.
 */
class AnimationEngine : public QObject
{
    Q_OBJECT
public:
    explicit AnimationEngine(QObject *parent = nullptr);

    /// Preload every animation frame from ":/sprites/<name><NN>.png".
    /// Must be called once before play().
    void preload();

    /// Start playing clip @p name. @p onFinished is called (once) when a
    /// non-looping animation reaches its last frame. Passing nullptr is safe.
    void play(const QString &name, std::function<void()> onFinished = nullptr);

    void stop();

    /// The QPixmap for the frame currently shown on screen.
    [[nodiscard]] QPixmap currentPixmap() const;

    [[nodiscard]] bool isLoaded() const { return m_loaded; }

    /// Shared animation definitions — also used by PetWindow for the random pool.
    static const QMap<QString, AnimDef> &defs();

signals:
    void frameChanged(const QPixmap &pixmap);
    void animationFinished();

private slots:
    void advance();

private:
    void loadClip(const QString &name, const AnimDef &def);

    QTimer                           m_timer;
    QMap<QString, QVector<QPixmap>>  m_sprites;
    QString                          m_currentAnim;
    int                              m_currentFrame{0};
    bool                             m_loaded{false};
    std::function<void()>            m_onFinished;
};
