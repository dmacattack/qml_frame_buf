#include "grabwindow.hpp"
#include <limits>

GrabWindow::GrabWindow( QWindow * parent )
: QQuickView( parent )
{
  setClearBeforeRendering( false );
  setPosition( std::numeric_limits<unsigned short>::max(), std::numeric_limits<unsigned short>::max() );

  connect( this, SIGNAL( afterRendering()  ), SLOT( afterRendering()  ), Qt::DirectConnection );
  connect( this, SIGNAL( beforeRendering() ), SLOT( beforeRendering() ), Qt::DirectConnection );
}

void GrabWindow::afterRendering()
{
  if( !fbo_.isNull() )
  {
    emit changeImage( fbo_->toImage() );
  }
}

void GrabWindow::beforeRendering()
{
  if (!fbo_)
  {
        fbo_.reset(new QOpenGLFramebufferObject( size(), QOpenGLFramebufferObject::NoAttachment) );
        setRenderTarget(fbo_.data());
  }
}
