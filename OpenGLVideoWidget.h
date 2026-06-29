#ifndef OPENGLVIDEOWIDGET_H
#define OPENGLVIDEOWIDGET_H

#include <QImage>
#include <QKeyEvent>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>

extern "C" {
    #include <libavutil/frame.h>
    #include <libswscale/swscale.h>
}

// OpenGL 渲染窗口，目前用 QPainter 画转换后的 RGB 图
class OpenGLVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit OpenGLVideoWidget(QWidget *parent = nullptr);
    ~OpenGLVideoWidget() override;

    bool m_isFullScreen = false;

signals:
    void keyPress(int key);

public slots:
    void presentFrame(const uint8_t* buffer, int imageWidth, int imageHeight);
    void presentYuvFrame(AVFrame* frame, int imageWidth, int imageHeight);
    void setFullScreen(bool flag);
    void clearRenderer();
    void refreshRenderer();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void releaseSwsContext();

    QImage m_image;
    SwsContext *m_swsContext = nullptr;

    // 尺寸或像素格式变了才重建 swsContext
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    AVPixelFormat m_frameFormat = AV_PIX_FMT_NONE;
};

#endif // OPENGLVIDEOWIDGET_H
