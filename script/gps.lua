function cleanup_callback()
  if handle then
    gps.close(handle)
  end
end

pit.cleanup(cleanup_callback)

function gps_callback(type, mode, ts,
                      year, month, day, hour, min, sec,
                      longitude, latitude, alt, speed, course)
  if type == 1 and mode >= 2 then
    local date = string.format("%04d-%02d-%02d ", year, month, day)
    local time = string.format("%02d:%02d:%02d ", hour, min, sec)
    pit.debug(1, date .. time)
  end
end

pit.loadlib("libbt")
gps = pit.loadlib("libnmea")
handle = gps.open(gps_callback, "17:7F:1C:0E:31:69", 1, 0)
