#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <QActionGroup>
#include <QFileDialog>
#include <QTimer>
#include <QFileInfo>
#include <QStyle>

extern QString formatTime(qint64 milliseconds);
extern void appendLog(const QString &logTitle,const QString &logStr);

MainWindow *g_appPtr;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_videoDecode(nullptr),
    m_sdl2Widget(nullptr),
    m_openGLWidget(nullptr),
    m_useSDLRendererAction(nullptr),
    m_useOpenGLRendererAction(nullptr),
    m_openNetworkVideoWindow(nullptr),
    m_statusBarLayout(nullptr),
    m_fileNameLabel(nullptr),
    m_videoInfoLabel(nullptr),
    m_lastUrl(QString())
{
    ui->setupUi(this);

    g_appPtr = this;

    this->setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

    initWindowStyle();
    initStatusBar();

    initRenderWidgets();
    initRenderMenu();

    resetVideoView(0);

    ui->progressBar->setValue(0);
    updatePlaybackUi(false);

    SDL_version linked;
    SDL_GetVersion(&linked);

    QString sdl2Version = QString("%1.%2.%3")
        .arg(QString::number(static_cast<int>(linked.major)))
        .arg(QString::number(static_cast<int>(linked.minor)))
        .arg(QString::number(static_cast<int>(linked.patch)));

    appendLog("Main",QString("SDL2 VersionInfo: ") + sdl2Version);


    appendLog("Main",QString("FFmpeg VersionInfo:") + QString(av_version_info()));

    // AVFrame 跨线程发送给渲染器，需要注册元类型
    qRegisterMetaType<AVFrame*>("AVFrame");


    initVideoDecode();

    connect(ui->playCtrl_btn, &QPushButton::clicked, this, [this]() {
        togglePlay();
    });

    connect(ui->stopPlay_btn,&QPushButton::clicked,this,&MainWindow::stopVideoPlay);
}

MainWindow::~MainWindow()
{
    if (m_videoDecode)
    {
        m_videoDecode->exitThread();
        delete m_videoDecode;
        m_videoDecode = nullptr;
    }
    delete ui;
}

void MainWindow::updateVideoTime(qint64 totalTime, qint64 currentTime)
{
    const bool hasDuration = totalTime > 0;
    ui->progressBar->setMaximum(hasDuration ? static_cast<int>(totalTime) : 0);
    ui->progressBar->setValue(hasDuration && currentTime > 0 ? static_cast<int>(currentTime) : 0);

    QString totalTimeText = formatTime(totalTime);
    QString currentTimeText = formatTime(currentTime);

    // 网络流通常拿不到总时长，只显示当前播放时间
    if(totalTime < 0)
        totalTimeText = "-";
    if(currentTime < 0)
        currentTimeText = "-";

    ui->label_time->setText(QString("%1 / %2").arg(currentTimeText).arg(totalTimeText));
}

bool MainWindow::customKeyPressProcess(int key)
{
    if(Qt::Key_Escape == key)
    {
        setCurrentRendererFullScreen(false);
        appendLog("Main","exit fullScreen");
        return true;
    }
    else if (Qt::Key_Left == key)
    {
        appendLog("Main","Left Pressed");
        if(m_videoDecode->getPlayStatus() && !m_videoDecode->m_isNetworkVideo)
        {
            try {
                m_videoDecode->seekBackward(3);
            }
            catch (const std::exception &e)
            {
                appendLog("Main",QString("Exception during seek backward: %1").arg(e.what()));
            }
        }
        return true;
    }
    else if (Qt::Key_Right == key)
    {
        appendLog("Main","Right Pressed");
        if (m_videoDecode->getPlayStatus() && !m_videoDecode->m_isNetworkVideo)
        {
            try {
                m_videoDecode->seekForward(3);
            }
            catch (const std::exception &e)
            {
                appendLog("Main",QString("Exception during seek forward: %1").arg(e.what()));
            }
        }
        return true;
    }

    else if (Qt::Key_Up == key)
    {
        appendLog("Main","Up Pressed");
        return true;
    }
    else if (Qt::Key_Down == key)
    {
        appendLog("Main","Down Pressed");
        return true;
    }
    else if (Qt::Key_Space == key)
    {
        togglePlay();
        return true;
    }
    else
    {
        return false;
    }
}

void MainWindow::setFileNameShow(QString fileName)
{
    m_fileNameLabel->setText(fileName);
}

void MainWindow::showVideoInfo(int width, int height, double frameRate, QString codecName)
{
    const QString resolutionText = (width > 0 && height > 0) ? QString("%1x%2").arg(width).arg(height) : "-";
    const QString frameRateText = frameRate > 0 ? QString::number(frameRate) : "-";
    const QString codecText = codecName.isEmpty() ? "-" : codecName;
    QString info = QString("%1:%2 %3:%4 %5:%6")
            .arg(QString("分辨率")).arg(resolutionText).arg(QString("帧率")).arg(frameRateText).
            arg(QString("解码器")).arg(codecText);

    m_videoInfoLabel->setText(info);
}

void MainWindow::playNetworkVideo(QString url)
{
    startVideo(url, true);
}

void MainWindow::stopVideoPlay()
{
    initVideoDecode();
    clearCurrentRenderer();

    resetVideoView(300);

    setFileNameShow(tr("未打开视频"));
    updateVideoTime(-1,-1);
    showVideoInfo(0,0,0,"-");
    updatePlaybackUi(false);
}

void MainWindow::resetVideoView(int focusDelayMs)
{
    QTimer::singleShot(focusDelayMs, this, [this]() {
       resize(width() + 1, height());
       resize(width() - 1, height());

       // 重新抢回键盘焦点，保证空格、Esc、方向键仍然交给播放器处理
       this->setFocusPolicy(Qt::StrongFocus);
       this->setFocus();
       refreshCurrentRenderer();
    });
}

void MainWindow::startVideo(const QString &url, bool closeNetworkWindow)
{
    if (url.isEmpty())
        return;

    initVideoDecode();

    clearCurrentRenderer();

    m_lastUrl = url;

    QMetaObject::invokeMethod(m_videoDecode, "play", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_videoDecode, "open", Qt::QueuedConnection, Q_ARG(QString, url));
    updatePlaybackUi(true);

    QTimer::singleShot(500, this, [=]() {
        if (!m_videoDecode->getPlayStatus())
        {
            updatePlaybackUi(false);
            return;
        }

        QFileInfo info(url);
        setFileNameShow(info.fileName());

        if (closeNetworkWindow && m_openNetworkVideoWindow)
            m_openNetworkVideoWindow->close();
    });
}

void MainWindow::togglePlay()
{
    if (m_videoDecode->getPlayStatus())
    {
        m_videoDecode->pause();
        updatePlaybackUi(false);
        return;
    }

    m_videoDecode->play();
    updatePlaybackUi(true);
}

void MainWindow::on_action_openLocal_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this,tr("选择视频文件"), "",
                tr("Video Files (*.mp4 *.mov *.avi *.mkv *.flv *.wmv *.mpeg *.mpg *.3gp *.webm *.ogg *.ts *.m4v);;All Files (*.*)")
            );

    if(!fileName.isEmpty())
    {
        if(QFile::exists(fileName))
        {
            if (m_lastUrl != fileName || m_lastUrl.isEmpty())
            {
                startVideo(fileName, false);
            }
        }
    }
}

void MainWindow::on_action_openNetWork_triggered()
{
    if(m_openNetworkVideoWindow != nullptr)
    {
        delete m_openNetworkVideoWindow;
        m_openNetworkVideoWindow = nullptr;
    }
    m_openNetworkVideoWindow = new OpenNetWorkVideoWindow(this);
    m_openNetworkVideoWindow->setAttribute(Qt::WA_DeleteOnClose);
    m_openNetworkVideoWindow->show();
    connect(m_openNetworkVideoWindow,&OpenNetWorkVideoWindow::openUrl,this,&MainWindow::playNetworkVideo);
    connect(m_openNetworkVideoWindow, &QObject::destroyed, this, [this]() {
        m_openNetworkVideoWindow = nullptr;
    });
}

void MainWindow::initRenderWidgets()
{
    m_sdl2Widget = new SDL2Widget(this);
    m_openGLWidget = new OpenGLVideoWidget(this);

    ui->video_layout->setContentsMargins(0, 0, 0, 0);
    ui->video_layout->addWidget(m_sdl2Widget);
    ui->video_layout->addWidget(m_openGLWidget);

    connect(m_sdl2Widget,&SDL2Widget::keyPress,this,&MainWindow::customKeyPressProcess);
    connect(m_openGLWidget,&OpenGLVideoWidget::keyPress,this,&MainWindow::customKeyPressProcess);

    m_sdl2Widget->show();
    m_openGLWidget->hide();
}

void MainWindow::initRenderMenu()
{
    QActionGroup *renderGroup = new QActionGroup(this);
    renderGroup->setExclusive(true);

    QMenu *viewMenu = ui->menuBar->addMenu(tr("视图"));
    QMenu *rendererMenu = viewMenu->addMenu(tr("渲染器"));

    m_useSDLRendererAction = rendererMenu->addAction(tr("SDL2 渲染"));
    m_useOpenGLRendererAction = rendererMenu->addAction(tr("OpenGL 渲染"));
    viewMenu->addSeparator();
    QAction *enterFullScreenAction = viewMenu->addAction(tr("进入全屏"));
    QAction *exitFullScreenAction = viewMenu->addAction(tr("退出全屏"));

    m_useSDLRendererAction->setCheckable(true);
    m_useOpenGLRendererAction->setCheckable(true);
    m_useSDLRendererAction->setChecked(true);

    renderGroup->addAction(m_useSDLRendererAction);
    renderGroup->addAction(m_useOpenGLRendererAction);

    connect(m_useSDLRendererAction, &QAction::triggered, this, [this]() {
        switchRenderer(RenderType::SDL2);
    });
    connect(m_useOpenGLRendererAction, &QAction::triggered, this, [this]() {
        switchRenderer(RenderType::OpenGL);
    });
    connect(enterFullScreenAction, &QAction::triggered, this, [this]() {
        setCurrentRendererFullScreen(true);
    });
    connect(exitFullScreenAction, &QAction::triggered, this, [this]() {
        setCurrentRendererFullScreen(false);
    });
}

void MainWindow::initWindowStyle()
{
    setWindowTitle(tr("VideoPlay"));
    setMinimumSize(820, 560);

    ui->action_openLocal->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    ui->action_openNetWork->setIcon(style()->standardIcon(QStyle::SP_DriveNetIcon));

    ui->playCtrl_btn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    ui->stopPlay_btn->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    ui->playCtrl_btn->setToolTip(tr("播放或暂停"));
    ui->stopPlay_btn->setToolTip(tr("停止播放"));

    setStyleSheet(R"(
        QMainWindow {
            background: #111419;
        }
        QMenuBar {
            background: #f7f8fa;
            border-bottom: 1px solid #d9dee7;
            padding: 3px 8px;
            color: #222831;
        }
        QMenuBar::item {
            padding: 6px 12px;
            border-radius: 4px;
        }
        QMenuBar::item:selected {
            background: #e8edf5;
        }
        QMenu {
            background: #ffffff;
            border: 1px solid #cfd6e2;
            padding: 6px;
        }
        QMenu::item {
            padding: 7px 28px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background: #edf4ff;
            color: #0f4c81;
        }
        QWidget#centralWidget {
            background: #111419;
        }
        QWidget#videoFrame {
            background: #07090d;
            border: 1px solid #2e3540;
            border-radius: 8px;
        }
        QWidget#controlBar {
            background: #181d24;
            border-top: 1px solid #2b323d;
        }
        QLabel#label_time {
            color: #d7dde8;
            font-size: 12px;
            min-width: 112px;
        }
        QProgressBar {
            border: 1px solid #303948;
            border-radius: 4px;
            background: #0f1319;
            color: #d7dde8;
            text-align: center;
            min-height: 10px;
            max-height: 10px;
        }
        QProgressBar::chunk {
            border-radius: 3px;
            background: #4aa3ff;
        }
        QPushButton {
            background: #26313f;
            border: 1px solid #3b4858;
            border-radius: 5px;
            color: #f5f7fb;
            min-height: 30px;
            padding: 7px 14px;
            min-width: 74px;
        }
        QPushButton:hover {
            background: #314052;
        }
        QPushButton:pressed {
            background: #1f2833;
        }
        QStatusBar {
            background: #f7f8fa;
            color: #3a4250;
            border-top: 1px solid #d9dee7;
        }
        QStatusBar QLabel {
            color: #3a4250;
        }
    )");
}

void MainWindow::initStatusBar()
{
    QWidget *w = new QWidget;
    ui->statusBar->addWidget(w);

    m_statusBarLayout = new QHBoxLayout;
    m_statusBarLayout->setContentsMargins(6, 0, 6, 0);
    m_statusBarLayout->setSpacing(16);

    m_fileNameLabel = new QLabel(tr("未打开视频"));
    m_videoInfoLabel = new QLabel(tr("分辨率:- 帧率:- 解码器:-"));
    m_fileNameLabel->setMinimumWidth(220);
    m_fileNameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_videoInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui->statusBar->setMinimumHeight(m_fileNameLabel->fontMetrics().height() + 12);

    m_statusBarLayout->addWidget(m_fileNameLabel);
    m_statusBarLayout->addWidget(m_videoInfoLabel);
    m_statusBarLayout->addStretch();

    w->setLayout(m_statusBarLayout);
}

void MainWindow::updatePlaybackUi(bool playing)
{
    ui->playCtrl_btn->setText(playing ? tr("暂停") : tr("播放"));
    ui->playCtrl_btn->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

void MainWindow::initVideoDecode()
{
    if(m_videoDecode != nullptr)
    {
        if (m_videoDecode->getPlayStatus())
        {
            m_videoDecode->stopDecoding();
        }
        m_videoDecode->exitThread();
        delete m_videoDecode;
        m_videoDecode = nullptr;
    }
    m_videoDecode = new VideoDecode;

    connectVideoRenderer();

    connect(m_videoDecode,&VideoDecode::videoTime,this,&MainWindow::updateVideoTime);
    connect(m_videoDecode,&VideoDecode::videoInfo,this,&MainWindow::showVideoInfo);
}

void MainWindow::connectVideoRenderer()
{
    if (!m_videoDecode)
        return;

    disconnect(m_videoDecode,&VideoDecode::videoPacket,m_sdl2Widget,&SDL2Widget::presentYuvFrame);
    disconnect(m_videoDecode,&VideoDecode::videoPacket,m_openGLWidget,&OpenGLVideoWidget::presentYuvFrame);

    if (m_renderType == RenderType::SDL2)
    {
        connect(m_videoDecode,&VideoDecode::videoPacket,m_sdl2Widget,&SDL2Widget::presentYuvFrame);
        return;
    }

    connect(m_videoDecode,&VideoDecode::videoPacket,m_openGLWidget,&OpenGLVideoWidget::presentYuvFrame);
}

void MainWindow::clearCurrentRenderer()
{
    if (m_renderType == RenderType::SDL2 && m_sdl2Widget)
    {
        m_sdl2Widget->clearRenderer();
        return;
    }

    if (m_openGLWidget)
        m_openGLWidget->clearRenderer();
}

void MainWindow::refreshCurrentRenderer()
{
    if (m_renderType == RenderType::SDL2 && m_sdl2Widget)
    {
        m_sdl2Widget->refreshRenderer();
        return;
    }

    if (m_openGLWidget)
        m_openGLWidget->refreshRenderer();
}

void MainWindow::setCurrentRendererFullScreen(bool flag)
{
    if (m_renderType == RenderType::SDL2 && m_sdl2Widget)
    {
        m_sdl2Widget->setFullScreen(flag);
        m_openGLWidget->m_isFullScreen = false;
        return;
    }

    if (m_openGLWidget)
    {
        m_openGLWidget->setFullScreen(flag);
        m_sdl2Widget->m_isFullScreen = false;
    }
}

void MainWindow::switchRenderer(RenderType renderType)
{
    if (m_renderType == renderType)
        return;

    if ((m_sdl2Widget && m_sdl2Widget->m_isFullScreen) || (m_openGLWidget && m_openGLWidget->m_isFullScreen))
        setCurrentRendererFullScreen(false);

    // 切换绘制方式时短暂停住解码，避免旧渲染器收到刚发出的帧后再释放一次
    const bool wasPlaying = m_videoDecode && m_videoDecode->getPlayStatus();
    if (wasPlaying)
        m_videoDecode->pause();

    m_renderType = renderType;

    if (m_renderType == RenderType::SDL2)
    {
        m_openGLWidget->hide();
        m_sdl2Widget->show();
        m_useSDLRendererAction->setChecked(true);
    }
    else
    {
        m_sdl2Widget->hide();
        m_openGLWidget->show();
        m_useOpenGLRendererAction->setChecked(true);
    }

    connectVideoRenderer();
    clearCurrentRenderer();
    resetVideoView(0);
    QTimer::singleShot(100, this, &MainWindow::refreshCurrentRenderer);

    if (wasPlaying)
    {
        m_videoDecode->play();
        updatePlaybackUi(true);
    }
    else
    {
        updatePlaybackUi(false);
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if(!customKeyPressProcess(event->key()))
    {
        QMainWindow::keyPressEvent(event);
    }
}
