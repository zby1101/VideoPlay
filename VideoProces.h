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
    #include <libswresample/swresample.h>
    #include "SDL.h"
}
// FFmpeg 解码线程，读包、解码、把帧交给渲染器
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
    // frame 交给接收方释放
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
    void setPlaybackSpeed(double speed);
    bool getPlayStatus();

private:
    bool applyPendingSeek();
    bool initAudioDecoder();
    bool initAudioOutput();
    bool ensureAudioResampler(const AVFrame *frame, double playbackSpeed);
    void decodeAudioPacket();
    void closeAudio();
    void clearAudioQueue();
    int64_t currentMediaClockUs(int64_t fallbackClockUs);
    int64_t currentAudioClockUs();
    void throttleAudioQueue();
    bool audioQueueReadyToStart();
    void startAudioIfReady();

    QThread m_videoDecodeThread;

    AVFrame*  m_frame = nullptr;
    AVFrame*  m_audioFrame = nullptr;
    AVPacket* m_packet = nullptr;
    AVFormatContext* m_formatContext = nullptr;
    AVCodecContext*  m_codecContext  = nullptr;
    AVCodecContext*  m_audioCodecContext = nullptr;
    AVDictionary* m_options = nullptr;
    AVStream* m_videoStream = nullptr;
    AVStream* m_audioStream = nullptr;
    SwrContext* m_swrContext = nullptr;

    QSize  m_size;
    qreal  m_frameRate = 0;
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
    int64_t m_totalTime = 0;
    int64_t m_startTime = 0;
    int64_t m_pauseTime = 0;
    double m_playbackSpeed = 1.0;
    bool m_audioAvailable = false;
    bool m_audioSubsystemInitialized = false;
    bool m_audioStarted = false;
    bool m_firstVideoFrameSent = false;
    SDL_AudioDeviceID m_audioDevice = 0;
    SDL_AudioSpec m_audioSpec;
    int m_audioOutSampleRate = 0;
    int64_t m_audioClockUs = 0;

    QMutex m_playStatusMutex;

    QWaitCondition m_playStatusCondition;
    bool m_stopRequested = false;
    bool m_seekRequested = false;
    int64_t m_pendingSeekOffsetUs = 0;

    char *m_error = nullptr;
};

extern QMutex g_logMutex;
#endif // VIDEOPROCES_H
