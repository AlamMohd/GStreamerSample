#include <iostream>
#include <string>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <thread>

GMainLoop *loopVideoCapture;
GMainLoop *loopVideoStream;

GstElement *Stream_pipeline, *Stream_appsrc, *Stream_conv, *Stream_videosink, *Stream_videoenc, *Stream_mux, *Stream_payloader, *Stream_filter;
GstCaps *convertCaps;

#define AUTO_VIDEO_SINK  0
#define OLD_ALGO         0

void fill_appsrc(const unsigned char *DataPtr, gsize size)
{
  GstBuffer *buffer = gst_buffer_new_and_alloc(size);
  gst_buffer_fill(buffer, 0, DataPtr, size);
  
  if (gst_app_src_push_buffer(GST_APP_SRC(Stream_appsrc), buffer) != GST_FLOW_OK)
  {
    g_printerr("Error: Buffer mapping failed.\n");
  }
}

void VideoStream(void)
{
  gst_init (nullptr, nullptr);
  loopVideoStream = g_main_loop_new (NULL, FALSE);
  
  Stream_pipeline = gst_pipeline_new ("pipeline");
  Stream_appsrc = gst_element_factory_make ("appsrc", "source");
  Stream_conv = gst_element_factory_make ("videoconvert", "conv");
  
  #if(AUTO_VIDEO_SINK == 1)
  Stream_videosink = gst_element_factory_make ("autovideosink", "videosink");

  gst_bin_add_many (GST_BIN (Stream_pipeline), Stream_appsrc, Stream_conv, Stream_videosink, NULL);
  gst_element_link_many (Stream_appsrc, Stream_conv, Stream_videosink, NULL);
  
  #else
  
  Stream_videosink = gst_element_factory_make ("udpsink", "videosink");
  g_object_set(G_OBJECT(Stream_videosink), "host", "127.0.0.1", "port", 5000, NULL);
  Stream_videoenc = gst_element_factory_make ("avenc_mpeg4", "videoenc");
  
  #if(OLD_ALGO == 1)
  Stream_payloader = gst_element_factory_make ("rtpmp4vpay", "payloader");
  g_object_set(G_OBJECT(Stream_payloader), "config-interval", 60, NULL);
  
  gst_bin_add_many (GST_BIN (Stream_pipeline), Stream_appsrc, Stream_conv, Stream_videoenc, Stream_payloader, Stream_videosink, NULL);
  gst_element_link_many (Stream_appsrc, Stream_conv, Stream_videoenc, Stream_payloader, Stream_videosink, NULL);
  
  #else
    
  Stream_filter = gst_element_factory_make ("capsfilter", "converter-caps");
  Stream_mux = gst_element_factory_make ("mpegtsmux", "ts-muxer");
  Stream_payloader = gst_element_factory_make ("rtpmp2tpay", "rtp-payloader");
  
  convertCaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, "I420",
    "width", G_TYPE_INT, 1280,
    "height", G_TYPE_INT, 720,
    "framerate", GST_TYPE_FRACTION, 30, 1,
    NULL);

  g_object_set (G_OBJECT (Stream_filter), "caps", convertCaps, NULL);
  g_object_set (G_OBJECT (Stream_payloader), "pt", 33, NULL);
  
  gst_bin_add_many (GST_BIN (Stream_pipeline), Stream_appsrc, Stream_conv, Stream_filter,  Stream_videoenc, Stream_mux, Stream_payloader, Stream_videosink, NULL);
  gst_element_link_many (Stream_appsrc, Stream_conv, Stream_filter , Stream_videoenc, Stream_mux, Stream_payloader, Stream_videosink, NULL);
  #endif  
  
  #endif

  g_object_set (G_OBJECT (Stream_appsrc), "caps",
    gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "RGB",
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
      "framerate", GST_TYPE_FRACTION, 30, 1, 
      NULL), 
    NULL);
    
  g_object_set (G_OBJECT (Stream_appsrc),
    "stream-type", 0,
    "is-live", TRUE,
    "format", GST_FORMAT_TIME, 
    NULL);
  
   gst_element_set_state(Stream_pipeline, GST_STATE_PLAYING);
   g_main_loop_run(loopVideoStream);
    
   gst_element_set_state(Stream_pipeline, GST_STATE_NULL);
   gst_object_unref(Stream_pipeline);
}

GstFlowReturn NewFrame(GstElement *sink, void *data) {
  
  GstSample *sample = nullptr;
  GstBuffer *buffer = nullptr;
  GstMapInfo map;

  g_signal_emit_by_name(sink, "pull-sample", &sample);
  
  if (!sample) {
    g_printerr("Error: Could not obtain sample.\n");
    return GST_FLOW_ERROR;
  }
  
  buffer = gst_sample_get_buffer (sample);  
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  
  fill_appsrc(map.data, map.size);
  
  gst_buffer_unmap(buffer, &map);
  gst_sample_unref(sample);
  
  return GST_FLOW_OK;
}

void VideoCapture(void)
{
  GstElement *pipeline, *source, *mjpegcaps, *decodemjpef, *convertmjpeg, *rgbcaps, *convertrgb, *sink;
  
  gst_init(nullptr, nullptr);
  
  pipeline     = gst_pipeline_new ("new-pipeline");
  source       = gst_element_factory_make("v4l2src", "source");
  mjpegcaps    = gst_element_factory_make("capsfilter", "mjpegcaps");
  decodemjpef  = gst_element_factory_make("jpegdec", "decodemjpef");
  convertmjpeg = gst_element_factory_make("videoconvert", "convertmjpeg");
  rgbcaps      = gst_element_factory_make("capsfilter", "rgbcaps");
  convertrgb   = gst_element_factory_make("videoconvert", "convertrgb");
  sink         = gst_element_factory_make("appsink", "sink");
  
  g_object_set(source, "device", "/dev/video0", NULL);
  
  g_object_set (G_OBJECT(mjpegcaps), "caps",
    gst_caps_new_simple (
      "image/jpeg", 
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL), 
    NULL);
    
  g_object_set (G_OBJECT(rgbcaps), "caps",
    gst_caps_new_simple (
      "video/x-raw", 
      "format", G_TYPE_STRING, "RGB",
      NULL), 
    NULL);
    
  g_object_set(sink, "emit-signals", TRUE, "sync", FALSE, NULL);
  g_signal_connect(sink, "new-sample", G_CALLBACK(NewFrame), nullptr);
  
  gst_bin_add_many(GST_BIN (pipeline), source, mjpegcaps, decodemjpef, convertmjpeg, rgbcaps, convertrgb, sink, NULL);
  if (!gst_element_link_many(source, mjpegcaps, decodemjpef, convertmjpeg, rgbcaps, convertrgb, sink, NULL)) {
    g_printerr("Error: Elements could not be linked.\n");
  }
  

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  loopVideoCapture = g_main_loop_new(NULL, FALSE);  
  g_main_loop_run(loopVideoCapture);
  
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
}

int main(void){
  
  std::thread ThreadStreame(VideoStream);
  std::thread ThreadCapture(VideoCapture);
  
  std::this_thread::sleep_for(std::chrono::milliseconds(10000));
  g_main_loop_quit(loopVideoCapture);
  g_main_loop_quit(loopVideoStream);
  ThreadStreame.join();
  ThreadCapture.join();
  
  return true;
}