#include "OpenGLVideoWidget.h"

#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

extern void appendLog(const QString &logTitle,const QString &logStr);

OpenGLVideoWidget::OpenGLVideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

OpenGLVideoWidget::~OpenGLVideoWidget()
{
    releaseSwsContext();
}

void OpenGLVideoWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void OpenGLVideoWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);

    if (m_image.isNull())
        return;

    QSize targetSize = m_image.size();
    targetSize.scale(size(), Qt::KeepAspectRatio);

    const QRect targetRect(QPoint((width() - targetSize.width()) / 2,
                                  (height() - targetSize.height()) / 2),
                           targetSize);
    painter.drawImage(targetRect, m_image);
}

void OpenGLVideoWidget::presentFrame(const uint8_t* buffer, int imageWidth, int imageHeight)
{
    if (!buffer || imageWidth <= 0 || imageHeight <= 0)
        return;

    QImage image(imageWidth, imageHeight, QImage::Format_RGB888);
    const int ySize = imageWidth * imageHeight;
    const int uvWidth = imageWidth / 2;
    const int uvHeight = imageHeight / 2;
    const uint8_t *yPlane = buffer;
    const uint8_t *uPlane = buffer + ySize;
    const uint8_t *vPlane = uPlane + uvWidth * uvHeight;

    const uint8_t *srcSlice[] = { yPlane, uPlane, vPlane, nullptr };
    const int srcStride[] = { imageWidth, uvWidth, uvWidth, 0 };
    uint8_t *dstSlice[] = { image.bits(), nullptr, nullptr, nullptr };
    const int dstStride[] = { image.bytesPerLine(), 0, 0, 0 };

    SwsContext *context = sws_getContext(imageWidth, imageHeight, AV_PIX_FMT_YUV420P,
                                         imageWidth, imageHeight, AV_PIX_FMT_RGB24,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!context)
        return;

    sws_scale(context, srcSlice, srcStride, 0, imageHeight, dstSlice, dstStride);
    sws_freeContext(context);

    m_image = image;
    update();
}

void OpenGLVideoWidget::presentYuvFrame(AVFrame *frame, int imageWidth, int imageHeight)
{
    if (!frame)
        return;

    if (imageWidth <= 0 || imageHeight <= 0)
    {
        av_frame_free(&frame);
        return;
    }

    if (frame->flags & AV_FRAME_FLAG_CORRUPT)
    {
        appendLog("OpenGL","Skipping corrupted frame.");
        av_frame_free(&frame);
        return;
    }

    const AVPixelFormat frameFormat = static_cast<AVPixelFormat>(frame->format);
    if (!m_swsContext || m_frameWidth != imageWidth || m_frameHeight != imageHeight || m_frameFormat != frameFormat)
    {
        releaseSwsContext();
        m_swsContext = sws_getContext(imageWidth, imageHeight, frameFormat,
                                      imageWidth, imageHeight, AV_PIX_FMT_RGB24,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_swsContext)
        {
            appendLog("OpenGL","Failed to create sws context.");
            av_frame_free(&frame);
            return;
        }

        m_frameWidth = imageWidth;
        m_frameHeight = imageHeight;
        m_frameFormat = frameFormat;
    }

    QImage image(imageWidth, imageHeight, QImage::Format_RGB888);
    uint8_t *dstData[] = { image.bits(), nullptr, nullptr, nullptr };
    const int dstLineSize[] = { image.bytesPerLine(), 0, 0, 0 };

    sws_scale(m_swsContext, frame->data, frame->linesize, 0, imageHeight, dstData, dstLineSize);
    av_frame_free(&frame);

    m_image = image;
    update();
}

void OpenGLVideoWidget::setFullScreen(bool flag)
{
    if (flag)
        window()->showFullScreen();
    else
        window()->showNormal();

    m_isFullScreen = flag;
    refreshRenderer();
}

void OpenGLVideoWidget::clearRenderer()
{
    m_image = QImage();
    update();
    appendLog("OpenGL","Renderer cleared.");
}

void OpenGLVideoWidget::refreshRenderer()
{
    update();
}

void OpenGLVideoWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton)
    {
        QMenu menu;
        menu.addAction(tr("全屏"), this, [this] { setFullScreen(true); });
        menu.addAction(tr("退出全屏"), this, [this] { setFullScreen(false); });
        menu.exec(event->globalPos());
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void OpenGLVideoWidget::keyPressEvent(QKeyEvent *event)
{
    if(event)
        emit keyPress(event->key());
}

void OpenGLVideoWidget::releaseSwsContext()
{
    if (m_swsContext)
    {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    m_frameWidth = 0;
    m_frameHeight = 0;
    m_frameFormat = AV_PIX_FMT_NONE;
}
