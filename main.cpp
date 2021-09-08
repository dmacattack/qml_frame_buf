#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QFile>
#include <QDebug>
#include <QQuickWindow>
#include "grabwindow.hpp"
#include <QImage>
#include <QScreen>
#include <QPixmap>
#include <QtQuickWidgets/QQuickWidget>
#include <QQuickItemGrabResult>
#include <QTimer>
#include <QDateTime>
#include <gst/gst.h>
#include <QThread>
#include <QBuffer>
#include <gst/app/gstappsrc.h>
#include "qmlcapture.hpp"


#define USE_RAW_BUF 0
#define IMG_BUF 1

// member vars
GstElement *mpPipeline  = NULL; // gst pipeline
GstElement *pQmlSrc     = NULL; // appsrc
bool mbIsWhiteBlock     = false;
uint16_t mWhiteBlock[400*300];
uint16_t mBlackBlock[400*300];
const QString mImages[] = // list of images from filesystem
{
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_0.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_1.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_2.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_3.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_4.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_5.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_6.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_7.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_8.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_9.jpg",
    "/home/insp-dev2/workspace/qml_frame_buf/imgs/frame_10.jpg",
};

// function prototypes
gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);
void saveToFile(QByteArray buf, QString filename);
void launchpipeline();
void pushFrame(GstAppSrc* appsrc);
void printTimeStamp();
QByteArray readImage();
qint64 readImageBuf(char *data);
void cb_need_data (GstElement *appsrc, guint unused_size, gpointer user_data);

/**
 * @brief launchpipeline - launch a gst pipeline
 */
void launchpipeline()
{
    /*
     * run this pipeline. For some reason you cant seem to use gst_parse_launch
     *
     * gst-launch-1.0 \
     *     appsrc name=qmlsrc stream-type=0 is-live=true ! \
     *     pngdec ! \
     *     clockoverlay ! \
     *     videoconvert ! \
     *     xvimagesink
     */
    mpPipeline               = gst_pipeline_new("pipeline");
    pQmlSrc                  = gst_element_factory_make("appsrc",       "qmlsrc");
#if !USE_RAW_BUF
    GstElement *jpegdec      = gst_element_factory_make("jpegdec",      NULL);
    GstElement *clockoverlay = gst_element_factory_make("clockoverlay", NULL);
#endif
    GstElement *vidconvert   = gst_element_factory_make("videoconvert", NULL);
    GstElement *xvsink       = gst_element_factory_make("xvimagesink",  NULL);

    // setup the caps
#if USE_RAW_BUF
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "width",  G_TYPE_INT, 400,
                                        "height", G_TYPE_INT, 300,
                                        "format", G_TYPE_STRING, "RGB16",
                                        "framerate", GST_TYPE_FRACTION, 0, 1,
                                        NULL);
#else
    GstCaps *caps = gst_caps_new_simple("image/jpeg",
                                        "width",  G_TYPE_INT, 400,
                                        "height", G_TYPE_INT, 300,
                                        "framerate", GST_TYPE_FRACTION, 0, 1,
                                        NULL);
#endif
    g_object_set(G_OBJECT(pQmlSrc), "caps", caps, NULL);

    // add elements into the pipeline
#if USE_RAW_BUF
    gst_bin_add_many(GST_BIN(mpPipeline), pQmlSrc, vidconvert, xvsink, NULL);
#else
    gst_bin_add_many(GST_BIN(mpPipeline), pQmlSrc, jpegdec, clockoverlay, vidconvert, xvsink, NULL);
#endif

    // link the elements
#if USE_RAW_BUF
    gst_element_link_many(pQmlSrc, vidconvert, xvsink, NULL);
#else
    gst_element_link_many(pQmlSrc, jpegdec, clockoverlay, vidconvert, xvsink, NULL);
#endif

    // setup the appsrc props
    g_object_set (G_OBJECT (pQmlSrc),
                  "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                  "format", GST_FORMAT_TIME,
                  "is-live", TRUE,
                  NULL);

    // set the appsrc callback for need data
    g_signal_connect (pQmlSrc, "need-data", G_CALLBACK (cb_need_data), NULL);

    qWarning() << "play the pipeline";
    gst_element_set_state (mpPipeline, GST_STATE_PLAYING);
}

/**
 * @brief pushFrame - push a frame into the gst pipeline
 */
void pushFrame(GstAppSrc* appsrc)
{
    GstFlowReturn ret = GST_FLOW_OK;
    static GstClockTime timestamp = 0;
    GstBuffer *buffer;
    guint size = 0;

    // print the timestamp
    printTimeStamp();

    // get a frame
#if USE_RAW_BUF
    gpointer pData = (gpointer)(mbIsWhiteBlock ? mWhiteBlock : mBlackBlock);
    mbIsWhiteBlock = !mbIsWhiteBlock;
    size = 300*400*2;
#else
#if IMG_BUF
    // read the image without qt complex types
    char* data = new char[4096];
    size = readImageBuf(data);
    gpointer pData = (gpointer)(data);

#else
    // read the image into a qbytearray
    QByteArray img = readImage();
    size = img.size();
    gpointer pData = (gpointer)(img.data());

#endif

#endif

    qWarning() << "pushing buffer size " << size;

    // make a buffer
    buffer = gst_buffer_new_wrapped_full( (GstMemoryFlags)0,
                                          pData,
                                          size,
                                          0,
                                          size,
                                          NULL,
                                          NULL );

    // add the timestamp (250ms)
    GST_BUFFER_PTS (buffer) = timestamp;
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 10); // (val,num,den)
    timestamp += GST_BUFFER_DURATION (buffer);

    // push the buffer
    ret = gst_app_src_push_buffer(appsrc, buffer);

    if (ret != GST_FLOW_OK)
    {
        qCritical() << "push error " << ret;
    }
}


int main(int argc, char *argv[])
{
    // init gstreamer
    gst_init(&argc, &argv);

    // init qml application
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
    {
        qCritical("root object is null/empty");
        return -1;
    }

    // get the root qml object
    QObject *pRoot = engine.rootObjects().first();

    // get the qml object to record
    QObject *pFrame = pRoot->findChild<QObject*>("frameboy");

    // create the Qml Capture
    QmlCapture *pCapture = new QmlCapture();
    pCapture->toUdpSink(pFrame);

    return app.exec();
}


/**
 * @brief bus_call - gstreamer bus callback
 */
gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
    bus = bus;
    GMainLoop *loop = (GMainLoop*)data;

    switch (GST_MESSAGE_TYPE (msg))
    {
    case GST_MESSAGE_EOS:
        qDebug() << "End of stream";
        g_main_loop_quit (loop);
        break;
    case GST_MESSAGE_ERROR:
    {
        gchar *debug = NULL;
        GError *err = NULL;

        gst_message_parse_error (msg, &err, &debug);

        qDebug("Error: %s", err->message);
        g_error_free (err);

        if (debug)
        {
            qDebug("Debug details: %s ", debug);
            g_free (debug);
        }

        g_main_loop_quit (loop);
        break;
    }
    default:
        break;
    }

    return TRUE;
}

/**
 * @brief saveToFile - save the given buffer to the file system
 */
void saveToFile(QByteArray buf, QString filename)
{
    QFile f(filename);

    if (f.open(QIODevice::WriteOnly))
    {
        qDebug() << "write buff to file";
        f.write(buf);
        f.close();
    }
    else
    {
        qWarning() << "couldnt open the file";
    }
}


/**
 * @brief printTimeStamp - print how long its been in ms
 */
void printTimeStamp()
{
    static qint64 dt = 0;

    if (dt == 0)
    {
        dt = QDateTime::currentMSecsSinceEpoch();
    }
    else
    {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 ts = now - dt;
        qDebug() << "timestamp = " << ts;
        dt = now;
    }
}

/**
 * @brief readImage - read an image from the filesystem
 * @return
 */
QByteArray readImage()
{
    static int cnt = 0;

    if (cnt > 10)
    {
        qFatal("stop");
    }

    QString fil = mImages[cnt++];
    qWarning() << "reading " << fil;
    QFile f(fil);
    f.open(QIODevice::ReadOnly);
    QByteArray frame = f.readAll();
    return frame;
}

/**
 * @brief readImageBuf - read the image into the given buffer
 */
qint64 readImageBuf(char *data)
{
    static int cnt = 0;

    if (cnt > 10)
    {
        qFatal("stop");
    }

    QString fil = mImages[cnt++];
    QFile f(fil);
    f.open(QIODevice::ReadOnly);
    qint64 len = f.read(data, 4096);

    qWarning() << "read " << fil << "len = " << len;

    return len;
}

/**
 * @brief cb_need_data - need data callback - update the want flag
 */
void cb_need_data (GstElement *appsrc, guint unused_size, gpointer user_data)
{
    Q_UNUSED(appsrc);
    Q_UNUSED(unused_size);
    Q_UNUSED(user_data);
  //prepare_buffer((GstAppSrc*)appsrc);

    qDebug() << "need data";
    pushFrame((GstAppSrc*)pQmlSrc);
}
