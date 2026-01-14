#include <gst/gst.h>
#include <glib.h>
#include <iostream>
#include <cstring>
#include <signal.h>
#include <unistd.h>
#include <ctime>  // 用于时间格式化

// 系统检测宏定义
#if defined(_WIN32) || defined(_WIN64)
#define OS_WINDOWS 1
#define OS_NAME "Windows"
#elif defined(__linux__)
#define OS_LINUX 1
#define OS_NAME "Linux"
#elif defined(__APPLE__) && defined(__MACH__)
#define OS_MACOS 1
#define OS_NAME "macOS"
#else
#define OS_UNKNOWN 1
#define OS_NAME "Unknown"
#endif

using namespace std;
static GMainLoop *main_loop = NULL;
static GstElement *pipeline = NULL;
static GstElement *textoverlay = NULL;  // 新增：保存文字叠加元素的引用

// 信号处理函数
static void signal_handler(int signum) {
    g_print("\nreceive  %d，stoping...\n", signum);
    if (main_loop) {
        g_main_loop_quit(main_loop);
    }
}

// 新增：更新时间戳文本的回调函数
static gboolean update_timestamp(gpointer data) {
    if (!textoverlay) return TRUE;

    // 获取当前时间并格式化
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    // 设置textoverlay的显示文本
    g_object_set(G_OBJECT(textoverlay), "text", time_str, NULL);

    return TRUE;  // 返回TRUE保持定时器运行
}

// 错误处理回调
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS: {
            g_print("push end\n");
            g_main_loop_quit(main_loop);
            break;
        }

        case GST_MESSAGE_ERROR: {
            gchar *debug_info = NULL;
            GError *err = NULL;

            gst_message_parse_error(msg, &err, &debug_info);

            g_printerr("error info %s: %s\n",
                      GST_OBJECT_NAME(msg->src), err->message);
            g_printerr("debug info %s\n", debug_info ? debug_info : "无");

            g_error_free(err);
            g_free(debug_info);

            g_main_loop_quit(main_loop);
            break;
        }

        case GST_MESSAGE_WARNING: {
            gchar *debug_info = NULL;
            GError *err = NULL;

            gst_message_parse_warning(msg, &err, &debug_info);

            g_print("alarm :%s: %s\n",
                    GST_OBJECT_NAME(msg->src), err->message);

            g_error_free(err);
            g_free(debug_info);
            break;
        }

        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

            // 只打印管道的状态变化
            if (GST_ELEMENT(GST_MESSAGE_SRC(msg)) == pipeline) {
                g_print("pipe state change: %s -> %s\n",
                       gst_element_state_get_name(old_state),
                       gst_element_state_get_name(new_state));
            }
            break;
        }

        case GST_MESSAGE_STREAM_STATUS: {
            GstStreamStatusType type;
            GstElement *owner;
            gst_message_parse_stream_status(msg, &type, &owner);
            g_print("flow state: %d\n", type);
            break;
        }

        default:
            break;
    }

    return TRUE;
}

// 创建推流管道
static GstElement* create_streaming_pipeline(const gchar *rtmp_url,
                                             gint video_width,
                                             gint video_height,
                                             gint video_bitrate,
                                             gint audio_bitrate) {
    GstElement *pipeline;
    GstElement *videosrc, *videoconvert, *videoscale, *capsfilter, *videoencoder, *h264parse;
    GstElement *audiosrc, *audioconvert, *audioresample, *audioencoder, *aacparse;
    GstElement *flvmux, *rtmpsink;
    GstCaps *caps;
    gchar caps_str[256];

    // 创建管道
    pipeline = gst_pipeline_new("camera-streamer-pipeline");
    if (!pipeline) {
        g_printerr("无法创建管道\n");
        return NULL;
    }

    // ========== 优化1: 设置管道的实时调度策略 ==========
    // g_object_set(G_OBJECT(pipeline), "latency", 0, NULL);  // 强制零延迟
    gst_element_set_start_time(pipeline, GST_CLOCK_TIME_NONE);

    // 创建视频元素
    videosrc = gst_element_factory_make("autovideosrc", "video-source");
    videoconvert = gst_element_factory_make("videoconvert", "video-convert");
    videoscale = gst_element_factory_make("videoscale", "video-scale");
    capsfilter = gst_element_factory_make("capsfilter", "video-caps");

    // 新增：创建文字叠加元素
    textoverlay = gst_element_factory_make("textoverlay", "timestamp-overlay");

    videoencoder = gst_element_factory_make("x264enc", "video-encoder");
    h264parse = gst_element_factory_make("h264parse", "h264-parser");

    // 创建音频元素
    audiosrc = gst_element_factory_make("autoaudiosrc", "audio-source");
    audioconvert = gst_element_factory_make("audioconvert", "audio-convert");
    audioresample = gst_element_factory_make("audioresample", "audio-resample");
    audioencoder = gst_element_factory_make("avenc_aac", "audio-encoder");
    aacparse = gst_element_factory_make("aacparse", "aac-parser");

    // 创建混合和输出元素
    flvmux = gst_element_factory_make("flvmux", "flv-mux");
    rtmpsink = gst_element_factory_make("rtmpsink", "rtmp-sink");

    // 检查所有元素是否成功创建（包含新增的textoverlay）
    if (!videosrc || !videoconvert || !videoscale || !capsfilter || !textoverlay ||
        !videoencoder || !h264parse || !audiosrc || !audioconvert ||
        !audioresample || !audioencoder || !aacparse || !flvmux || !rtmpsink) {
        g_printerr("cant create GStreamer element\n");
        return NULL;
    }

    // ========== 优化2: 优化视频源参数，减少采集延迟 ==========
    g_object_set(G_OBJECT(videosrc),
                 "do-timestamp", TRUE,  // 立即打时间戳
                 "latency", 0,          // 零延迟采集
                 NULL);

    // 设置视频caps
    snprintf(caps_str, sizeof(caps_str),
             "video/x-raw,width=%d,height=%d,framerate=30/1,format=I420",
             video_width, video_height);
    caps = gst_caps_from_string(caps_str);
    g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
    gst_caps_unref(caps);

    // 新增：配置textoverlay参数（右上角时间戳）
    // 修正：配置textoverlay参数（右上角时间戳）
    // 新增：配置textoverlay参数（右上角时间戳）
    g_object_set(G_OBJECT(textoverlay),
                 // "alignment", 2,          // 0=左对齐, 1=居中, 2=右对齐
                 "valignment", 0,         // 0=顶部对齐, 1=居中, 2=底部对齐
                 "xpad", 15,              // 右边距15像素（减小边距）
                 "ypad", 10,              // 上边距10像素（减小边距）
                 "font-desc", "Sans Bold 20", // 使用更清晰的字体
                 "color", 0xFFFFFFFF,     // 白色文字，不透明度FF
                 "shaded-background", TRUE,    // 启用阴影背景
                 // "shadow-color", 0x00000080,   // 半透明黑色阴影
                 "shadow-xoffset", 2,     // 阴影X偏移
                 "shadow-yoffset", 2,     // 阴影Y偏移
                 "halignment", 2,         // 水平对齐（右对齐）
                 "valignment", 0,         // 垂直对齐（顶部对齐）
                 "deltax", 0,             // X方向偏移
                 "deltay", 0,             // Y方向偏移
                 "text", "Initializing...", // 初始文本
                 NULL);

    // 在设置textoverlay后，添加调试信息
    gint xpad_val, ypad_val;
    g_object_get(G_OBJECT(textoverlay), "xpad", &xpad_val, "ypad", &ypad_val, NULL);
    g_print("TextOverlay位置 - xpad: %d, ypad: %d\n", xpad_val, ypad_val);

    // ========== 优化3: 优化x264编码器参数，极致降低延迟 ==========
    g_object_set(G_OBJECT(videoencoder),
                 "bitrate", video_bitrate,
                 "tune", 0x00000004,        // zerolatency (零延迟)
                 "speed-preset", 0,         // ultrafast (比原来的1更快)
                 // "profile", 1,              // baseline (基础配置，解码更快)
                 "key-int-max", 30,         // 关键帧间隔1秒(30帧)，减少等待关键帧时间
                 "bframes", 0,              // 禁用B帧，B帧会增加延迟
                 "byte-stream", TRUE,       // 字节流模式
                 "threads", 4,              // 多线程编码
                 // "sync", FALSE,             // 不同步，立即输出
                 "crf", 28,
                 NULL);

    // ========== 优化4: 优化音频编码器，减少音频延迟 ==========
    g_object_set(G_OBJECT(audioencoder),
                 "bitrate", audio_bitrate,
                 // "low-latency", TRUE,      // 低延迟模式
                 "threads", 2,             // 多线程
                 NULL);

    // ========== 优化5: 优化FLV muxer，立即输出 ==========
    g_object_set(G_OBJECT(flvmux),
                 "streamable", TRUE,
                 "sync", FALSE,            // 不同步音频视频，立即输出
                 "max-delay", 0,           // 最大延迟0ms
                 "min-index-interval", 1,  // 最小索引间隔
                 NULL);

    // ========== 优化6: 优化RTMP sink，减少缓冲 ==========
    g_object_set(G_OBJECT(rtmpsink),
                 "location", rtmp_url,
                 "sync", FALSE,            // 不同步
                 "async", FALSE,           // 非异步
                 "buffer-mode", 0,         // 无缓冲模式
                 "max-lateness", 0,        // 最大延迟0ms
                 "timeout", 1000,          // 超时时间1秒
                 "chunk-size", 4096,
                 NULL);

    // 将所有元素添加到管道（包含新增的textoverlay）
    gst_bin_add_many(GST_BIN(pipeline),
                     videosrc, videoconvert, videoscale, capsfilter, textoverlay,
                     videoencoder, h264parse,
                     audiosrc, audioconvert, audioresample, audioencoder, aacparse,
                     flvmux, rtmpsink, NULL);

    // 链接视频元素（新增：在capsfilter后链接textoverlay，再链接encoder）
    if (!gst_element_link_many(videosrc, videoconvert, videoscale, capsfilter,
                               textoverlay, videoencoder, h264parse, NULL)) {
        g_printerr("无法链接视频元素\n");
        return NULL;
    }

    // 链接音频元素
    if (!gst_element_link_many(audiosrc, audioconvert, audioresample,
                               audioencoder, aacparse, NULL)) {
        g_printerr("无法链接音频元素\n");
        return NULL;
    }

    // 将视频和音频连接到flvmux
    GstPad *video_pad = gst_element_get_static_pad(h264parse, "src");
    GstPad *audio_pad = gst_element_get_static_pad(aacparse, "src");
    GstPad *flvmux_video_pad = gst_element_get_request_pad(flvmux, "video");
    GstPad *flvmux_audio_pad = gst_element_get_request_pad(flvmux, "audio");

    if (gst_pad_link(video_pad, flvmux_video_pad) != GST_PAD_LINK_OK) {
        g_printerr("无法链接视频到flvmux\n");
        gst_object_unref(video_pad);
        gst_object_unref(flvmux_video_pad);
        return NULL;
    }

    if (gst_pad_link(audio_pad, flvmux_audio_pad) != GST_PAD_LINK_OK) {
        g_printerr("无法链接音频到flvmux\n");
        gst_object_unref(audio_pad);
        gst_object_unref(flvmux_audio_pad);
        return NULL;
    }

    // 链接flvmux到rtmpsink
    if (!gst_element_link(flvmux, rtmpsink)) {
        g_printerr("无法链接flvmux到rtmpsink\n");
        return NULL;
    }

    // 释放pad引用
    gst_object_unref(video_pad);
    gst_object_unref(audio_pad);
    gst_object_unref(flvmux_video_pad);
    gst_object_unref(flvmux_audio_pad);

    return pipeline;
}

int main(int argc, char *argv[]) {

    cout<<gst_version_string()<<endl;
    GstBus *bus;
    guint bus_watch_id;
    guint timeout_id;  // 新增：定时器ID

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化GStreamer
    gst_init(&argc, &argv);

    // 创建主循环
    main_loop = g_main_loop_new(NULL, FALSE);

    // 创建推流管道
    pipeline = create_streaming_pipeline(
        "rtmp://81.70.252.95:1935/live/livestream",
        640, 480,  // 视频分辨率
        1000,      // 视频比特率 (kbps)
        128        // 音频比特率 (kbps)
    );

    if (!pipeline) {
        g_printerr("无法创建推流管道\n");
        return -1;
    }

    // 新增：创建定时器，每秒更新一次时间戳文本
    timeout_id = g_timeout_add(1000, update_timestamp, NULL);  // 1000ms = 1秒

    // 设置消息总线监听
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, main_loop);
    gst_object_unref(bus);

    // ========== 优化7: 立即启动管道，不等待 ==========
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("无法启动推流管道\n");
        gst_object_unref(pipeline);
        return -1;
    }

    std::cout << "开始摄像头推流..." << std::endl;
    std::cout << "目标: rtmp://81.70.252.95:1935/live/livestream" << std::endl;
    std::cout << "分辨率: 640x480" << std::endl;
    std::cout << "按Ctrl+C停止推流" << std::endl;

    // 运行主循环
    g_main_loop_run(main_loop);

    // 清理资源
    std::cout << "stop push stream..." << std::endl;

    // 新增：移除定时器
    g_source_remove(timeout_id);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_source_remove(bus_watch_id);
    g_main_loop_unref(main_loop);

    std::cout << "push stopped" << std::endl;

    return 0;
}