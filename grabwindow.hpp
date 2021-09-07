#ifndef GRABWINDOW_HPP
#define GRABWINDOW_HPP

#include <QOpenGLFramebufferObject>
#include <QScopedPointer>
#include <QQuickView>
#include <QImage>

class GrabWindow: public QQuickView
{
  Q_OBJECT

signals:
     void changeImage( const QImage &image );

public:
    GrabWindow( QWindow * parent = 0 );

private slots:
    void afterRendering();
    void beforeRendering();

private:
    QScopedPointer<QOpenGLFramebufferObject> fbo_;
};

#endif // GRABWINDOW_HPP
