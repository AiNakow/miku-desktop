#include "AnimationEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <array>
#include <cmath>

static constexpr double kPlaybackSpeedScale = 0.82;

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

static QString resolveFramePath(const QString &name, int frameIndex)
{
    const QString filename = QStringLiteral("%1%2.png")
                                 .arg(name)
                                 .arg(frameIndex, 2, 10, QChar('0'));

    const QString resourcePath = QStringLiteral(":/sprites/%1").arg(filename);
    if (QFileInfo::exists(resourcePath))
        return resourcePath;

    const QStringList fallbackDirs = {
        QDir::currentPath() + QStringLiteral("/drawable-hdpi"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/drawable-hdpi"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../drawable-hdpi"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../drawable-hdpi")
    };

    for (const QString &dir : fallbackDirs)
    {
        const QString candidate = QDir(dir).filePath(filename);
        if (QFileInfo::exists(candidate))
            return candidate;
    }

    return resourcePath;
}

static QPixmap makePlaceholderFrame(const QString &name)
{
    QPixmap px(240, 240);
    px.fill(QColor(255, 0, 255, 180));
    QPainter painter(&px);
    painter.setPen(Qt::white);
    painter.drawText(px.rect(), Qt::AlignCenter, QStringLiteral("Missing\n%1").arg(name));
    return px;
}

static bool hasVisiblePixels(const QImage &image)
{
    if (image.isNull())
        return false;
    if (!image.hasAlphaChannel())
        return true;

    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y)
    {
        const QRgb *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x)
        {
            if (qAlpha(row[x]) > 3)
                return true;
        }
    }
    return false;
}

static QImage recoverAlphaFromRgb(const QImage &image)
{
    QImage out = image.convertToFormat(QImage::Format_ARGB32);

    const std::array<QPoint, 4> samplePoints = {
        QPoint(0, 0),
        QPoint(std::max(0, out.width() - 1), 0),
        QPoint(0, std::max(0, out.height() - 1)),
        QPoint(std::max(0, out.width() - 1), std::max(0, out.height() - 1))
    };

    int bgR = 0;
    int bgG = 0;
    int bgB = 0;
    for (const QPoint &pt : samplePoints)
    {
        const QRgb c = out.pixel(pt);
        bgR += qRed(c);
        bgG += qGreen(c);
        bgB += qBlue(c);
    }
    bgR /= static_cast<int>(samplePoints.size());
    bgG /= static_cast<int>(samplePoints.size());
    bgB /= static_cast<int>(samplePoints.size());

    auto isBackgroundLike = [bgR, bgG, bgB](int r, int g, int b) {
        return std::abs(r - bgR) <= 12
            && std::abs(g - bgG) <= 12
            && std::abs(b - bgB) <= 12;
    };

    for (int y = 0; y < out.height(); ++y)
    {
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
        {
            const int r = qRed(row[x]);
            const int g = qGreen(row[x]);
            const int b = qBlue(row[x]);
            if (qAlpha(row[x]) != 0)
                continue;

            if (isBackgroundLike(r, g, b))
                row[x] = qRgba(r, g, b, 0);
            else
                row[x] = qRgba(r, g, b, 255);
        }
    }
    return out;
}

static QPixmap loadFramePixmap(const QString &path)
{
    QImage image(path);
    if (image.isNull())
        return {};

    if (!hasVisiblePixels(image))
        image = recoverAlphaFromRgb(image);

    return QPixmap::fromImage(image);
}

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
        const QString path = resolveFramePath(name, f);

        QPixmap px = loadFramePixmap(path);
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
    const int effectiveFps = std::max(1, static_cast<int>(std::lround(def.fps * kPlaybackSpeedScale)));
    m_timer.setInterval(1000 / effectiveFps);
    m_timer.start();

    // Emit the very first frame without waiting for the first tick
    if (m_sprites.value(name).isEmpty())
        loadClip(name, s_defs.value(name));

    const auto &frames = m_sprites.value(name);
    if (!frames.isEmpty())
    {
        emit frameChanged(frames[0]);
        return;
    }

    m_timer.stop();
    emit frameChanged(makePlaceholderFrame(name));

    if (m_onFinished)
    {
        auto cb = std::move(m_onFinished);
        m_onFinished = nullptr;
        cb();
    }
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
