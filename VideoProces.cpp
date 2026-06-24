#include "VideoProces.h"
#include <QDebug>
#include <QDateTime>

static constexpr int kErrorBufferSize = 2048;
static constexpr int kFFmpegLogLevel = AV_LOG_ERROR;

#ifdef QT_DEBUG
    #define PRINT_LOG 1
#else
    #define PRINT_LOG 0
#endif

QMutex g_logMutex;


QString formatTime(qint64 milliseconds)
{
    qint64 hours = milliseconds / (1000 * 60 * 60);
    qint64 minutes = (milliseconds / (1000 * 60)) % 60;
    qint64 seconds = (milliseconds / 1000) % 60;

    return QString("%1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
}


void appendLog(const QString &logTitle, const QString &logStr)
{
#if PRINT_LOG
    if (logStr.isEmpty())
        return;

    QMutexLocker locker(&g_logMutex);
    QString log = QString("[%1] [%2] %3")
                      .arg(QDateTime::currentDateTime().toString("MM-dd hh:mm:ss.zzz"))
                      .arg(logTitle)
                      .arg(logStr);

    qDebug() << log;
#else
    Q_UNUSED(logTitle);
    Q_UNUSED(logStr);
#endif
}
void ffmpegLogCallback(void* ptr, int level, const char* fmt, va_list vl)
{
    Q_UNUSED(ptr);

#if PRINT_LOG
    QMutexLocker locker(&g_logMutex);

    if (level > kFFmpegLogLevel)
        return;

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, vl);

    // appendLog 也会拿 g_logMutex，这里直接输出，避免递归锁死
    QString log = QString("[%1] [%2] %3")
                      .arg(QDateTime::currentDateTime().toString("MM-dd hh:mm:ss.zzz"))
                      .arg("FFmpeg")
                      .arg(QString::fromUtf8(buffer).trimmed());

    qDebug() << log;
#else
    Q_UNUSED(level);
    Q_UNUSED(fmt);
    Q_UNUSED(vl);
#endif
}

VideoDecode::VideoDecode(QObject *parent) : QObject(parent),
    m_isPlaying(false),
    m_isNetworkVideo(false)
{
    moveToThread(&m_videoDecodeThread);
    m_videoDecodeThread.start();

    m_error = new char[kErrorBufferSize];

    av_log_set_callback(ffmpegLogCallback);

#if PRINT_LOG
    av_log_set_level(kFFmpegLogLevel);
#else
    av_log_set_level(AV_LOG_QUIET);
#endif
}

VideoDecode::~VideoDecode()
{
    exitThread();
    delete[] m_error;
    m_error = nullptr;
}

void VideoDecode::ret2ErrorStr(int ret)
{
#if PRINT_LOG
    memset(m_error, 0, kErrorBufferSize);
    av_strerror(ret, m_error, kErrorBufferSize);
    QString errorText = QString("%1 %2").arg(QString("Error：")).arg(QString(m_error));
    appendLog("FFmpeg",errorText);
#else
    Q_UNUSED(ret)
#endif
}

bool VideoDecode::isNetworkVideo(QString filePath)
{
    QUrl url(filePath);
    return url.isValid() && (url.scheme() == "http" || url.scheme() == "https" ||
                             url.scheme() == "rtsp" || url.scheme() == "rtmp");
}

void VideoDecode::free()
{
    if (m_options != nullptr)
    {
        av_dict_free(&m_options);
        m_options = nullptr;
    }
    if (m_codecContext != nullptr)
    {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }
    if (m_formatContext != nullptr)
    {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }
    if (m_packet != nullptr)
    {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    if (m_frame != nullptr)
    {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    appendLog("VideoDecode","All decoding resources freed.");
}

qreal VideoDecode::rationalToDouble(const AVRational* rational)
{
    if (!rational || rational->den == 0)
        return 0;

    return qreal(rational->num) / rational->den;
}

bool VideoDecode::getPlayStatus()
{
    QMutexLocker locker(&m_playStatusMutex);
    return m_isPlaying;
}

void VideoDecode::exitThread()
{
    if (m_videoDecodeThread.isRunning())
    {
        stopDecoding();
        m_videoDecodeThread.quit();
        m_videoDecodeThread.wait();
        appendLog("VideoDecode","Decode thread exited.");
    }
}

void VideoDecode::pause()
{
    QMutexLocker locker(&m_playStatusMutex);
    if (m_isPlaying)
    {
        m_isPlaying = false;
        m_pauseTime = av_gettime();
        appendLog("VideoDecode","Playback paused. m_isPlaying set to false.");
    }
}

void VideoDecode::play()
{
    QMutexLocker locker(&m_playStatusMutex);

    if (!m_isPlaying)
    {
        m_isPlaying = true;
        m_startTime += av_gettime() - m_pauseTime;
        appendLog("VideoDecode","Playback resumed. m_isPlaying set to true.");

        // 网络流恢复播放时尽量追到最新帧，避免继续播放旧缓存
        if (m_formatContext && m_isNetworkVideo)
        {
            int64_t seekTarget = av_gettime();
            seekTarget = av_rescale_q(seekTarget, {1, AV_TIME_BASE}, m_formatContext->streams[m_videoStreamIndex]->time_base);

            if (av_seek_frame(m_formatContext, m_videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY) >= 0)
            {
                avcodec_flush_buffers(m_codecContext);
                appendLog("VideoDecode","Seeked to latest keyframe.");
            }
            else
            {
                appendLog("VideoDecode","Failed to seek to latest keyframe.");
            }
        }
        else
        {
            appendLog("VideoDecode","Skipping seeking to keyframe for local video.");
        }

        m_playStatusCondition.wakeAll();
    }
}


void VideoDecode::stopDecoding()
{
    QMutexLocker locker(&m_playStatusMutex);
    m_isPlaying = false;
    m_stopRequested = true;
    m_seekRequested = false;
    m_pendingSeekOffsetUs = 0;
    appendLog("VideoDecode","Decoding stopped. m_isPlaying set to false.");
    m_playStatusCondition.wakeAll();
}

bool VideoDecode::open(QString url)
{
    if(url.isNull())
    {
        m_isPlaying = false;
        return false;
    }

    {
        QMutexLocker locker(&m_playStatusMutex);
        m_stopRequested = false;
        m_seekRequested = false;
        m_pendingSeekOffsetUs = 0;
    }

    if (m_isPlaying)
    {
        appendLog("VideoDecode","A video is already being played.");
    }

    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    if (!m_packet || !m_frame)
    {
        appendLog("VideoDecode","Failed to allocate packet or frame.");
        free();
        m_isPlaying = false;
        return false;
    }

    m_options = nullptr;
    if(isNetworkVideo(url))
    {
        m_isNetworkVideo = true;
        av_dict_set(&m_options, "rtsp_transport", "tcp", 0);
        av_dict_set(&m_options, "fflags", "nobuffer", 0);
        av_dict_set(&m_options, "max_delay", "500000", 0);
        av_dict_set(&m_options, "flush_packets", "1", 0);
    }
    else
    {
        m_isNetworkVideo = false;
    }

    int ret = avformat_open_input(&m_formatContext, url.toStdString().c_str(),nullptr,&m_options);
    if(ret < 0)
    {
        ret2ErrorStr(ret);
        free();
        m_isPlaying = false;
        return false;
    }

    ret = avformat_find_stream_info(m_formatContext, nullptr);
    if(ret < 0)
    {
        ret2ErrorStr(ret);
        free();
        m_isPlaying = false;
        return false;
    }
    m_totalTime = m_formatContext->duration > 0 ? m_formatContext->duration / (AV_TIME_BASE / 1000) : -1;

    m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if(m_videoStreamIndex < 0)
    {
        ret2ErrorStr(m_videoStreamIndex);
        free();
        m_isPlaying = false;
        return false;
    }

    m_videoStream = m_formatContext->streams[m_videoStreamIndex];

    m_size.setWidth(m_videoStream->codecpar->width);
    m_size.setHeight(m_videoStream->codecpar->height);
    m_frameRate = rationalToDouble(&m_videoStream->avg_frame_rate);

    const AVCodec* codec = avcodec_find_decoder(m_videoStream->codecpar->codec_id);
    if (!codec)
    {
        appendLog("VideoDecode","Unsupported video codec.");
        free();
        m_isPlaying = false;
        return false;
    }

    appendLog("VideoDecode",QString("w:%1,h:%2,%3fps,%4")
                .arg(m_size.width()).arg(m_size.height()).arg(m_frameRate).arg(codec->long_name));

    emit videoInfo(m_size.width(),m_size.height(),m_frameRate,QString(codec->name));

    m_codecContext = avcodec_alloc_context3(codec);
    if(!m_codecContext)
    {
        appendLog("VideoDecode",QString::fromLocal8Bit("创建视频解码器上下文失败！"));
        free();
        m_isPlaying = false;
        return false;
    }

    ret = avcodec_parameters_to_context(m_codecContext, m_videoStream->codecpar);
    if(ret < 0)
    {
        ret2ErrorStr(ret);
        free();
        m_isPlaying = false;
        return false;
    }

    m_codecContext->flags2 |= AV_CODEC_FLAG2_FAST;
    m_codecContext->thread_type = FF_THREAD_SLICE;
    m_codecContext->thread_count = 0;

    ret = avcodec_open2(m_codecContext, nullptr, nullptr);
    if(ret < 0)
    {
        ret2ErrorStr(ret);
        free();
        m_isPlaying = false;
        return false;
    }

    m_startTime = av_gettime();

    if(!m_formatContext)
    {
        m_isPlaying = false;
        free();
        return false;
    }

    AVRational timebase, avTimeBase;
    int64_t packetTimeUs, elapsedUs;

    while (av_read_frame(m_formatContext, m_packet) >= 0)
    {
        {
            QMutexLocker locker(&m_playStatusMutex);
            while (!m_isPlaying && !m_stopRequested)
            {
                appendLog("VideoDecode","Decoder paused. Waiting...");
                m_playStatusCondition.wait(&m_playStatusMutex);
            }

            if (m_stopRequested)
            {
                appendLog("VideoDecode","Stop requested. Exiting decode loop.");
                m_isPlaying = false;
                av_packet_unref(m_packet);
                break;
            }
        }

        if (applyPendingSeek())
        {
            av_packet_unref(m_packet);
            continue;
        }

        if (!m_videoDecodeThread.isRunning())
        {
            appendLog("VideoDecode","Thread is stopping. Exiting decode loop.");
            {
                QMutexLocker locker(&m_playStatusMutex);
                m_isPlaying = false;
            }
            av_packet_unref(m_packet);
            break;
        }

        if(m_packet->stream_index == m_videoStreamIndex)
        {
             timebase = m_formatContext->streams[m_videoStreamIndex]->time_base;
             avTimeBase = {1,AV_TIME_BASE};
             elapsedUs = av_gettime() - m_startTime;

             packetTimeUs = 0;
             if (!m_isNetworkVideo && m_packet->dts != AV_NOPTS_VALUE)
                 packetTimeUs = av_rescale_q(m_packet->dts,timebase,avTimeBase);

             if (packetTimeUs > elapsedUs)
                 av_usleep(static_cast<uint>(packetTimeUs - elapsedUs));

             emit videoTime(m_totalTime,elapsedUs / (AV_TIME_BASE / 1000));

             if(avcodec_send_packet(m_codecContext, m_packet) == 0)
             {
                 while (avcodec_receive_frame(m_codecContext, m_frame) == 0)
                 {
                     // 每个渲染器独占并释放自己的 AVFrame，避免切换渲染器时重复释放。
                     AVFrame *frameToSend = av_frame_alloc();
                     if (frameToSend && av_frame_ref(frameToSend, m_frame) == 0)
                     {
                         emit videoPacket(frameToSend, m_size.width(), m_size.height());
                     }
                     else
                     {
                         appendLog("VideoDecode","Failed to copy decoded frame.");
                         if (frameToSend)
                             av_frame_free(&frameToSend);
                     }
                     av_frame_unref(m_frame);
                 }
             }
        }
        av_packet_unref(m_packet);
    }
    {
        QMutexLocker locker(&m_playStatusMutex);
        m_isPlaying = false;
    }
    free();
    return true;
}

bool VideoDecode::applyPendingSeek()
{
    int64_t seekOffsetUs = 0;
    {
        QMutexLocker locker(&m_playStatusMutex);
        if (!m_seekRequested)
            return false;

        seekOffsetUs = m_pendingSeekOffsetUs;
        m_seekRequested = false;
        m_pendingSeekOffsetUs = 0;
    }

    if (!m_formatContext || !m_codecContext || m_videoStreamIndex < 0)
    {
        appendLog("VideoDecode","Cannot seek: Invalid format context or video index.");
        return false;
    }

    int64_t targetUs = av_gettime() - m_startTime + seekOffsetUs;
    if (targetUs < 0)
        targetUs = 0;

    if (m_totalTime > 0)
    {
        const int64_t durationUs = m_totalTime * 1000;
        if (targetUs > durationUs)
            targetUs = durationUs;
    }

    const AVRational timeBase = m_formatContext->streams[m_videoStreamIndex]->time_base;
    const int64_t targetPts = av_rescale_q(targetUs, {1, AV_TIME_BASE}, timeBase);
    if (av_seek_frame(m_formatContext, m_videoStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY) < 0)
    {
        appendLog("VideoDecode","Seek failed.");
        return false;
    }

    avcodec_flush_buffers(m_codecContext);
    m_startTime = av_gettime() - targetUs;
    emit videoTime(m_totalTime, targetUs / 1000);
    appendLog("VideoDecode",QString("Seeked to %1 ms.").arg(targetUs / 1000));
    return true;
}

void VideoDecode::seekBackward(int second)
{
    QMutexLocker locker(&m_playStatusMutex);

    if (!m_formatContext || m_videoStreamIndex < 0)
    {
        appendLog("VideoDecode","Cannot seek backward: Invalid format context or video index.");
        return;
    }

    m_pendingSeekOffsetUs -= static_cast<int64_t>(second) * AV_TIME_BASE;
    m_seekRequested = true;
    m_playStatusCondition.wakeAll();

    appendLog("VideoDecode",QString("Seek backward requested: %1 second.").arg(second));
}

void VideoDecode::seekForward(int second)
{
    QMutexLocker locker(&m_playStatusMutex);

    if (!m_formatContext || m_videoStreamIndex < 0)
    {
        appendLog("VideoDecode","Cannot seek forward: Invalid format context or video index.");
        return;
    }

    m_pendingSeekOffsetUs += static_cast<int64_t>(second) * AV_TIME_BASE;
    m_seekRequested = true;
    m_playStatusCondition.wakeAll();

    appendLog("VideoDecode",QString("Seek forward requested: %1 second.").arg(second));
}
