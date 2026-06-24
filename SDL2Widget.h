#ifndef SDL2WIDGET_H
#define SDL2WIDGET_H

#include <QByteArray>
#include <QWidget>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QMouseEvent>

extern "C" {
    #include "SDL.h"
    #include <libavutil/frame.h>
}
// SDL2 渲染窗口,普通模式嵌入 Qt 窗口，全屏模式使用独立 SDL 窗口
class SDL2Widget : public QWidget
{
    Q_OBJECT
public:
    explicit SDL2Widget(QWidget *parent = nullptr);
    ~SDL2Widget() override;

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
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool createEmbeddedRenderer();
    bool createFullscreenRenderer();
    bool ensureTexture(int width, int height);
    void releaseSdlObjects();
    void syncRenderSize();
    void renderTexture();
    void scheduleRefresh();
    void cacheYuvBuffer(const uint8_t* buffer, int width, int height);
    void cacheYuvFrame(const AVFrame *frame, int width, int height);
    void uploadCachedFrame();
    void processSdlEvents();

    SDL_Window*   m_sdlWindow = nullptr;
    SDL_Renderer* m_sdlRenderer = nullptr;
    SDL_Texture*  m_sdlTexture = nullptr;

    int m_textureWidth = 0;
    int m_textureHeight = 0;

    // 切换全屏或重建 SDL Renderer 后，用最后一帧恢复画面，避免短暂黑屏
    QByteArray m_cachedYuvData;

    int m_cachedWidth = 0;
    int m_cachedHeight = 0;
    bool m_sdlInitialized = false;
};

#endif // SDL2WIDGET_H
