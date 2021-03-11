modules.rtp = {
  new = function(source, RTSP_PORT, RTP_PORT)
    local t = {}

    t.RTSP_PORT = RTSP_PORT
    t.RTP_PORT  = RTP_PORT
    t.RTCP_PORT = RTP_PORT + 1

    t.STREAM_LIB = "libstream"
    t.streamlib = pit.loadlib(t.STREAM_LIB)

    t.net = modules.network.new(false)
    t.net:update()

    t.source = source

    t.session = {}
    t.rtsp = nil
    t.playing = 0

    math.randomseed(pit.time())

    t.rtsp_send_start = function(t)
      t.reply = ""
    end

    t.rtsp_send = function(t, line)
      -- pit.debug(1, "reply line: " .. line)
      t.reply = t.reply .. line
    end

    t.rtsp_send_finish = function(t, handle)
      if #t.reply > 0 then
        pit.debug(1, "sending reply")
        t.rtsp:write(handle, t.reply)
      end
    end

    -- function called everytimne the client sends something via RTSP
    t.rtsp_callback = function(self, event, handle, peer, buffer, data)
      -- we are interested only in LINE events
      if event ~= self.streamlib.LINE then
        return
      end

      if not data.state then
        data.state = 0
      end

      -- build function to parse the line sent by the client
      local f = string.gmatch(buffer, "([%a%d%p]+)")

      if data.state == 0 then
        -- the first line contains the method
        data.method = f()
        data.userAgent = nil
        data.cseq = nil
        data.sid = nil
        data.rtp_port  = nil
        data.rtcp_port = nil
        data.range = nil
        data.speed = nil
        data.header = nil
        data.body = nil
        pit.debug(1, "received method " .. data.method)
        data.state = 1
        return
      end

      if data.state == 1 then
        -- next lines contain header parameters
        local header = f()
        local handled = false

        if header == "User-Agent:" then
          data.userAgent = string.sub(buffer, 12)
          handled = true
        end

        if header == "CSeq:" then
          data.cseq = f()
          handled = true
        end

        if header == "Session:" then
          data.sid = f()
          handled = true
        end

        if header == "Range:" and data.method == "PLAY" then
          -- Range: npt=0.000-
          data.range = f() -- for live streams the range is ignored
          handled = true
        end

        if header == "Speed:" and data.method == "PLAY" then
          -- Speed: 0.000
          data.speed = f() -- for live streams the speed is ignored
          handled = true
        end

        if header == "Accept:" and data.method == "DESCRIBE" then
          data.accept = f()
          handled = true
        end

        if header == "Transport:" and data.method == "SETUP" then
          -- parse the Transport header, which looks like this:
          -- Transport: RTP/AVP;unicast;client_port=9044-9045
          local value = f()
          local g = string.gmatch(value, "([^;]+)")
          local arg = g()
          while arg do
            if string.sub(arg, 1, 12) == "client_port=" then
              local ports = string.sub(arg, 13)
              local h = string.gmatch(arg, "([%d]+)")
              data.rtp_port  = h()
              data.rtcp_port = h()
              pit.debug(1, "RTP  port: " .. data.rtp_port)
              pit.debug(1, "RTCP port: " .. data.rtcp_port)
              data.header = buffer .. ";server_port=" .. t.RTP_PORT .. "-" .. t.RTCP_PORT
              pit.debug(1, data.header)
              handled = true
              break
            end
            arg = g()
          end
        end

        if buffer ~= "" then
          -- an unknown header was sent, ignore it
          if not handled then
            pit.debug(1, "unhandled header: " .. buffer)
          end
        else
          -- an empty line means end of request, now process the method and parameters

          if data.method == "OPTIONS" then
            -- reply OPTIONS with methods we support
            data.header = "Public: DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE"
          end

          if data.method == "DESCRIBE" then
            -- reply DESCRIBE with media description using SDP protocol
            if data.accept == "application/sdp" then
              local session_id = math.random(1000000)
              local session_version = math.random(1000000)
              data.body = "v=0\r\n" ..
                          "o=pit " .. session_id .. " " .. session_version .. " IN IP4 " .. t.host .. "\r\n" ..
                          "s=test\r\n" ..
                          "c=IN IP4 " .. t.host .. "\r\n" ..
                          "m=video 0 RTP/AVP 26\r\n" ..
                          "a=rtpmap:26 JPEG/90000\r\n"
              data.header = "Content-Type: application/sdp\r\nContent-Length: " .. #data.body
            end
          end

          if data.method == "SETUP" then
            if data.sid then
              -- existing session
              pit.debug(1, "using session " .. data.sid)
            else
              -- new session, create new session id as a random number
              data.sid = string.format("%08x", math.random(1000000))
              pit.debug(1, "new session " .. data.sid)
            end
            if not t.session[data.sid] then
              t.session[data.sid] = {}
              t.session[data.sid].playing = false
            end
            -- stores which port numbers the client is using
            t.session[data.sid].peer_rtp_port  = data.rtp_port
            t.session[data.sid].peer_rtcp_port = data.rtcp_port
          end

          if data.method == "PLAY" and data.sid and t.session[data.sid] and not t.session[data.sid].playing then
            pit.debug(1, "play session " .. data.sid)
            if t.playing == 0 then
              -- starts the source (camera) informing the client host and port
              pit.debug(1, "starting source")
              t.source:start()
              t.source:setrtp(peer, t.session[data.sid].peer_rtp_port)
            end
            t.playing = t.playing + 1
            t.session[data.sid].playing = true
          end

          if data.method == "PAUSE" and data.sid and t.session[data.sid] and t.session[data.sid].playing then
            pit.debug(1, "pause session " .. data.sid)
            t.playing = t.playing - 1
            if t.playing == 0 then
              pit.debug(1, "stoping source")
              t.source:setrtp("", 0)
              t.source:stop()
            end
            t.session[data.sid].playing = false
          end

          if data.method == "TEARDOWN" and data.sid and t.session[data.sid] then
            pit.debug(1, "teardown session " .. data.sid)
            if t.session[data.sid].playing then
              pit.debug(1, "session is playing")
              t.playing = t.playing - 1
              if t.playing == 0 then
                pit.debug(1, "stoping source")
                t.source:setrtp("", 0)
                t.source:stop()
              end
            end
            t.session[data.sid] = nil
          end

          -- sends RTSP reply to client
          t:rtsp_send_start()
          t:rtsp_send("RTSP/1.0 200 OK\r\n")
          t:rtsp_send(string.format("CSeq: %d\r\n", data.cseq))
          if data.sid then
            t:rtsp_send(string.format("Session: %s\r\n", data.sid))
          end
          if data.header then
            t:rtsp_send(data.header .. "\r\n")
            data.header = nil
          end
          t:rtsp_send("\r\n")
          if data.body then
            t:rtsp_send(data.body)
            data.body = nil
          end
          t:rtsp_send_finish(handle)
          data.state = 0
          return
         end
      end
    end

    -- create the RTSP server
    t.open = function(self)
      if not self.rtsp then
        self.host = self.net:getaddress()
        if self.host then
          self.rtsp = modules.stream.new(self.rtsp_callback, self.streamlib)
          self.rtsp:bind(self.host, self.RTSP_PORT)
        end
      end
    end

    -- destroy the RTSP server
    t.close = function(self)
      if self.rtsp then
        self.rtsp:close()
        self.rtsp = nil
      end
      if self.playing > 0 then
        self.source:setrtp("", 0)
        self.source:stop()
        self.playing = 0
      end
    end

    return t
  end
}
