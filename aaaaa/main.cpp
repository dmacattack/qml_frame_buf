#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QByteArray>
#include <QDateTime>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdint.h>
#include <QString>
#include <unistd.h>

// got this from https://gist.github.com/floe/e35100f091315b86a5bf

int want = 1;
uint16_t b_white[384*288];
uint16_t b_black[384*288];
GstElement *mpAppSrc = NULL;

/**
 * @brief pushFrame - push a frame onto the bus
 * @param appsrc
 */
static void pushFrame(GstAppSrc* appsrc)
{

  static gboolean white = FALSE;
  static GstClockTime timestamp = 0;
  GstBuffer *buffer;
  guint size;
  GstFlowReturn ret;
  static qint64 dt = 0;

  // escape if we dont want a buffer - this seems to add a dummy frame delay ?
  //if (!want)
  //{
  //    return;
  //}
  want = 0;

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

  size = 384 * 288 * 2;

  buffer = gst_buffer_new_wrapped_full( (GstMemoryFlags)0,
                                        (gpointer)(white?b_white:b_black),
                                        size,
                                        0,
                                        size,
                                        NULL,
                                        NULL );

  white = !white;

  GST_BUFFER_PTS (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 4); // (val,num,den)
  timestamp += GST_BUFFER_DURATION (buffer);

  ret = gst_app_src_push_buffer(appsrc, buffer);

  if (ret != GST_FLOW_OK)
  {
    /* something wrong, stop pushing */
    // g_main_loop_quit (loop);
  }
}

/**
 * @brief cb_need_data - need data callback - update the want flag
 */
static void cb_need_data (GstElement *appsrc, guint unused_size, gpointer user_data)
{
    Q_UNUSED(appsrc);
    Q_UNUSED(unused_size);
    Q_UNUSED(user_data);

    qDebug() << "gimme data";
  //prepare_buffer((GstAppSrc*)appsrc);
    pushFrame((GstAppSrc*)mpAppSrc);

    //want = 1;
}

gint main (gint argc, gchar *argv[])
{
  GstElement *pipeline, *appsrc, *conv, *videosink;

  // create black and white frames
  for (int i = 0; i < 384*288; i++) { b_black[i] = 0; b_white[i] = 0xFFFF; }

  // init GStreamer
  gst_init (&argc, &argv);

#if 1
  // setup pipeline
  pipeline  = gst_pipeline_new         ("pipeline");
  appsrc    = gst_element_factory_make ("appsrc", "source");
  conv      = gst_element_factory_make ("videoconvert", "conv");
  videosink = gst_element_factory_make ("xvimagesink", "videosink");

  // member var
  mpAppSrc = appsrc;

  // setup
  GstCaps *caps = gst_caps_new_simple ("video/x-raw",
                                       "format", G_TYPE_STRING, "RGB16",
                                       "width", G_TYPE_INT, 384,
                                       "height", G_TYPE_INT, 288,
                                       "framerate", GST_TYPE_FRACTION, 0, 1,
                                       NULL);
  g_object_set (G_OBJECT (appsrc), "caps", caps, NULL);
  gst_caps_unref(caps);

  // add elements into the pipeline
  gst_bin_add_many (GST_BIN (pipeline), appsrc, conv, videosink, NULL);

  // link the elements
  gst_element_link_many (appsrc, conv, videosink, NULL);

  // setup appsrc
  g_object_set (G_OBJECT (appsrc),
                "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
                "format", GST_FORMAT_TIME,
                "is-live", TRUE,
                NULL);
#else
    pipeline = gst_parse_launch( "appsrc name=source stream-type=0 format=3 is-live=1 \
                                         caps=\"video/x-raw, \
                                         format=(string)RGB16, \
                                         width=(int)384, \
                                         height=(int)288, \
                                         framerate(fraction)=0/1\" \
                                  videoconvert name=conv ! \
                                  xvimagesink name=videosink", NULL);
    appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "source");

#endif
  g_signal_connect (appsrc, "need-data", G_CALLBACK (cb_need_data), NULL);

  /* play */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  // start pushing frames
  while (1)
  {
    usleep(100);
    //pushFrame((GstAppSrc*)appsrc);
    //g_main_context_iteration(g_main_context_default(),FALSE);
  }

  /* clean up */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}






