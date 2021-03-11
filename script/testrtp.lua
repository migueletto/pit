-- load libraies
camlib    = pit.loadlib("libv4l")
medialib  = pit.loadlib("libmedia")

-- used ports
RTSP_PORT = 8554
RTP_PORT  = 28600

-- create nodes
cam_node  = camlib.camera("/dev/video0", 640, 480, 0, 15)
rgb_node  = medialib.rgb()
jpeg_node = medialib.jpeg(50)
rtp_node  = medialib.rtp("0.0.0.0", RTP_PORT)

-- connect nodes: cam -> rgb -> jpeg ->rtp
if cam_node and rgb_node and jpeg_node and rtp_node then
  medialib.connect(cam_node,  rgb_node)
  medialib.connect(rgb_node,  jpeg_node)
  medialib.connect(jpeg_node, rtp_node)
end

-- function to start streaming
function start(t)
  if not cam_node then
    pit.debug(0, "camera is not open")
    return false
  end

  if handle then
    pit.debug(0, "camera is already started")
    return false
  end

  -- activate V4L camera node
  medialib.option(cam_node, "start")

  -- starts the PLAY thread: the source of the pipeline is the camera node
  handle = medialib.play(function() end, cam_node)
  if not handle then
    pit.debug(0, "camera start failed")
    return false
  end

  pit.debug(1, "camera started")
  return true
end

-- function to stop streaming
function stop(t)
  if handle then
    pit.debug(1, "camera stop")

    -- deactivate the V4L camera node
    medialib.option(cam_node, "stop")

    -- terminate the PLAY thread
    medialib.stop(handle) 

    handle = nil
  else
    pit.debug(0, "camera is not started")
  end
  return true
end

-- function to set the RTP client host and port
function setrtp(t, host, port)
  if rtp_node then
    pit.debug(1, "set RTP")
    medialib.option(rtp_node, "host", host)
    medialib.option(rtp_node, "port", port)
  end
end

-- cleanup function: stop services and destroy nodes
function cleanup_callback()
  if rtp then
    rtp:close()
  end
  if handle then
    stop()
  end
  if cam_node then
    medialib.destroy(cam_node)
    medialib.destroy(rgb_node)
    medialib.destroy(jpeg_node)
    medialib.destroy(rtp_node)
  end
end

pit.cleanup(cleanup_callback)

-- camera source: the rtp module uses this object to control the video source
cam = {
  start = start,   -- used by rtp module to start video
  stop = stop,     -- used by rtp module to stop video
  setrtp = setrtp  -- used by rtp module when a client is connected
}

-- lua module loading functionality
MODULE_PREFIX = "./script/"
modules = {}

-- loads a lua module from the script directory
function load_module(name)
  if not modules[name] then
    if pit.include(MODULE_PREFIX .. name .. ".lua") then
      pit.debug(1, "loaded module " .. name)
    end
  end
end

-- load required modules
load_module("network")
load_module("stream")
load_module("rtp")

-- create and start rtp module
rtp = modules.rtp.new(cam, RTSP_PORT, RTP_PORT)
rtp:open()
