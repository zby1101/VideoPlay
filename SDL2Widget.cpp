#include "SDL2Widget.h"
#include <QTimer>
#include <QMenu>
#include <cstring>

extern void appendLog(const QString &logTitle,const QString &logStr);

SDL2Widget::SDL2Widget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setFocusPolicy(Qt::StrongFocus);

    QTimer::singleShot(0, this, [this]()
    {
        if (SDL_Init(SDL_INIT_VIDEO) < 0)
        {
            appendLog("SDL2",QString("SDL_Init failed: ") + QString(SDL_GetError()));
            return;
        }
        m_sdlInitialized = true;
        createEmbeddedRenderer();
    });

    QTimer *eventTimer = new QTimer(this);
    connect(eventTimer, &QTimer::timeout, this, &SDL2Widget::processSdlEvents);
    eventTimer->start(16);
}

SDL2Widget::~SDL2Widget()
{
    releaseSdlObjects();
    if (m_sdlInitialized)
        SDL_Quit();
}

void SDL2Widget::presentFrame(const uint8_t* buffer, int imageWidth, int imageHeight)
{
    if (!m_sdlRenderer)
    {
        appendLog("SDL2","Render not initialized!");
        return;
    }

    cacheYuvBuffer(buffer, imageWidth, imageHeight);
    if (!ensureTexture(imageWidth, imageHeight))
        return;

    uploadCachedFrame();
    renderTexture();
}


void SDL2Widget::presentYuvFrame(AVFrame *frame, int imageWidth, int imageHeight)
{
    if (!frame)
        return;

    if (imageWidth <= 0 || imageHeight <= 0)
    {
        av_frame_free(&frame);
        return;
    }

    if (!m_sdlRenderer)
    {
        appendLog("SDL2","Render not initialized!");
        av_frame_free(&frame);
        return;
    }

    if (frame->flags & AV_FRAME_FLAG_CORRUPT)
    {
        appendLog("SDL2","Skipping corrupted frame.");
        av_frame_free(&frame);
        return;
    }

    cacheYuvFrame(frame, imageWidth, imageHeight);
    if (!ensureTexture(imageWidth, imageHeight))
    {
        av_frame_free(&frame);
        return;
    }

    uploadCachedFrame();
    renderTexture();

    av_frame_free(&frame);
}


void SDL2Widget::setFullScreen(bool flag)
{
    if (!m_sdlInitialized || flag == m_isFullScreen)
    {
        scheduleRefresh();
        return;
    }

    releaseSdlObjects();

    // 从全屏回到嵌入模式前先显示 QWidget，否则 SDL_CreateWindowFrom 可能拿不到有效窗口
    if (!flag)
        show();

    const bool ok = flag ? createFullscreenRenderer() : createEmbeddedRenderer();
    if (!ok)
    {
        m_isFullScreen = false;
        show();
        createEmbeddedRenderer();
        return;
    }

    if (flag)
        hide();

    m_isFullScreen = flag;
    uploadCachedFrame();
    scheduleRefresh();
}

void SDL2Widget::clearRenderer()
{
    m_cachedYuvData.clear();
    m_cachedWidth = 0;
    m_cachedHeight = 0;

    if (m_sdlRenderer)
    {
        SDL_RenderClear(m_sdlRenderer);
        SDL_RenderPresent(m_sdlRenderer);
        appendLog("SDL2","Renderer cleared.");
    }
}

void SDL2Widget::refreshRenderer()
{
    syncRenderSize();
    renderTexture();
}

void SDL2Widget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshRenderer();
}

void SDL2Widget::keyPressEvent(QKeyEvent *event)
{
    if(event)
        emit keyPress(event->key());
}

void SDL2Widget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton)
    {
        QMenu menu;
        menu.addAction(tr("全屏"), this, [this] {
            QTimer::singleShot(0, this, [this] { setFullScreen(true); });
        });
        menu.addAction(tr("退出全屏"), this, [this] {
            QTimer::singleShot(0, this, [this] { setFullScreen(false); });
        });
        menu.exec(event->globalPos());
    }
    else
    {
        QWidget::mousePressEvent(event);
    }
}

void SDL2Widget::syncRenderSize()
{
    if (!m_sdlRenderer)
        return;

    SDL_RenderSetLogicalSize(m_sdlRenderer, 0, 0);
    SDL_RenderSetViewport(m_sdlRenderer, nullptr);
}

bool SDL2Widget::createEmbeddedRenderer()
{
    m_sdlWindow = SDL_CreateWindowFrom(reinterpret_cast<void*>(winId()));
    if (!m_sdlWindow)
    {
        appendLog("SDL2",QString("SDL_CreateWindowFrom failed: ") + QString(SDL_GetError()));
        return false;
    }

    m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_sdlRenderer)
    {
        appendLog("SDL2",QString("SDL_CreateRenderer failed: ") + QString(SDL_GetError()));
        releaseSdlObjects();
        return false;
    }

    SDL_SetRenderDrawColor(m_sdlRenderer, 255, 255, 255, 255);
    SDL_RenderClear(m_sdlRenderer);
    SDL_RenderPresent(m_sdlRenderer);
    uploadCachedFrame();
    syncRenderSize();
    return true;
}

bool SDL2Widget::createFullscreenRenderer()
{
    // Qt 原生窗口全屏时 SDL 渲染器容易丢画面，独立 SDL 窗口更稳定
    m_sdlWindow = SDL_CreateWindow("VideoPlay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 1280, 720, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!m_sdlWindow)
    {
        appendLog("SDL2",QString("SDL_CreateWindow fullscreen failed: ") + QString(SDL_GetError()));
        return false;
    }

    m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_sdlRenderer)
    {
        appendLog("SDL2",QString("SDL_CreateRenderer fullscreen failed: ") + QString(SDL_GetError()));
        releaseSdlObjects();
        return false;
    }

    SDL_SetRenderDrawColor(m_sdlRenderer, 255, 255, 255, 255);
    SDL_RenderClear(m_sdlRenderer);
    SDL_RenderPresent(m_sdlRenderer);
    return true;
}

bool SDL2Widget::ensureTexture(int width, int height)
{
    if (!m_sdlRenderer || width <= 0 || height <= 0)
        return false;

    if (m_sdlTexture && m_textureWidth == width && m_textureHeight == height)
        return true;

    if (m_sdlTexture)
    {
        SDL_DestroyTexture(m_sdlTexture);
        m_sdlTexture = nullptr;
    }

    m_sdlTexture = SDL_CreateTexture(m_sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!m_sdlTexture)
    {
        appendLog("SDL2","Failed to create YUV texture.");
        m_textureWidth = 0;
        m_textureHeight = 0;
        return false;
    }

    m_textureWidth = width;
    m_textureHeight = height;
    return true;
}

void SDL2Widget::releaseSdlObjects()
{
    if (m_sdlTexture)
    {
        SDL_DestroyTexture(m_sdlTexture);
        m_sdlTexture = nullptr;
    }
    if (m_sdlRenderer)
    {
        SDL_DestroyRenderer(m_sdlRenderer);
        m_sdlRenderer = nullptr;
    }
    if (m_sdlWindow)
    {
        SDL_DestroyWindow(m_sdlWindow);
        m_sdlWindow = nullptr;
    }
    m_textureWidth = 0;
    m_textureHeight = 0;
}

void SDL2Widget::renderTexture()
{
    if (!m_sdlRenderer)
        return;

    SDL_SetRenderDrawColor(m_sdlRenderer, 255, 255, 255, 255);
    SDL_RenderClear(m_sdlRenderer);
    if (m_sdlTexture)
    {
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRendererOutputSize(m_sdlRenderer, &outputWidth, &outputHeight);
        if (outputWidth <= 0 || outputHeight <= 0)
        {
            // 嵌入窗口刚切换尺寸时 SDL 可能暂时拿不到输出尺寸
            outputWidth = width();
            outputHeight = height();
        }

        SDL_Rect targetRect = {0, 0, outputWidth, outputHeight};
        if (m_textureWidth > 0 && m_textureHeight > 0 && outputWidth > 0 && outputHeight > 0)
        {
            const double textureRatio = static_cast<double>(m_textureWidth) / m_textureHeight;
            const double outputRatio = static_cast<double>(outputWidth) / outputHeight;

            if (outputRatio > textureRatio)
            {
                targetRect.h = outputHeight;
                targetRect.w = static_cast<int>(outputHeight * textureRatio);
                targetRect.x = (outputWidth - targetRect.w) / 2;
            }
            else
            {
                targetRect.w = outputWidth;
                targetRect.h = static_cast<int>(outputWidth / textureRatio);
                targetRect.y = (outputHeight - targetRect.h) / 2;
            }
        }

        SDL_RenderCopy(m_sdlRenderer, m_sdlTexture, nullptr, &targetRect);
    }
    SDL_RenderPresent(m_sdlRenderer);
}

void SDL2Widget::scheduleRefresh()
{
    refreshRenderer();
    QTimer::singleShot(0, this, &SDL2Widget::refreshRenderer);
    QTimer::singleShot(80, this, &SDL2Widget::refreshRenderer);
    QTimer::singleShot(200, this, &SDL2Widget::refreshRenderer);
}

void SDL2Widget::cacheYuvBuffer(const uint8_t* buffer, int width, int height)
{
    const int ySize = width * height;
    const int uvSize = ySize / 4;
    m_cachedYuvData.resize(ySize + uvSize * 2);
    memcpy(m_cachedYuvData.data(), buffer, m_cachedYuvData.size());
    m_cachedWidth = width;
    m_cachedHeight = height;
}

void SDL2Widget::cacheYuvFrame(const AVFrame *frame, int width, int height)
{
    const int uvWidth = width / 2;
    const int uvHeight = height / 2;
    const int ySize = width * height;
    const int uvSize = uvWidth * uvHeight;

    m_cachedYuvData.resize(ySize + uvSize * 2);
    m_cachedWidth = width;
    m_cachedHeight = height;
    uint8_t *dstY = reinterpret_cast<uint8_t*>(m_cachedYuvData.data());
    uint8_t *dstU = dstY + ySize;
    uint8_t *dstV = dstU + uvSize;

    // FFmpeg 每行可能带 padding，逐行拷贝可以避免花屏
    for (int y = 0; y < height; ++y)
        memcpy(dstY + y * width, frame->data[0] + y * frame->linesize[0], width);
    for (int y = 0; y < uvHeight; ++y)
    {
        memcpy(dstU + y * uvWidth, frame->data[1] + y * frame->linesize[1], uvWidth);
        memcpy(dstV + y * uvWidth, frame->data[2] + y * frame->linesize[2], uvWidth);
    }
}

void SDL2Widget::uploadCachedFrame()
{
    if (m_cachedYuvData.isEmpty() || m_cachedWidth <= 0 || m_cachedHeight <= 0)
        return;

    if (!ensureTexture(m_cachedWidth, m_cachedHeight))
        return;

    const int ySize = m_textureWidth * m_textureHeight;
    const int uvWidth = m_textureWidth / 2;
    const int uvHeight = m_textureHeight / 2;
    const int uvSize = uvWidth * uvHeight;
    if (m_cachedYuvData.size() < ySize + uvSize * 2)
        return;

    const uint8_t *yPlane = reinterpret_cast<const uint8_t*>(m_cachedYuvData.constData());
    const uint8_t *uPlane = yPlane + ySize;
    const uint8_t *vPlane = uPlane + uvSize;

    SDL_UpdateYUVTexture(m_sdlTexture, nullptr,
        yPlane, m_textureWidth,
        uPlane, uvWidth,
        vPlane, uvWidth);
}

void SDL2Widget::processSdlEvents()
{
    if (!m_isFullScreen)
        return;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            setFullScreen(false);
        else if (event.type == SDL_QUIT)
            setFullScreen(false);
    }
}
