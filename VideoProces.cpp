#include "VideoProces.h"
#include <QByteArray>
#include <QDebug>
#include <QDateTime>

static constexpr int kErrorBufferSize = 2048;
static constexpr int kFFmpegLogLevel = AV_LOG_ERROR;
// SDL 统一吃双声道 S16，音频帧先转成这个格式
static constexpr int kAudioOutputSampleRate = 48000;
static constexpr int kAudioOutputChannels = 2;
// 起播前先攒一点音频，免得 SDL 刚开始就断粮
static constexpr int kAudioStartupQueueMaxMs = 160;
// 队列太长会拖慢 seek、暂停和倍速切换
static constexpr int kAudioQueueMaxMs = 500;

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

    // appendLog 也会拿这把锁，这里直接输出
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
    memset(&m_audioSpec, 0, sizeof(m_audioSpec));

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
    closeAudio();

    if (m_options != nullptr)
    {
        av_dict_free(&m_options);
        m_options = nullptr;
    }
    if (m_swrContext != nullptr)
    {
        swr_free(&m_swrContext);
        m_swrContext = nullptr;
    }
    if (m_audioCodecContext != nullptr)
    {
        avcodec_free_context(&m_audioCodecContext);
        m_audioCodecContext = nullptr;
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
    if (m_audioFrame != nullptr)
    {
        av_frame_free(&m_audioFrame);
        m_audioFrame = nullptr;
    }
    m_audioStream = nullptr;
    m_audioStreamIndex = -1;
    m_audioAvailable = false;
    m_audioStarted = false;
    m_firstVideoFrameSent = false;
    m_audioClockUs = 0;
    m_audioOutSampleRate = 0;
    appendLog("VideoDecode","All decoding resources freed.");
}

qreal VideoDecode::rationalToDouble(const AVRational* rational)
{
    if (!rational || rational->den == 0)
        return 0;

    return qreal(rational->num) / rational->den;
}

bool VideoDecode::initAudioDecoder()
{
    // 没有音频流就按纯视频播放
    m_audioStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audioStreamIndex < 0)
    {
        appendLog("Audio","No audio stream found.");
        return false;
    }

    m_audioStream = m_formatContext->streams[m_audioStreamIndex];
    const AVCodec *codec = avcodec_find_decoder(m_audioStream->codecpar->codec_id);
    if (!codec)
    {
        appendLog("Audio","Unsupported audio codec.");
        return false;
    }

    m_audioCodecContext = avcodec_alloc_context3(codec);
    if (!m_audioCodecContext)
    {
        appendLog("Audio","Failed to allocate audio codec context.");
        return false;
    }

    int ret = avcodec_parameters_to_context(m_audioCodecContext, m_audioStream->codecpar);
    if (ret < 0)
    {
        ret2ErrorStr(ret);
        avcodec_free_context(&m_audioCodecContext);
        return false;
    }

    ret = avcodec_open2(m_audioCodecContext, codec, nullptr);
    if (ret < 0)
    {
        ret2ErrorStr(ret);
        avcodec_free_context(&m_audioCodecContext);
        return false;
    }

    if (!initAudioOutput())
    {
        avcodec_free_context(&m_audioCodecContext);
        return false;
    }

    m_audioAvailable = true;
    appendLog("Audio",QString("Audio stream opened: %1").arg(codec->name));
    return true;
}

bool VideoDecode::initAudioOutput()
{
    // 视频渲染可能已经初始化过 SDL，这里只补音频子系统
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        {
            appendLog("Audio",QString("SDL audio init failed: ") + QString(SDL_GetError()));
            return false;
        }
        m_audioSubsystemInitialized = true;
    }

    SDL_AudioSpec desired;
    memset(&desired, 0, sizeof(desired));
    desired.freq = kAudioOutputSampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = kAudioOutputChannels;
    desired.samples = 1024;

    m_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &m_audioSpec, 0);
    if (!m_audioDevice)
    {
        appendLog("Audio",QString("SDL_OpenAudioDevice failed: ") + QString(SDL_GetError()));
        if (m_audioSubsystemInitialized)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            m_audioSubsystemInitialized = false;
        }
        return false;
    }

    // 首帧出来前先攒音频，不让声音抢跑
    SDL_PauseAudioDevice(m_audioDevice, 1);
    return true;
}

bool VideoDecode::ensureAudioResampler(const AVFrame *frame, double playbackSpeed)
{
    if (!frame || !m_audioDevice)
        return false;

    if (playbackSpeed <= 0)
        playbackSpeed = 1.0;

    // 用输出采样率做倍速，同一段媒体时间会得到更少或更多 PCM
    const int targetSampleRate = qMax(8000, static_cast<int>(m_audioSpec.freq / playbackSpeed + 0.5));
    if (m_swrContext && m_audioOutSampleRate == targetSampleRate)
        return true;

    if (m_swrContext)
    {
        swr_free(&m_swrContext);
        m_swrContext = nullptr;
    }

    AVChannelLayout inLayout;
    memset(&inLayout, 0, sizeof(inLayout));
    if (frame->ch_layout.nb_channels > 0)
        av_channel_layout_copy(&inLayout, &frame->ch_layout);
    else
        av_channel_layout_default(&inLayout, frame->channels);

    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    const int ret = swr_alloc_set_opts2(&m_swrContext,
                                        &outLayout,
                                        AV_SAMPLE_FMT_S16,
                                        targetSampleRate,
                                        &inLayout,
                                        static_cast<AVSampleFormat>(frame->format),
                                        frame->sample_rate,
                                        0,
                                        nullptr);
    av_channel_layout_uninit(&inLayout);

    if (ret < 0 || !m_swrContext)
    {
        ret2ErrorStr(ret);
        return false;
    }

    if (swr_init(m_swrContext) < 0)
    {
        appendLog("Audio","Failed to initialize swr context.");
        swr_free(&m_swrContext);
        m_swrContext = nullptr;
        return false;
    }

    m_audioOutSampleRate = targetSampleRate;
    return true;
}

void VideoDecode::decodeAudioPacket()
{
    if (!m_audioAvailable || !m_audioCodecContext || !m_audioDevice)
        return;

    int ret = avcodec_send_packet(m_audioCodecContext, m_packet);
    if (ret < 0)
        return;

    while (avcodec_receive_frame(m_audioCodecContext, m_audioFrame) == 0)
    {
        double playbackSpeed = 1.0;
        {
            QMutexLocker locker(&m_playStatusMutex);
            playbackSpeed = m_isNetworkVideo ? 1.0 : m_playbackSpeed;
        }

        if (!ensureAudioResampler(m_audioFrame, playbackSpeed))
        {
            av_frame_unref(m_audioFrame);
            continue;
        }

        const int outSamples = av_rescale_rnd(
            swr_get_delay(m_swrContext, m_audioFrame->sample_rate) + m_audioFrame->nb_samples,
            m_audioOutSampleRate,
            m_audioFrame->sample_rate,
            AV_ROUND_UP);
        const int bytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
        const int bufferSize = outSamples * kAudioOutputChannels * bytesPerSample;
        if (bufferSize <= 0)
        {
            av_frame_unref(m_audioFrame);
            continue;
        }

        QByteArray audioBuffer(bufferSize, 0);
        uint8_t *outData[] = { reinterpret_cast<uint8_t*>(audioBuffer.data()) };
        const int convertedSamples = swr_convert(m_swrContext,
                                                 outData,
                                                 outSamples,
                                                 const_cast<const uint8_t**>(m_audioFrame->data),
                                                 m_audioFrame->nb_samples);
        if (convertedSamples <= 0)
        {
            av_frame_unref(m_audioFrame);
            continue;
        }

        const int queuedBytes = convertedSamples * kAudioOutputChannels * bytesPerSample;
        throttleAudioQueue();
        if (SDL_QueueAudio(m_audioDevice, audioBuffer.constData(), queuedBytes) < 0)
        {
            appendLog("Audio",QString("SDL_QueueAudio failed: ") + QString(SDL_GetError()));
            av_frame_unref(m_audioFrame);
            continue;
        }

        // 这里记的是队列末尾的媒体时间，真正播到哪儿还得扣掉未播放的队列
        int64_t frameStartUs = m_audioClockUs;
        if (m_audioFrame->best_effort_timestamp != AV_NOPTS_VALUE && m_audioStream)
            frameStartUs = av_rescale_q(m_audioFrame->best_effort_timestamp,
                                        m_audioStream->time_base,
                                        {1, AV_TIME_BASE});

        const int64_t frameDurationUs = av_rescale_q(m_audioFrame->nb_samples,
                                                     {1, m_audioFrame->sample_rate},
                                                     {1, AV_TIME_BASE});
        {
            QMutexLocker locker(&m_playStatusMutex);
            m_audioClockUs = frameStartUs + frameDurationUs;
        }
        startAudioIfReady();
        av_frame_unref(m_audioFrame);
    }
}

void VideoDecode::closeAudio()
{
    clearAudioQueue();
    if (m_audioDevice)
    {
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
    }
    m_audioStarted = false;
    m_firstVideoFrameSent = false;
    if (m_audioSubsystemInitialized)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_audioSubsystemInitialized = false;
    }
}

void VideoDecode::clearAudioQueue()
{
    if (m_audioDevice)
        SDL_ClearQueuedAudio(m_audioDevice);
}

int64_t VideoDecode::currentAudioClockUs()
{
    if (!m_audioAvailable || !m_audioDevice || !m_audioStarted)
        return -1;

    const int bytesPerSecond = m_audioSpec.freq * kAudioOutputChannels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    if (bytesPerSecond <= 0)
        return -1;

    double playbackSpeed = 1.0;
    int64_t audioClockUs = 0;
    {
        QMutexLocker locker(&m_playStatusMutex);
        playbackSpeed = m_isNetworkVideo ? 1.0 : m_playbackSpeed;
        audioClockUs = m_audioClockUs;
    }

    const Uint32 queuedBytes = SDL_GetQueuedAudioSize(m_audioDevice);
    if (audioClockUs <= 0 && queuedBytes == 0)
        return -1;

    // SDL_QueueAudio 没有回调时钟，只能用队列末尾时间反推当前播放点
    const int64_t queuedWallUs = static_cast<int64_t>(queuedBytes) * AV_TIME_BASE / bytesPerSecond;
    const int64_t queuedMediaUs = static_cast<int64_t>(queuedWallUs * playbackSpeed);
    const int64_t clockUs = audioClockUs - queuedMediaUs;
    return clockUs > 0 ? clockUs : 0;
}

int64_t VideoDecode::currentMediaClockUs(int64_t fallbackClockUs)
{
    // 有声音就跟声音走，声音还没起来就先用视频时钟
    const int64_t audioClockUs = currentAudioClockUs();
    return audioClockUs >= 0 ? audioClockUs : fallbackClockUs;
}

void VideoDecode::throttleAudioQueue()
{
    if (!m_audioDevice)
        return;

    // 首帧前不在音频队列上等，免得开头音频包多时画面出不来
    if (!m_audioStarted)
        return;

    const int bytesPerSecond = m_audioSpec.freq * kAudioOutputChannels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    const Uint32 maxQueuedBytes = static_cast<Uint32>(bytesPerSecond * kAudioQueueMaxMs / 1000);
    // 别把 SDL 队列灌太满，否则暂停和跳转都会带着旧声音
    while (SDL_GetQueuedAudioSize(m_audioDevice) > maxQueuedBytes)
    {
        {
            QMutexLocker locker(&m_playStatusMutex);
            if (!m_isPlaying || m_stopRequested)
                return;
        }
        av_usleep(5 * 1000);
    }
}

bool VideoDecode::audioQueueReadyToStart()
{
    if (!m_audioDevice || m_audioStarted)
        return false;

    const int bytesPerSecond = m_audioSpec.freq * kAudioOutputChannels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    if (bytesPerSecond <= 0)
        return false;

    const Uint32 maxQueuedBytes = static_cast<Uint32>(bytesPerSecond * kAudioStartupQueueMaxMs / 1000);
    return SDL_GetQueuedAudioSize(m_audioDevice) >= maxQueuedBytes;
}

void VideoDecode::startAudioIfReady()
{
    if (!m_audioAvailable || !m_audioDevice || m_audioStarted || !m_firstVideoFrameSent)
        return;

    if (!audioQueueReadyToStart())
        return;

    {
        QMutexLocker locker(&m_playStatusMutex);
        if (!m_isPlaying || m_stopRequested)
            return;

        m_audioStarted = true;
    }

    SDL_PauseAudioDevice(m_audioDevice, 0);
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
        if (m_audioDevice && m_audioStarted)
            SDL_PauseAudioDevice(m_audioDevice, 1);
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
        if (m_audioDevice && m_audioStarted)
            SDL_PauseAudioDevice(m_audioDevice, 0);
        appendLog("VideoDecode","Playback resumed. m_isPlaying set to true.");

        // 网络流恢复时尽量追到新帧，不接着播旧缓存
        if (m_formatContext && m_isNetworkVideo)
        {
            int64_t seekTarget = av_gettime();
            seekTarget = av_rescale_q(seekTarget, {1, AV_TIME_BASE}, m_formatContext->streams[m_videoStreamIndex]->time_base);

            if (av_seek_frame(m_formatContext, m_videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY) >= 0)
            {
                avcodec_flush_buffers(m_codecContext);
                if (m_audioCodecContext)
                    avcodec_flush_buffers(m_audioCodecContext);
                if (m_audioDevice)
                    SDL_PauseAudioDevice(m_audioDevice, 1);
                clearAudioQueue();
                m_audioClockUs = 0;
                m_audioStarted = false;
                m_firstVideoFrameSent = false;
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

void VideoDecode::setPlaybackSpeed(double speed)
{
    if (speed < 0.25)
        speed = 0.25;
    else if (speed > 4.0)
        speed = 4.0;

    QMutexLocker locker(&m_playStatusMutex);
    if (m_isNetworkVideo)
    {
        appendLog("VideoDecode","Playback speed is ignored for network streams.");
        return;
    }

    if (qFuzzyCompare(m_playbackSpeed, speed))
        return;

    // 切倍速只改时钟映射，不改当前媒体位置
    const int64_t now = av_gettime();
    const int64_t clockUs = m_isPlaying ? now : m_pauseTime;
    const int64_t currentMediaUs = static_cast<int64_t>((clockUs - m_startTime) * m_playbackSpeed);
    m_playbackSpeed = speed;
    m_startTime = clockUs - static_cast<int64_t>(currentMediaUs / m_playbackSpeed);
    m_audioClockUs = currentMediaUs;
    if (m_audioDevice)
    {
        SDL_PauseAudioDevice(m_audioDevice, 1);
        SDL_ClearQueuedAudio(m_audioDevice);
    }
    m_audioStarted = false;
    m_firstVideoFrameSent = false;
    appendLog("VideoDecode",QString("Playback speed set to %1x.").arg(m_playbackSpeed));
}

void VideoDecode::stopDecoding()
{
    QMutexLocker locker(&m_playStatusMutex);
    m_isPlaying = false;
    m_stopRequested = true;
    m_seekRequested = false;
    m_pendingSeekOffsetUs = 0;
    if (m_audioDevice)
        SDL_PauseAudioDevice(m_audioDevice, 1);
    clearAudioQueue();
    m_audioStarted = false;
    m_firstVideoFrameSent = false;
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
        m_audioStarted = false;
        m_firstVideoFrameSent = false;
    }

    if (m_isPlaying)
    {
        appendLog("VideoDecode","A video is already being played.");
    }

    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_audioFrame = av_frame_alloc();
    if (!m_packet || !m_frame || !m_audioFrame)
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

    // 音频起不来不拦视频，退回纯视频时钟
    initAudioDecoder();

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
             double playbackSpeed = 1.0;
             int64_t startTime = 0;
             {
                 QMutexLocker locker(&m_playStatusMutex);
                 playbackSpeed = m_playbackSpeed;
                 startTime = m_startTime;
             }
             elapsedUs = static_cast<int64_t>((av_gettime() - startTime) * playbackSpeed);

             packetTimeUs = 0;
             if (!m_isNetworkVideo && m_packet->dts != AV_NOPTS_VALUE)
                 packetTimeUs = av_rescale_q(m_packet->dts,timebase,avTimeBase);

             // 首帧先送出去，后面再跟主时钟
             const int64_t mediaClockUs = currentMediaClockUs(elapsedUs);
             if (m_firstVideoFrameSent && packetTimeUs > mediaClockUs)
                 av_usleep(static_cast<uint>((packetTimeUs - mediaClockUs) / playbackSpeed));

             emit videoTime(m_totalTime,mediaClockUs / (AV_TIME_BASE / 1000));

             if(avcodec_send_packet(m_codecContext, m_packet) == 0)
             {
                 while (avcodec_receive_frame(m_codecContext, m_frame) == 0)
                 {
                     bool dropLateFrame = false;
                     if (m_audioAvailable && m_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                     {
                         const int64_t frameUs = av_rescale_q(m_frame->best_effort_timestamp,
                                                              m_videoStream->time_base,
                                                              {1, AV_TIME_BASE});
                         const int64_t masterClockUs = currentAudioClockUs();
                         // 画面明显落后声音时丢帧追一下
                         dropLateFrame = masterClockUs >= 0 && frameUs + 100 * 1000 < masterClockUs;
                     }

                     if (dropLateFrame)
                     {
                         av_frame_unref(m_frame);
                         continue;
                     }

                     // 渲染器拿到自己的 AVFrame，切换时不会重复释放
                     AVFrame *frameToSend = av_frame_alloc();
                     if (frameToSend && av_frame_ref(frameToSend, m_frame) == 0)
                     {
                         emit videoPacket(frameToSend, m_size.width(), m_size.height());
                         m_firstVideoFrameSent = true;
                         startAudioIfReady();
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
        else if (m_packet->stream_index == m_audioStreamIndex)
        {
            // 音频包只进 SDL 队列，进度从队列大小反推
            decodeAudioPacket();
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

    double playbackSpeed = 1.0;
    int64_t startTime = 0;
    {
        QMutexLocker locker(&m_playStatusMutex);
        playbackSpeed = m_playbackSpeed;
        startTime = m_startTime;
    }

    int64_t targetUs = static_cast<int64_t>((av_gettime() - startTime) * playbackSpeed) + seekOffsetUs;
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

    // seek 后清掉解码缓存和 SDL 队列，别把旧声音带过去
    avcodec_flush_buffers(m_codecContext);
    if (m_audioCodecContext)
        avcodec_flush_buffers(m_audioCodecContext);
    if (m_audioDevice)
        SDL_PauseAudioDevice(m_audioDevice, 1);
    clearAudioQueue();
    {
        QMutexLocker locker(&m_playStatusMutex);
        m_startTime = av_gettime() - static_cast<int64_t>(targetUs / m_playbackSpeed);
        m_audioClockUs = targetUs;
        m_audioStarted = false;
        m_firstVideoFrameSent = false;
    }
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
