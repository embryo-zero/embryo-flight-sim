// ScreenshotUriHandler.cxx -- Provide screenshots via http
//
// Started by Curtis Olson, started June 2001.
// osg support written by James Turner
// Ported to new httpd infrastructure by Torsten Dreyer
//
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2014 Torsen Dreyer

#include "ScreenshotUriHandler.hxx"

#include <osgDB/Registry>
#include <osgDB/ReaderWriter>
#include <osgUtil/SceneView>
#include <osgViewer/Viewer>

#include <Canvas/canvas_mgr.hxx>
#include <GUI/dialog.hxx>
#include <GUI/new_gui.hxx>
#include <simgear/canvas/Canvas.hxx>

#include <simgear/threads/SGQueue.hxx>
#include <simgear/structure/Singleton.hxx>
#include <Main/globals.hxx>
#include <Viewer/renderer.hxx>

#include <atomic>
#include <queue>

using std::string;
using std::vector;
using std::list;

namespace sc = simgear::canvas;

namespace flightgear {
namespace http {

///////////////////////////////////////////////////////////////////////////

class ImageReadyListener {
public:
  virtual void imageReady(osg::ref_ptr<osg::Image>) = 0;
  virtual ~ImageReadyListener()
  {
  }
};

class StringReadyListener {
public:
  virtual void stringReady(const std::string &) = 0;
  // called instead of stringReady if the image could not be compressed
  virtual void stringFailed() {}
  virtual ~StringReadyListener()
  {
  }
};

struct ImageCompressionTask {
  StringReadyListener * stringReadyListener;
  string format;
  osg::ref_ptr<osg::Image> image;

  ImageCompressionTask()
  {
    stringReadyListener = NULL;
  }

  ImageCompressionTask(const ImageCompressionTask & other)
  {
    stringReadyListener = other.stringReadyListener;
    format = other.format;
    image = other.image;
  }

  ImageCompressionTask & operator =(const ImageCompressionTask & other)
  {
    stringReadyListener = other.stringReadyListener;
    format = other.format;
    image = other.image;
    return *this;
  }

};

class ImageCompressor: public OpenThreads::Thread {
public:
  ImageCompressor()
  {
  }
  virtual void run();
  void addTask(ImageCompressionTask & task);
  private:
  typedef SGBlockingQueue<ImageCompressionTask> TaskList;
  TaskList _tasks;
};

typedef simgear::Singleton<ImageCompressor> ImageCompressorSingleton;

void ImageCompressor::run()
{
  osg::ref_ptr<osgDB::ReaderWriter::Options> options = new osgDB::ReaderWriter::Options("JPEG_QUALITY 80 PNG_COMPRESSION 9");

  SG_LOG(SG_NETWORK, SG_DEBUG, "ImageCompressor is running");
  for (;;) {
    ImageCompressionTask task = _tasks.pop();
    SG_LOG(SG_NETWORK, SG_DEBUG, "ImageCompressor has an image");
    if ( NULL != task.stringReadyListener) {
      SG_LOG(SG_NETWORK, SG_DEBUG, "ImageCompressor checking for writer for " << task.format);
      osgDB::ReaderWriter* writer = osgDB::Registry::instance()->getReaderWriterForExtension(task.format);
      if (!writer) {
          task.stringReadyListener->stringFailed();
          continue;
      }

      if (!task.image) {
          SG_LOG(SG_NETWORK, SG_DEV_WARN, "ImageCOmpressor passed NULL image-ref");
          task.stringReadyListener->stringFailed();
          continue;
      }

      SG_LOG(SG_NETWORK, SG_DEBUG, "ImageCompressor compressing to " << task.format);
      std::stringstream outputStream;
      osgDB::ReaderWriter::WriteResult wr;
      wr = writer->writeImage(*task.image, outputStream, options);

      if (wr.success()) {
        SG_LOG(SG_NETWORK, SG_DEBUG, "ImageCompressor compressed to  " << task.format);
        task.stringReadyListener->stringReady(outputStream.str());
      } else {
          SG_LOG(SG_NETWORK, SG_WARN, "ImageCompressor: failed to write " << task.format << " image (" << task.image->s() << "x" << task.image->t() << ", pixel format 0x" << std::hex << task.image->getPixelFormat() << std::dec << "): " << wr.message());
          task.stringReadyListener->stringFailed();
      }
      SG_LOG(SG_NETWORK, SG_DEBUG, "ImageCompressor done for this image" << task.format);
    }
  }
  SG_LOG(SG_NETWORK, SG_DEBUG, "ImageCompressor exiting");
}

void ImageCompressor::addTask(ImageCompressionTask & task)
{
  _tasks.push(task);
}

/**
 * Based on <a href="http://code.google.com/p/osgworks">osgworks</a> ScreenCapture.cpp
 *
 */
class ScreenshotCallback: public osg::Camera::DrawCallback {
public:
  ScreenshotCallback()
      : _min_delta_tick(1.0/8.0)
  {
    _previousFrameTick = osg::Timer::instance()->tick();
  }

  virtual void operator ()(osg::RenderInfo& renderInfo) const
  {
    osg::Timer_t n = osg::Timer::instance()->tick();
    double dt = osg::Timer::instance()->delta_s(_previousFrameTick,n);
    if (dt < _min_delta_tick)
        return;
    _previousFrameTick = n;

    bool hasSubscribers = false;
    {
      OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_lock);
      hasSubscribers = !_subscribers.empty();

    }
    if (hasSubscribers) {
      osg::ref_ptr<osg::Image> image = new osg::Image;
      const osg::Viewport* vp = renderInfo.getState()->getCurrentViewport();
      image->readPixels(vp->x(), vp->y(), vp->width(), vp->height(), GL_RGB, GL_UNSIGNED_BYTE);
      {
        OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_lock);
        while (!_subscribers.empty()) {
          try {
            _subscribers.back()->imageReady(image);
          }
          catch (...) {
          }
          _subscribers.pop_back();

        }
      }
    }
  }

  void subscribe(ImageReadyListener * subscriber)
  {
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_lock);
    _subscribers.push_back(subscriber);
  }

  void unsubscribe(ImageReadyListener * subscriber)
  {
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_lock);
    _subscribers.remove( subscriber );
  }

private:
  mutable list<ImageReadyListener*> _subscribers;
  mutable OpenThreads::Mutex _lock;
  mutable double _previousFrameTick;
  double _min_delta_tick;
};

///////////////////////////////////////////////////////////////////////////

class ScreenshotRequest: public ConnectionData, public ImageReadyListener, StringReadyListener {
public:
  ScreenshotRequest(const string & window, const string & type, bool stream)
      : _type(type), _stream(stream)
  {
    if ( NULL == osgDB::Registry::instance()->getReaderWriterForExtension(_type))
    throw sg_format_exception("Unsupported image type: " + type, type);

    osg::Camera * camera = findLastCamera(globals->get_renderer()->getViewerBase(), window);
    if ( NULL == camera)
    throw sg_error("Can't find a camera for window '" + window + "'");

    // add our ScreenshotCallback to the camera
    if ( NULL == camera->getFinalDrawCallback()) {
      // TODO: are we leaking the Callback on reinit?
      camera->setFinalDrawCallback(new ScreenshotCallback());
    }

    _screenshotCallback = dynamic_cast<ScreenshotCallback*>(camera->getFinalDrawCallback());
    if ( NULL == _screenshotCallback)
    throw sg_error("Can't find ScreenshotCallback");

    requestScreenshot();
  }

  virtual ~ScreenshotRequest()
  {
    _screenshotCallback->unsubscribe(this);
  }

  virtual void imageReady(osg::ref_ptr<osg::Image> rawImage)
  {
    // called from a rendering thread, not from the main loop
    ImageCompressionTask task;
    task.image = rawImage;
    task.format = _type;
    task.stringReadyListener = this;
    ImageCompressorSingleton::instance()->addTask(task);
  }

  void requestScreenshot()
  {
    _screenshotCallback->subscribe(this);
  }

  mutable OpenThreads::Mutex _lock;

  virtual void stringReady(const string & s)
  {
    // called from the compressor thread
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_lock);
    _compressedData = s;
  }

  string getScreenshot()
  {
    string reply;
    {
      // called from the main loop
      OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_lock);
      reply = _compressedData;
      _compressedData.clear();
    }
    return reply;
  }

  osg::Camera* findLastCamera(osgViewer::ViewerBase * viewer, const string & windowName)
  {
    osgViewer::ViewerBase::Windows windows;
    viewer->getWindows(windows);

    osgViewer::GraphicsWindow* window = NULL;

    if (!windowName.empty()) {
      for (osgViewer::ViewerBase::Windows::iterator itr = windows.begin(); itr != windows.end(); ++itr) {
        if ((*itr)->getTraits()->windowName == windowName) {
          window = *itr;
          break;
        }
      }
    }

    if ( NULL == window) {
      if (!windowName.empty()) {
        SG_LOG(SG_NETWORK, SG_INFO, "requested window " << windowName << " not found, using first window");
      }
      window = *windows.begin();
    }

    SG_LOG(SG_NETWORK, SG_DEBUG, "Looking for last Camera of window '" << window->getTraits()->windowName << "'");

    osg::GraphicsContext::Cameras& cameras = window->getCameras();
    osg::Camera* lastCamera = 0;
    for (osg::GraphicsContext::Cameras::iterator cam_itr = cameras.begin(); cam_itr != cameras.end(); ++cam_itr) {
      if (lastCamera) {
        if ((*cam_itr)->getRenderOrder() > lastCamera->getRenderOrder()) {
          lastCamera = (*cam_itr);
        }
        if ((*cam_itr)->getRenderOrder() == lastCamera->getRenderOrder()
            && (*cam_itr)->getRenderOrderNum() >= lastCamera->getRenderOrderNum()) {
          lastCamera = (*cam_itr);
        }
      } else {
        lastCamera = *cam_itr;
      }
    }

    return lastCamera;
  }

  bool isStream() const
  {
    return _stream;
  }

  const string & getType() const
  {
    return _type;
  }

private:
  string _type;
  bool _stream;
  string _compressedData;
  ScreenshotCallback * _screenshotCallback;
};

/**
 */
class CanvasImageRequest : public ConnectionData, public simgear::canvas::CanvasImageReadyListener, StringReadyListener {
public:
    // true while an image handed to the compressor thread hasn't been
    // completed (or failed) yet
    std::atomic<bool> compressing{false};
    sc::CanvasPtr canvas;
    int connected = 0;

    CanvasImageRequest(const string & window, const string & type, int canvasindex, bool stream)
    : _type(type), _stream(stream) {
        SG_LOG(SG_NETWORK, SG_DEBUG, "CanvasImageRequest:");

        if (NULL == osgDB::Registry::instance()->getReaderWriterForExtension(_type))
            throw sg_format_exception("Unsupported image type: " + type, type, {}, false /* don't report */);

        auto canvas_mgr = globals->get_subsystem<CanvasMgr>();
        if (!canvas_mgr) {
            SG_LOG(SG_NETWORK, SG_WARN, "CanvasImage:CanvasMgr not found");
        } else {
            canvas = canvas_mgr->getCanvas(canvasindex);
            if (!canvas) {
                throw sg_error("CanvasImage:Canvas not found for index " + std::to_string(canvasindex), {}, false /* don't report */);
            } else {
                SG_LOG(SG_NETWORK, SG_DEBUG, "CanvasImage:Canvas found for index " << canvasindex);
                //SG_LOG(SG_NETWORK, SG_DEBUG, "CanvasImageRequest: found camera " << camera << ", width=" << canvas->getSizeX() << ", height=%d\n" << canvas->getSizeY());

                SGConstPropertyNode_ptr canvasnode = canvas->getProps();
                if (canvasnode) {
                    string canvasname = canvasnode->getStringValue("name");
                    if (!canvasname.empty()) {
                        SG_LOG(SG_NETWORK, SG_INFO, "CanvasImageRequest: node=" << canvasnode->getDisplayName().c_str() << ", canvasname =" << canvasname);
                    }
                }
                //Looping until success is no option
                connected = canvas->subscribe(this);
            }
        }
    }

    // Assumption: when unsubscribe returns,there might just be a compressor thread running,
    // causing a crash when the deconstructor finishes. Rare, but might happen. Just wait to be sure.
    virtual ~CanvasImageRequest() {
        if (compressing) {
            SG_LOG(SG_NETWORK, SG_ALERT, "CanvasImage: task running, pausing for 15 seconds");
            SGTimeStamp::sleepFor(SGTimeStamp::fromSec(15));
        }

        if (canvas && connected){
            canvas->unsubscribe(this);
        }
    }

    void imageReady(osg::ref_ptr<osg::Image> rawImage) override
    {
        SG_LOG(SG_NETWORK, SG_DEBUG, "CanvasImage:imageReady");
        // called from a rendering thread, not from the main loop
        ImageCompressionTask task;
        compressing = true;
        task.image = rawImage;
        task.format = _type;
        task.stringReadyListener = this;
        ImageCompressorSingleton::instance()->addTask(task);
    }

    void requestCanvasImage() {
        connected = canvas->subscribe(this);
    }

    mutable OpenThreads::Mutex _lock;

    void stringReady(const string& s) override
    {
        SG_LOG(SG_NETWORK, SG_DEBUG, "CanvasImage:stringReady");

        // called from the compressor thread
        OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_lock);
        _compressedData = s;
        // allow destructor
        compressing = false;
    }

    void stringFailed() override
    {
        compressing = false;
    }

    string getCanvasImage() {
        string reply;
        {
            // called from the main loop
            OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_lock);
            reply = _compressedData;
            _compressedData.clear();
        }
        return reply;
    }

    bool isStream() const {
        return _stream;
    }

    const string & getType() const {
        return _type;
    }

private:
    string _type;
    bool _stream;
    string _compressedData;
};

ScreenshotUriHandler::ScreenshotUriHandler(const std::string& uri)
    : URIHandler(uri)
{
}

ScreenshotUriHandler::~ScreenshotUriHandler()
{
  ImageCompressorSingleton::instance()->cancel();
  //ImageCompressorSingleton::instance()->join();
}

const static string KEY_SCREENSHOT("ScreenshotUriHandler::ScreenshotRequest");
const static string KEY_CANVASIMAGE("ScreenshotUriHandler::CanvasImageRequest");
#define BOUNDARY "--fgfs-screenshot-boundary"

bool ScreenshotUriHandler::handleGetRequest(const HTTPRequest & request, HTTPResponse & response, Connection * connection)
{
  if (!ImageCompressorSingleton::instance()->isRunning())
  ImageCompressorSingleton::instance()->start();

  string type = request.RequestVariables.get("type");
  if (type.empty()) type = "jpg";

  //  string camera = request.RequestVariables.get("camera");
  string window = request.RequestVariables.get("window");

  bool stream = (!request.RequestVariables.get("stream").empty());

  int canvasindex = -1;
  string s_canvasindex = request.RequestVariables.get("canvasindex");
  if (!s_canvasindex.empty()) canvasindex = atoi(s_canvasindex.c_str());

  // 'dialog' takes precedence over 'canvasindex': resolve the named dialog
  // to the canvas which renders it
  const string dialogName = request.RequestVariables.get("dialog");
  if (!dialogName.empty()) {
      auto gui = globals->get_subsystem<NewGUI>();
      FGDialog* dialog = gui ? gui->getDialog(dialogName) : nullptr;
      canvasindex = dialog ? dialog->getCanvasIndex() : -1;
      if (canvasindex < 0) {
          response.Header["Content-Type"] = "text/plain";
          response.StatusCode = 404;
          response.Content = "Dialog not found, not open, or has no canvas: " + dialogName;
          return true;
      }
  }

  SG_LOG(SG_NETWORK, SG_DEBUG, "screenshot request: dialog='" << dialogName << "' canvasindex=" << canvasindex << " type=" << type);

  SGSharedPtr<ScreenshotRequest> screenshotRequest;
  SGSharedPtr<CanvasImageRequest> canvasimageRequest;
  try {
    SG_LOG(SG_NETWORK, SG_DEBUG, "new ScreenshotRequest("<<window<<","<<type<<"," << stream << "," << canvasindex <<")");
    if (canvasindex == -1)
        screenshotRequest = new ScreenshotRequest(window, type, stream);
    else
        canvasimageRequest = new CanvasImageRequest(window, type, canvasindex, stream);
  }
  catch (sg_format_exception & ex)
  {
    SG_LOG(SG_NETWORK, SG_INFO, ex.getFormattedMessage());
    response.Header["Content-Type"] = "text/plain";
    response.StatusCode = 410;
    response.Content = ex.getFormattedMessage();
    return true;
  }
  catch (sg_error & ex)
  {
    SG_LOG(SG_NETWORK, SG_INFO, ex.getFormattedMessage());
    response.Header["Content-Type"] = "text/plain";
    response.StatusCode = 500;
    response.Content = ex.getFormattedMessage();
    return true;
  }

  if (!stream) {
    response.Header["Content-Type"] = string("image/").append(type);
    response.Header["Content-Disposition"] = string("inline; filename=\"fgfs-screen.").append(type).append("\"");
  } else {
    response.Header["Content-Type"] = string("multipart/x-mixed-replace; boundary=" BOUNDARY);

  }

  if (canvasindex == -1)
      connection->put(KEY_SCREENSHOT, screenshotRequest);
  else
      connection->put(KEY_CANVASIMAGE, canvasimageRequest);
  return false; // call me again thru poll
}

bool ScreenshotUriHandler::poll(Connection * connection)
{
  SGSharedPtr<ConnectionData> data = connection->get(KEY_SCREENSHOT);
  if (data) {
    ScreenshotRequest * screenshotRequest = dynamic_cast<ScreenshotRequest*>(data.get());
    if ( NULL == screenshotRequest) return true; // Should not happen, kill the connection

    const string & screenshot = screenshotRequest->getScreenshot();
    if (screenshot.empty()) {
      SG_LOG(SG_NETWORK, SG_DEBUG, "No screenshot available.");
      return false; // not ready yet, call again.
    }

    SG_LOG(SG_NETWORK, SG_DEBUG, "Screenshot is ready, size=" << screenshot.size());

      if (screenshotRequest->isStream()) {
        std::ostringstream ss;
        ss << BOUNDARY << "\r\nContent-Type: image/";
        ss << screenshotRequest->getType() << "\r\nContent-Length:";
        ss << screenshot.size() << "\r\n\r\n";
        connection->write(ss.str().c_str(), ss.str().length());
      }

      connection->write(screenshot.data(), screenshot.size());

      if (screenshotRequest->isStream()) {
        screenshotRequest->requestScreenshot();
        // continue until user closes connection
        return false;
      }

      // single screenshot, send terminating chunk
      connection->remove(KEY_SCREENSHOT);
      connection->write("", 0);
      return true; // done.
  } // Screenshot

  // CanvasImage
  data = connection->get(KEY_CANVASIMAGE);
  CanvasImageRequest * canvasimageRequest = dynamic_cast<CanvasImageRequest*> (data.get());
  if (NULL == canvasimageRequest) return true; // Should not happen, kill the connection

  if (!canvasimageRequest->connected) {
      SG_LOG(SG_NETWORK, SG_INFO, "CanvasImageRequest: not connected. Resubscribing");
      canvasimageRequest->requestCanvasImage();
  }

  const string & canvasimage = canvasimageRequest->getCanvasImage();
  if (canvasimage.empty()) {
      SG_LOG(SG_NETWORK, SG_BULK, "No canvasimage available.");
      return false; // not ready yet, call again.
  }

  SG_LOG(SG_NETWORK, SG_DEBUG, "CanvasImage is ready, size=" << canvasimage.size());

  if (canvasimageRequest->isStream()) {
      std::ostringstream ss;
      ss << BOUNDARY << "\r\nContent-Type: image/";
      ss << canvasimageRequest->getType() << "\r\nContent-Length:";
      ss << canvasimage.size() << "\r\n\r\n";
      connection->write(ss.str().c_str(), ss.str().length());
  }
  connection->write(canvasimage.data(), canvasimage.size());
  if (canvasimageRequest->isStream()) {
      canvasimageRequest->requestCanvasImage();
      // continue until user closes connection
      return false;
  }

  SG_LOG(SG_NETWORK, SG_DEBUG, "screenshot request: sent " << canvasimage.size() << " bytes");
  // single canvasimage: done (the httpd terminates the response)
  connection->remove(KEY_CANVASIMAGE);
  return true; // done.
}

} // namespace http
} // namespace flightgear
