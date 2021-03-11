-- stream module: a lua wrapper for libstream library

modules.stream = {
  new = function(callback, streamlib, white_list)
    local t = {}

    t.streamlib = streamlib
    t.handle = nil
    t.bound = false
    t.data = {}
    t.peer = {}

    t.stream_callback = function(self, event, handle, addr, port, buffer)
      if event == t.streamlib.BIND then
        pit.debug(1, "bound to port " .. port)
        self.bound = true
      end

      if event == t.streamlib.ACCEPT then
        if (not white_list) or white_list[addr] then
          pit.debug(1, "connection from " .. addr)
          self.data[handle] = {}
          self.peer[handle] = addr
          callback(self, event, handle, self.peer[handle], nil, self.data[handle])
        else
          -- reject connection
          pit.debug(0, "unauthorized connection from " .. addr)
          return true
        end
        return false
      end

      if event == t.streamlib.DISCONNECT then
        pit.debug(1, "disconnected from " .. addr)
        self.data[handle] = nil
        self.peer[handle] = nil
        callback(self, event, handle, self.peer[handle], nil, self.data[handle])
        return false
      end

      if event == t.streamlib.LINE then
        callback(self, event, handle, self.peer[handle], buffer, self.data[handle])
        return false
      end

      if event == t.streamlib.DATA then
        return false
      end
    end

    t.bind = function(self, server_addr, server_port)
      self.handle = t.streamlib.server(function(event, handle, addr, port, buffer)
        return self:stream_callback(event, handle, addr, port, buffer)
      end, server_addr, server_port, 0)
    end

    t.isbound = function(self)
      return self.bound
    end

    t.write = function(self, handle, buffer)
      if handle then
        t.streamlib.write(handle, buffer)
      end
    end

    t.close = function(self)
      if self.handle then
        t.streamlib.close(self.handle)
        self.handle = nil
      end
    end

    return t
  end
}
