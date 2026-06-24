#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QObject>
#include <QMainWindow>
#include <QAction>
#include <QMenu>
#include <QHBoxLayout>
#include <QLabel>

#include "SDL2Widget.h"
#include "OpenGLVideoWidget.h"
#include "VideoProces.h"
#include "OpenNetWorkVideoWindow.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void updateVideoTime(qint64 totalTime,qint64 currentTime);
    bool customKeyPressProcess(int key);
    void setFileNameShow(QString fileName);

public slots:
    void showVideoInfo(int width, int height, double frameRate, QString codecName);
    void playNetworkVideo(QString url);
    void stopVideoPlay();

signals:

private slots:
    void on_action_openLocal_triggered();
    void on_action_openNetWork_triggered();

private:
    enum class RenderType
    {
        SDL2,
        OpenGL
    };

    Ui::MainWindow *ui;

    VideoDecode *m_videoDecode;
    SDL2Widget *m_sdl2Widget;
    OpenGLVideoWidget *m_openGLWidget;
    RenderType m_renderType = RenderType::SDL2;
    QAction *m_useSDLRendererAction;
    QAction *m_useOpenGLRendererAction;

    OpenNetWorkVideoWindow *m_openNetworkVideoWindow;
    QHBoxLayout *m_statusBarLayout;
    QLabel *m_fileNameLabel;
    QLabel *m_videoInfoLabel;

    void initStatusBar();
    void initRenderWidgets();
    void initRenderMenu();
    void initWindowStyle();
    void initVideoDecode();

    // 当前只有一个渲染器接收帧，避免切换绘制方式时两个窗口同时消费 AVFrame
    void connectVideoRenderer();
    void clearCurrentRenderer();
    void refreshCurrentRenderer();
    void setCurrentRendererFullScreen(bool flag);
    void switchRenderer(RenderType renderType);
    void resetVideoView(int focusDelayMs);
    void startVideo(const QString &url, bool closeNetworkWindow);
    void togglePlay();
    void updatePlaybackUi(bool playing);

    QString m_lastUrl;

protected:
    void keyPressEvent(QKeyEvent *event) override;
};
extern MainWindow *g_appPtr;
#endif // MAINWINDOW_H
