#ifndef VIDEOPROCES_H
#define VIDEOPROCES_H

#include <QObject>
#include <QThread>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QUrl>
#include <QSize>

extern "C"{
    #include <libavcodec/avcodec.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/common.h>
    #include <libavutil/frame.h>
    #include <libavutil/samplefmt.h>
    #include <libavutil/opt.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/parseutils.h>
    #include <libavutil/mem.h>
    #include <libswscale/swscale.h>
    #include <libavformat/avformat.h>
    #include <libavutil/time.h>
    #include <libavutil/log.h>
}
// FFmpeg 解码线程，负责读包、解码并把视频帧发送给当前渲染器
class VideoDecode : public QObject
{
    Q_OBJECT
public:
    explicit VideoDecode(QObject *parent = nullptr);
    ~VideoDecode();

    void ret2ErrorStr(int ret);

    qreal rationalToDouble(const AVRational* rational);

    bool isNetworkVideo(QString filePath);

    bool m_isPlaying;
    bool m_isNetworkVideo;

signals:
    // 接收方负责释放 frame
    void videoPacket(AVFrame *frame, int imageWidth, int imageHeight);
    void videoTime(qint64 totalTime,qint64 currentTime);
    void videoInfo(int width, int height, double frameRate, QString codecName);

public slots:
    bool open(QString url);
    void stopDecoding();
    void exitThread();
    void free();
    void play();
    void pause();
    void seekForward(int second);
    void seekBackward(int second);
    bool getPlayStatus();

private:
    QThread m_videoDecodeThread;

    AVFrame*  m_frame = nullptr;
    AVPacket* m_packet = nullptr;
    AVFormatContext* m_formatContext = nullptr;
    AVCodecContext*  m_codecContext  = nullptr;
    AVDictionary* m_options = nullptr;
    AVStream* m_videoStream = nullptr;

    QSize  m_size;
    qreal  m_frameRate = 0;
    int m_videoStreamIndex = -1;
    int64_t m_totalTime = 0;
    int64_t m_startTime = 0;
    int64_t m_pauseTime = 0;

    QMutex m_playStatusMutex;

    QWaitCondition m_playStatusCondition;
    bool m_stopRequested = false;

    char *m_error = nullptr;
};

extern QMutex g_logMutex;
#endif // VIDEOPROCES_H
