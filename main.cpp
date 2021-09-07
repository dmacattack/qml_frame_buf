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
#include <QVector>

// member vars
GstElement *mpPipeline = NULL;
GstElement *pQmlSrc = NULL;
QQuickItem *pItem  = NULL;
QTimer *pTimer = NULL;
int cnt = 0;
QString filename = QString("/home/inspectron/Desktop/images/img_%1.jpg").arg(cnt);
QVector<QString> mImages;
qint64 ms = 0;

// function prototypes
gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);
void saveToFile(QByteArray buf, QString filename);
void cb_need_data (GstElement *appsrc, guint unused_size, gpointer user_data);



/**
 * @brief launchpipeline - launch a gst pipeline
 */
void launchpipeline_old()
{
    QString launchString = "";
#if 0
    QTextStream(&launchString) << "appsrc name=qmlsrc stream-type=0 is-live=1 ! \
                                   pngdec ! \
                                   clockoverlay ! \
                                   videoconvert ! \
                                   jpegenc ! \
                                   filesink location=" << filename;
#else
    QTextStream(&launchString) << "appsrc name=qmlsrc stream-type=0 is-live=1 ! \
                                   pngdec ! \
                                   clockoverlay ! \
                                   videoconvert ! \
                                   xvimagesink";
#endif

    mpPipeline = gst_parse_launch(launchString.toStdString().c_str(), NULL);
    gst_element_set_state (mpPipeline, GST_STATE_PLAYING);

    pQmlSrc = gst_bin_get_by_name(GST_BIN(mpPipeline), "qmlsrc");

    // i think this allows the pipeline to send the eos from element to element rather than the whole pipeline
    //g_object_set(mPipeline, MESSAGE_FORWARD, TRUE, NULL);

    GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (mpPipeline));
    gst_bus_add_watch (bus, bus_call, NULL);
    gst_object_unref (bus);

    qDebug() << "gst pipeline launched at " << QDateTime::currentDateTime().toString("hh:mm:ss");
}

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
    mpPipeline = gst_pipeline_new("pipeline");
    GstElement *appsrc       = gst_element_factory_make("appsrc",       "qmlsrc");
    GstElement *jpegdec      = gst_element_factory_make("jpegdec",      NULL);
    GstElement *clockoverlay = gst_element_factory_make("clockoverlay", NULL);
    GstElement *vidconvert   = gst_element_factory_make("videoconvert", NULL);
    GstElement *xvsink       = gst_element_factory_make("xvimagesink",  NULL);

    // TODO null check

    // set the member ptr to the appsrc
    pQmlSrc = appsrc;

    // setup the caps
    GstCaps *caps = gst_caps_new_simple("image/jpeg",
                                        "width",  G_TYPE_INT, 400,
                                        "height", G_TYPE_INT, 300,
                                        "framerate", GST_TYPE_FRACTION, 0, 1,
                                        NULL);
    g_object_set(G_OBJECT(appsrc), "caps", caps, NULL);

    // add elements into the pipeline
    gst_bin_add_many(GST_BIN(mpPipeline), appsrc, jpegdec, clockoverlay, vidconvert, xvsink, NULL);

    // link the elements
    gst_element_link_many(appsrc, jpegdec, clockoverlay, vidconvert, xvsink, NULL);

    // setup the appsrc props
    g_object_set (G_OBJECT (appsrc),
                  "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                  "format", GST_FORMAT_TIME,
                  "is-live", TRUE,
                  NULL);


    qWarning() << "play the pipeline";

    // set the appsrc callback for need data
    g_signal_connect (appsrc, "need-data", G_CALLBACK (cb_need_data), NULL);

    gst_element_set_state (mpPipeline, GST_STATE_PLAYING);
}

/**
 * @brief pushFrame - push a frame into the gst pipeline
 * @param frame - png encoded frame
 */
void pushFrame(QByteArray frame2)
{
    GstFlowReturn ret = GST_FLOW_OK;
    GstBuffer *buffer = NULL;
    GstMapInfo info;
    static GstClockTime timestamp = 0;

#if 0
    static bool doFirst = true;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 diff = (now - ms);
    if (doFirst || (diff >= 100)) /* 10fps */
    {
        doFirst = false;
        ms = now;

    }
    else
    {
        qWarning() << "ts: " << ms << now << diff;
        return;
    }
#endif

    // read the image from file rather than the provided frame
    QString fil = mImages.at(cnt);
    QFile f(fil);
    f.open(QIODevice::ReadOnly| QIODevice::Text);
    QByteArray frame = f.readAll();

    int len = frame.length();

    qDebug() << fil << "size = " << len << QDateTime::currentDateTime().toString("ss.zzz");

    // allocate a gst buffer
#if 0
    buffer = gst_buffer_new_allocate(NULL, len, NULL);
    gst_buffer_map(buffer, &info, GST_MAP_WRITE);
    unsigned char* buf = info.data;

    // move the frame into the buffer
    memmove(buf, frame.data(), len);
#endif

    buffer = gst_buffer_new_wrapped_full( (GstMemoryFlags)0,
                                          (gpointer)(frame.data()),
                                          len,
                                          0,
                                          len,
                                          NULL,
                                          NULL );
    // update filename
    filename = QString("/home/inspectron/Desktop/images/frame_%1_op.jpg").arg(cnt);
    cnt++;
    qDebug() << "         pushing " << filename;

    if (cnt > 10)
    {
        qFatal("stop");
    }

    // ?? no clue what this does
    GST_BUFFER_PTS (buffer) = timestamp;
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 4); // (val,num,den) 1/4 s = 250ms
    timestamp += GST_BUFFER_DURATION (buffer);

    // push into pipeline
    ret = gst_app_src_push_buffer((GstAppSrc*)(pQmlSrc), buffer);


    if (ret != GST_FLOW_OK)
    {
        qCritical() << "push error " << ret;
    }

    // cleanup memory
    //gst_buffer_unmap(buffer, &info);
}


/**
 * @brief captureFrame - capture a frame from the qml
 */
void captureFrame()
{
    QSharedPointer<const QQuickItemGrabResult> grabres = pItem->grabToImage();

    QObject::connect(grabres.data(), &QQuickItemGrabResult::ready, [=]()
    {
        QImage x = grabres->image();
        qDebug() << "qml::ready - imgsz = " << x.byteCount();
        QByteArray ba;
        QBuffer buf(&ba);
        buf.open(QIODevice::WriteOnly);
        x.save(&buf, "jpg");
        buf.close();

        // save this part to a file
        QString frameName = QString("/home/inspectron/Desktop/images/frame_%1.jpg").arg(cnt);
        qWarning() << __func__ << "saving frame to " << frameName;
        saveToFile(ba, frameName);

        pushFrame(ba);
    });
}

void onTimerExp()
{
    //pushFrame("");

    //captureFrame();
}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    QGuiApplication app(argc, argv);

    // create list of images
    for (int i = 0 ; i < 11 ; i++)
    {
        QString f = QString("/home/inspectron/Desktop/images/frame_%1.jpg").arg(i);
        mImages.push_back(f);
    }

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
    {return -1;}

    // create timer
    pTimer = new QTimer();
    pTimer->setInterval(500);
    QObject::connect(pTimer, &QTimer::timeout, onTimerExp);

    // start the timestamp timer
    ms = QDateTime::currentMSecsSinceEpoch();

    QObject *pRoot = engine.rootObjects().first();

    QObject *pFrame = pRoot->findChild<QObject*>("frameboy");
    qDebug() << "pFrame is null " << (pFrame == NULL);


    // start the pipeline
    launchpipeline();

    // select the qml src object
    pItem = qobject_cast<QQuickItem*>(pFrame);

    if (pItem != NULL)
    {
        pTimer->start();
    }
    else
    {
        qWarning() << "pItem is null";
    }





#if 0
    while(true)
    {
        // thanks to : https://stackoverflow.com/questions/37439554/save-qml-image-inside-c
        //qDebug() << "start the grab at " << QDateTime::currentMSecsSinceEpoch();

        QSharedPointer<const QQuickItemGrabResult> grabres = pItem->grabToImage();

        QObject::connect(grabres.data(), &QQuickItemGrabResult::ready, [=]()
        {
          //  qDebug() << "saving SS " << QDateTime::currentMSecsSinceEpoch();
            //pushFrame(grabres->image());
            QImage x = grabres->image();
            qDebug() << "imgsz = " << x.byteCount();
            QByteArray ba;
            QBuffer buf(&ba);
            buf.open(QIODevice::WriteOnly);
            x.save(&buf, "png");
            buf.close();

            pushFrame(ba);

#if 0
            auto list = QImageWriter::supportedImageFormats();
            for (int i = 0; i < list.length(); i++)
            {
                qDebug() << list.at(i);
            }

            "bmp"
            "cur"
            "icns"
            "ico"
            "jpeg"
            "jpg"
            "pbm"
            "pgm"
            "png"
            "ppm"
            "tif" // 480206 saved. vs 480000 raw
            "tiff" // 480206 saved. vs 480000 raw
            "wbmp"
            "webp"
            "xbm" // 77065
            "xpm" // 121279

#endif
            //qDebug() << "buffer len = " << ba.length();
            //saveToFile(ba);


            // save the frame to the filesystem
            //grabres->saveToFile("/home/inspectron/Desktop/frame.png");
        });

        QThread::msleep(200);

    }
#endif




#if 0
    QObject *pRoot = engine.rootObjects().first();

#if 1
    QObject *pFrame = pRoot->findChild<QObject*>("frameboy");
    qDebug() << "pFrame is null " << (pFrame == NULL);
#endif

    GrabWindow grab;
    grab.setResizeMode( QQuickView::SizeViewToRootObject );
    //grab.setSource(QUrl(QStringLiteral("qrc:/main.qml")));
    grab.setContent(QUrl(QStringLiteral("qrc:/main.qml")),NULL, pFrame );
    //grab.setFlags(Qt::Popup);

    QObject::connect(&grab, &GrabWindow::changeImage, [](QImage img)
    {
        qDebug() << "image changed " << img.size();
    });

//    grab.show();

#endif


    return app.exec();
}


/**
 * @brief bus_call - bus callback
 * @param bus
 * @param msg
 * @param data
 * @return
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

void saveToFile(QByteArray buf, QString filename)
{
    QFile f(filename);

    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
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
 * @brief cb_need_data - need data callback - update the want flag
 */
void cb_need_data (GstElement *appsrc, guint unused_size, gpointer user_data)
{
    Q_UNUSED(appsrc);
    Q_UNUSED(unused_size);
    Q_UNUSED(user_data);
  //prepare_buffer((GstAppSrc*)appsrc);

    qDebug() << "need data";

    pushFrame("");

    //want = 1;
}
