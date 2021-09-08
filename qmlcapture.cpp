#include "qmlcapture.hpp"
#include <gst/app/gstappsrc.h>
#include <QDateTime>
#include <QDebug>
#include <QQuickItemGrabResult>
#include <QBuffer>
#include <QFile>

#define DBG_BLOCK 0

// anonymous namespace
namespace
{
    const int FRAME_WD = 400; // TODO should be a variable
    const int FRAME_HT = 300; // TODO should be a variable
}

// static initialization
bool QmlCapture::sAppSrcNeedsData = true;
QmlCapture *QmlCapture::theCaptureClass = NULL;

/**
 * @brief QmlCapture::QmlCapture - ctor
 */
QmlCapture::QmlCapture()
: mpPipeline     (NULL)
, mpQmlSrc       (NULL)
, mpQuickItem    (NULL)
, mpCaptureTimer (new QTimer())
{
    // assign static this
    theCaptureClass = this;

    // setup timer
    mpCaptureTimer->setInterval(10);
    QObject::connect( mpCaptureTimer, SIGNAL(timeout()), this, SLOT(onCaptureTimerExp()) );
}

/**
 * @brief QmlCapture::~QmlCapture - dtor
 */
QmlCapture::~QmlCapture()
{
    // TODO delete the gst objects
    delete mpCaptureTimer;
}

/**
 * @brief QmlCapture::captureImage - capture the qml object to an image at the given path
 * @param pObj - pointer to qml object to capture
 * @param path - path to save the image
 */
void QmlCapture::captureImage(QObject *pObj, QString path)
{
    // TODO implement
    Q_UNUSED(pObj);
    Q_UNUSED(path);
}

/**
 * @brief QmlCapture::startVideo - start to capture the qml object to a video at the given path
 * @param pObj - pointer to qml object to capture
 * @param path - path to save the image
 */
void QmlCapture::startVideo(QObject *pObj, QString path)
{
    // TODO implement
    Q_UNUSED(pObj);
    Q_UNUSED(path);
}

/**
 * @brief QmlCapture::stopVideo - stop video recording in process
 */
void QmlCapture::stopVideo()
{
    // TODO implement
}

/**
 * @brief QmlCapture::toVideoSink - output the qml to a video sink for debuggin/demonstration purposes
 * @param pObj - pointer to qml object to capture
 */
void QmlCapture::toVideoSink(QObject *pObj)
{
    // setup the quick item
    mpQuickItem = qobject_cast<QQuickItem*>(pObj);

    // launch the sink pipeline
    launchVideoSinkPipeline();

    // start the capture timer
    mpCaptureTimer->start();
}

/**
 * @brief QmlCapture::toTee - capture the qml to a gstreamer tee, so that it can be used by a gstreamer pipeline
 * @param pObj - pointer to qml object to capture
 */
void QmlCapture::toTee(QObject *pObj)
{
    // TODO implement
    Q_UNUSED(pObj);
}

/**
 * @brief QmlCapture::onCaptureTimerExp - slot callback timer expiration to capture a new frame
 */
void QmlCapture::onCaptureTimerExp()
{
#if DBG_BLOCK
    qDebug() << "appsrc needs data " << sAppSrcNeedsData;
#endif

    if (sAppSrcNeedsData)
    {
        captureFrame();
    }
    else
    {
        // dont do anything
    }
}

/**
 * @brief QmlCapture::pushFrame - push a frame into the appsrc
 * @param buf - buffer
 * @param size - buffer size
 */
void QmlCapture::pushFrame(char *buf, guint size)
{
    pushFrame((gpointer)(buf), size);
}

/**
 * @brief QmlCapture::pushFrame - overloaded function to push a frame into the appsrc
 * @param buf - qbytearray frame
 */
void QmlCapture::pushFrame(QByteArray &buf)
{
    pushFrame((gpointer)(buf.data()), buf.size());
}

/**
 * @brief QmlCapture::pushFrame - overloaded function to push a frame into the appsrc
 * @param buf - gpointer buffer
 * @param size - buffer size
 */
void QmlCapture::pushFrame(gpointer buf, guint size)
{
    GstFlowReturn ret = GST_FLOW_OK;
    static GstClockTime timestamp = 0;
    GstBuffer *buffer = NULL;

#if DBG_BLOCK
    qDebug() << "pushing frame of size " << size;
#endif

    // print the timestamp
    printTimeStamp();

    // make a buffer
    buffer = gst_buffer_new_wrapped_full( (GstMemoryFlags)0, // flags
                                          buf,               // data
                                          size,              // maxsize
                                          0,                 // offset
                                          size,              // size
                                          NULL,              // user_data
                                          NULL );            // notify

    // add the timestamp
    GST_BUFFER_PTS (buffer) = timestamp;
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 15); // (val,num,den)
    timestamp += GST_BUFFER_DURATION (buffer);

    // push the buffer
    ret = gst_app_src_push_buffer( GST_APP_SRC(mpQmlSrc), buffer );

    if (ret != GST_FLOW_OK)
    {
        qCritical() << "push error " << ret;
    }
}

/**
 * @brief QmlCapture::launchVideoSinkPipeline - launch a videosink pipeline
 *
 * gst-launch-1.0 \
 *     appsrc name=qmlsrc stream-type=0 is-live=true ! \
 *            caps="image/jpeg,width=400,height=300,framerate=0/1" \
 *     jpegdec ! \
 *     clockoverlay ! \
 *     videoconvert ! \
 *     xvimagesink
 *
 */
void QmlCapture::launchVideoSinkPipeline()
{
    mpPipeline               = gst_pipeline_new        ("pipeline"             );
    mpQmlSrc                 = gst_element_factory_make("appsrc",      "qmlsrc");
    GstElement *jpegdec      = gst_element_factory_make("jpegdec",      NULL   );
    GstElement *clockoverlay = gst_element_factory_make("clockoverlay", NULL   );
    GstElement *vidconvert   = gst_element_factory_make("videoconvert", NULL   );
    GstElement *xvsink       = gst_element_factory_make("xvimagesink",  NULL   );

    // setup the caps
    GstCaps *caps = gst_caps_new_simple("image/jpeg",
                                        "width",  G_TYPE_INT, FRAME_WD,
                                        "height", G_TYPE_INT, FRAME_HT,
                                        "framerate", GST_TYPE_FRACTION, 0, 1,
                                        NULL);
    g_object_set(G_OBJECT(mpQmlSrc), "caps", caps, NULL);

    // add elements into the pipeline
    gst_bin_add_many(GST_BIN(mpPipeline), mpQmlSrc, jpegdec, clockoverlay, vidconvert, xvsink, NULL);

    // link the elements
    gst_element_link_many(mpQmlSrc, jpegdec, clockoverlay, vidconvert, xvsink, NULL);

    // setup the appsrc props
    g_object_set (G_OBJECT (mpQmlSrc),
                  "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                  "format", GST_FORMAT_TIME,
                  "is-live", TRUE,
                  NULL);

    // set the appsrc callback for need data
    g_signal_connect (mpQmlSrc, "need-data", G_CALLBACK (sAppsrcNeedsData), NULL);

    qDebug() << "play the pipeline";
    gst_element_set_state (mpPipeline, GST_STATE_PLAYING);
}

/**
 * @brief QmlCapture::sAppsrcNeedsData - static callback for when the appsrc needs more data
 * @param appsrc - appsrc gst element
 * @param unused_size - unused data size
 * @param user_data - user data
 */
void QmlCapture::sAppsrcNeedsData(GstElement *appsrc, guint unused_size, gpointer user_data)
{
    Q_UNUSED(appsrc);
    Q_UNUSED(unused_size);
    Q_UNUSED(user_data);

    // set a flag that the appsrc is ready
    sAppSrcNeedsData = true;
}

/**
 * @brief QmlCapture::printTimeStamp - print timestamp between frames
 */
void QmlCapture::printTimeStamp()
{
#if DBG_BLOCK
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
#endif
}

/**
 * @brief captureFrame - capture a frame from the qml
 */
void QmlCapture::captureFrame()
{
    sAppSrcNeedsData = false;
    QSharedPointer<const QQuickItemGrabResult> grabres = mpQuickItem->grabToImage();

    QObject::connect(grabres.data(), &QQuickItemGrabResult::ready, [=]()
    {
        // save the image into a byte array using a qbuffer
        QImage qmlImg = grabres->image();
#if DBG_BLOCK
        qDebug() << "qml::ready - imgsz = " << qmlImg.byteCount();
#endif
        QByteArray ba;
        QBuffer buf(&ba);
        buf.open(QIODevice::WriteOnly);
        qmlImg.save(&buf, "jpeg"); // TODO make this configurable variable
        buf.close();
        pushFrame(ba);
    });
}

/**
 * @brief saveToFile - debug function to save the given buffer to the file system
 */
void QmlCapture::saveToFile(QByteArray buf, QString filename)
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










