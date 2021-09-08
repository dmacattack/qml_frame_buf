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
    const int UDP_PORT = 5007;
}

// static initialization
CAPTURE::eFRAME_STATUS QmlCapture::sFrameStatus = CAPTURE::eSTATUS_IDLE;
QmlCapture *QmlCapture::theCaptureClass = NULL;

/**
 * @brief QmlCapture::QmlCapture - ctor
 */
QmlCapture::QmlCapture()
: mpPipeline     (NULL)
, mpQmlSrc       (NULL)
, mpQuickItem    (NULL)
, mpCaptureTimer (new QTimer())
, mFrameWidth    (0)
, mFrameHeight   (0)
, mGrabRes       ()
, mFrame         ()
{
    // assign static this
    theCaptureClass = this;

    // setup timer
    mpCaptureTimer->setInterval(66); // this determines approx framerate
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
    // set the qml object
    setQuickItem(pObj);

    // grab the qml directly into an image
    mGrabRes = mpQuickItem->grabToImage();
    QObject::connect(mGrabRes.data(), &QQuickItemGrabResult::ready, [&, path]()
    {
        // save the image into a byte array using a qbuffer
        QImage qmlImg = mGrabRes->image();
        QByteArray ba;
        QBuffer buf(&ba);
        buf.open(QIODevice::WriteOnly);
        qmlImg.save(&buf, "jpeg", 85);
        buf.close();

        // save the frame to a file
        saveToFile(ba, path);
    });
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
    // set the qml object
    setQuickItem(pObj);

    // launch the pipeline
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
    // set the qml object
    setQuickItem(pObj);

    // launch the pipeline
    launchVideoTeePipeline();

    // start the capture timer
    mpCaptureTimer->start();
}

/**
 * @brief QmlCapture::toUdpSink - capture the qml to a udpsink, so that it can be picked up later
 * @param pObj - pointer to qml object to capture
 */
void QmlCapture::toUdpSink(QObject *pObj)
{
    // set the qml object
    setQuickItem(pObj);

    // launch the sink pipeline
    launchUdpSinkPipeline();

    // start the capture timer
    mpCaptureTimer->start();
}

/**
 * @brief QmlCapture::onCaptureTimerExp - slot callback timer expiration to capture a new frame
 */
void QmlCapture::onCaptureTimerExp()
{
#if DBG_BLOCK
    qDebug() << "appsrc needs data " << sAppSrcNeedsData;
#endif

    // just capture the frame
    if (sFrameStatus == CAPTURE::eSTATUS_IDLE)
    {
        captureFrame();
    }
}

/**
 * @brief QmlCapture::setQuickItem - set the qml quick item
 * @param pObj - pointer to qml object to capture
 */
void QmlCapture::setQuickItem(QObject *pObj)
{
    // set the dimensions
    mFrameWidth = pObj->property("width").toInt();
    mFrameHeight = pObj->property("height").toInt();

    qDebug() << "object wxh = " << mFrameWidth << "x" << mFrameHeight;
    // setup the quick item
    mpQuickItem = qobject_cast<QQuickItem*>(pObj);
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
void QmlCapture::pushFrame()
{
    pushFrame((gpointer)(mFrame.data()), mFrame.size());
}

/**
 * @brief QmlCapture::pushFrame - overloaded function to push a frame into the appsrc
 * @param buf - gpointer buffer
 * @param size - buffer size
 */
void QmlCapture::pushFrame(gpointer buf, guint size)
{
    GstFlowReturn ret = GST_FLOW_OK;
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

    // push the buffer
    ret = gst_app_src_push_buffer( GST_APP_SRC(mpQmlSrc), buffer );

    if (ret != GST_FLOW_OK)
    {
        qCritical() << "push error " << ret;
    }

    sFrameStatus = CAPTURE::eSTATUS_IDLE;
}

/**
 * @brief QmlCapture::launchVideoSinkPipeline - launch a videosink pipeline
 *
 * gst-launch-1.0 \
 *     appsrc name=qmlsrc stream-type=0 is-live=true ! \
 *            caps="image/jpeg,width=400,height=300,framerate=0/1" \
 *     jpegdec ! \
 *     videorate ! \
 *     video/x-raw, framerate=15/1 ! \
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
    GstElement *videorate    = gst_element_factory_make("videorate",    NULL   );
    GstElement *vrcaps       = gst_element_factory_make("capsfilter",   NULL   );
    GstElement *clockoverlay = gst_element_factory_make("clockoverlay", NULL   );
    GstElement *vidconvert   = gst_element_factory_make("videoconvert", NULL   );
    GstElement *xvsink       = gst_element_factory_make("xvimagesink",  NULL   );

    // setup the caps
    {
        GstCaps *caps = gst_caps_new_simple("image/jpeg",
                                            "width",  G_TYPE_INT, mFrameWidth,
                                            "height", G_TYPE_INT, mFrameHeight,
                                            "framerate", GST_TYPE_FRACTION, 0, 1,
                                            NULL);
        g_object_set(G_OBJECT(mpQmlSrc), "caps", caps, NULL);
        gst_caps_unref(caps);
    }

    {
        GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                            "framerate", GST_TYPE_FRACTION, 15, 1,
                                            NULL);
        g_object_set(G_OBJECT(vrcaps), "caps", caps, NULL);
        gst_caps_unref(caps);
    }

    // add elements into the pipeline
    gst_bin_add_many(GST_BIN(mpPipeline), mpQmlSrc, jpegdec, videorate, vrcaps, clockoverlay, vidconvert, xvsink, NULL);

    // link the elements
    gst_element_link_many(mpQmlSrc, jpegdec, videorate, vrcaps, clockoverlay, vidconvert, xvsink, NULL);

    // setup the appsrc props
    g_object_set (G_OBJECT (mpQmlSrc),
                  "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                  "format", GST_FORMAT_TIME,
                  "is-live", TRUE,
                  NULL);

    qDebug() << "play the pipeline";
    gst_element_set_state (mpPipeline, GST_STATE_PLAYING);
}

/**
 * @brief QmlCapture::launchVideoTeePipeline
 */
void QmlCapture::launchVideoTeePipeline()
{
    // TODO implement
}

/**
 * @brief QmlCapture::launchUdpSinkPipeline
 *
 * gst-launch-1.0 \
 *     appsrc name=qmlsrc stream-type=0 is-live=true ! \
 *            caps="image/jpeg,width=400,height=300,framerate=0/1" \
 *     rtpjpegpay ! \
 *     udpsink port=<port>
 */
void QmlCapture::launchUdpSinkPipeline()
{
    mpPipeline               = gst_pipeline_new        ("pipeline"             );
    mpQmlSrc                 = gst_element_factory_make("appsrc",      "qmlsrc");
    GstElement *rtpjpegpay   = gst_element_factory_make("rtpjpegpay",   NULL   );
    GstElement *udpsink      = gst_element_factory_make("udpsink",      NULL   );

    // setup the caps
    {
        GstCaps *caps = gst_caps_new_simple("image/jpeg",
                                            "width",  G_TYPE_INT, mFrameWidth,
                                            "height", G_TYPE_INT, mFrameHeight,
                                            "framerate", GST_TYPE_FRACTION, 0, 1,
                                            NULL);
        g_object_set(G_OBJECT(mpQmlSrc), "caps", caps, NULL);
        gst_caps_unref(caps);
    }

    g_object_set(G_OBJECT(udpsink), "port", UDP_PORT, NULL);

    // add elements into the pipeline
    gst_bin_add_many(GST_BIN(mpPipeline), mpQmlSrc, rtpjpegpay, udpsink, NULL);

    // link the elements
    gst_element_link_many(mpQmlSrc, rtpjpegpay, udpsink, NULL);

    // setup the appsrc props
    g_object_set (G_OBJECT (mpQmlSrc),
                  "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                  "format", GST_FORMAT_TIME,
                  "is-live", TRUE,
                  NULL);

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
    // NOTE this callback is unused for now since
    // you cant grab frames from the qml outside the ui thread
    // so we simply grab frames on a timer
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
    sFrameStatus = CAPTURE::eSTATUS_CAPTURING;
    mGrabRes = mpQuickItem->grabToImage();

    QObject::connect(mGrabRes.data(), &QQuickItemGrabResult::ready, [&]()
    {
        // save the image into a byte array using a qbuffer
        QImage qmlImg = mGrabRes->image();
#if DBG_BLOCK
        qDebug() << "qml::ready - imgsz = " << qmlImg.byteCount();
#endif
        QBuffer buf(&mFrame);
        buf.open(QIODevice::WriteOnly);
        qmlImg.save(&buf, "jpeg", 85);
        buf.close();

        sFrameStatus = CAPTURE::eSTATUS_PUSHING;
        pushFrame();
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










