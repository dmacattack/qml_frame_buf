#ifndef QMLCAPTURE_HPP
#define QMLCAPTURE_HPP

#include <gst/gst.h>
#include <QQuickItem>
#include <QObject>
#include <QTimer>

/**
 * @brief The QmlCapture class - object to capture a qml object into a video/picture/etc
 */
class QmlCapture
: public QObject
{
    Q_OBJECT
public:
    QmlCapture();
    ~QmlCapture();

    void captureImage(QObject *pObj, QString path);
    void startVideo(QObject *pObj, QString path);
    void stopVideo();
    void toVideoSink(QObject *pObj);
    void toTee(QObject *pObj);

signals:
// TODO add signal for status update

private slots:
    void onCaptureTimerExp();

private:
    void pushFrame(char* buf, guint size);
    void pushFrame(QByteArray &buf);
    void pushFrame(gpointer buf, guint size);
    void launchVideoSinkPipeline();
    static void sAppsrcNeedsData(GstElement *appsrc, guint unused_size, gpointer user_data);
    void printTimeStamp();
    void captureFrame();
    void saveToFile(QByteArray buf, QString filename);

private:
    GstElement *mpPipeline;
    GstElement *mpQmlSrc;
    QQuickItem *mpQuickItem;
    QTimer *mpCaptureTimer;
    static bool sAppSrcNeedsData;
    static QmlCapture *theCaptureClass;

};

#endif // QMLCAPTURE_HPP
