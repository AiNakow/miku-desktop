#include "AnimationEngine.h"

// ── Static animation definitions ──────────────────────────────────────────────
// Keep in sync with drawable-hdpi/ frame counts.
static const QMap<QString, AnimDef> s_defs = {
    {QStringLiteral("angry"),          {5,  8,  false}},
    {QStringLiteral("bye"),            {14, 8,  false}},
    {QStringLiteral("cry"),            {12, 8,  false}},
    {QStringLiteral("def"),            {18, 8,  true }},
    {QStringLiteral("dizzy"),          {19, 8,  false}},
    {QStringLiteral("drink"),          {33, 10, false}},
    {QStringLiteral("eatcake"),        {15, 8,  false}},
    {QStringLiteral("excited"),        {8,  10, false}},
    {QStringLiteral("fall"),           {9,  12, false}},
    {QStringLiteral("hairflip"),       {5,  8,  false}},
    {QStringLiteral("heart"),          {10, 8,  false}},
    {QStringLiteral("hello"),          {10, 8,  false}},
    {QStringLiteral("jump"),           {8,  10, false}},
    {QStringLiteral("lift"),           {12, 8,  true }},
    {QStringLiteral("listenmusic"),    {4,  6,  false}},
    {QStringLiteral("notlistenmusic"), {23, 8,  false}},
    {QStringLiteral("punching"),       {10, 10, false}},
    {QStringLiteral("question"),       {7,  8,  false}},
    {QStringLiteral("rolling"),        {15, 8,  false}},
    {QStringLiteral("rotate"),         {10, 8,  false}},
    {QStringLiteral("shy"),            {16, 8,  false}},
    {QStringLiteral("sleep"),          {14, 6,  false}},
    {QStringLiteral("watch"),          {8,  8,  false}},
};

const QMap<QString, AnimDef> &AnimationEngine::defs()
{
    return s_defs;
}

// ── Constructor ───────────────────────────────────────────────────────────────

AnimationEngine::AnimationEngine(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &AnimationEngine::advance);
}

// ── Preload ───────────────────────────────────────────────────────────────────

void AnimationEngine::preload()
{
    for (auto it = s_defs.constBegin(); it != s_defs.constEnd(); ++it)
        loadClip(it.key(), it.value());

    m_loaded = true;
}

void AnimationEngine::loadClip(const QString &name, const AnimDef &def)
{
    QVector<QPixmap> frames;
    frames.reserve(def.frames);

    for (int f = 1; f <= def.frames; ++f)
    {
        const QString path =
            QStringLiteral(":/sprites/%1%2.png")
                .arg(name)
                .arg(f, 2, 10, QChar('0'));

        QPixmap px(path);
        if (!px.isNull())
            frames.append(std::move(px));
    }

    m_sprites.insert(name, std::move(frames));
}

// ── Playback control ──────────────────────────────────────────────────────────

void AnimationEngine::play(const QString &name, std::function<void()> onFinished)
{
    if (!s_defs.contains(name))
        return;

    m_timer.stop();
    m_currentAnim  = name;
    m_currentFrame = 0;
    m_onFinished   = std::move(onFinished);

    const AnimDef &def = s_defs.value(name);
    m_timer.setInterval(1000 / def.fps);
    m_timer.start();

    // Emit the very first frame without waiting for the first tick
    const auto &frames = m_sprites.value(name);
    if (!frames.isEmpty())
        emit frameChanged(frames[0]);
}

void AnimationEngine::stop()
{
    m_timer.stop();
    m_onFinished = nullptr;
}

// ── Frame advance (called by QTimer) ─────────────────────────────────────────

void AnimationEngine::advance()
{
    const auto &frames = m_sprites.value(m_currentAnim);
    if (frames.isEmpty())
        return;

    ++m_currentFrame;

    const AnimDef &def = s_defs.value(m_currentAnim);

    if (m_currentFrame >= frames.size())
    {
        if (def.loop)
        {
            m_currentFrame = 0;
        }
        else
        {
            m_timer.stop();
            m_currentFrame = frames.size() - 1;
            emit frameChanged(frames[m_currentFrame]);
            emit animationFinished();

            if (m_onFinished)
            {
                auto cb    = std::move(m_onFinished);
                m_onFinished = nullptr;
                cb();
            }
            return;
        }
    }

    emit frameChanged(frames[m_currentFrame]);
}

// ── Query ─────────────────────────────────────────────────────────────────────

QPixmap AnimationEngine::currentPixmap() const
{
    const auto &frames = m_sprites.value(m_currentAnim);
    if (m_currentFrame < frames.size())
        return frames[m_currentFrame];
    return {};
}
