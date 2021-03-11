-- network module: a lua wrapper for libinet libray

modules.network = {
  new = function(desktop)
    local t = {}

    t.INET_LIB = "libinet"
    t.inetlib = pit.loadlib(t.INET_LIB)

    t.interface = nil
    t.address = nil
    t.broadcast = nil
    t.status = -1

    t.update = function(t)
      local aux = 0
      local old_interface = t.interface
      local old_address = t.address
      t.interface = nil
      t.address = nil
      t.broadcast = nil

      if t.inetlib then
        local ifs = t.inetlib.list()
        if ifs then
          local lo = false
          for _,ifname in ipairs(ifs) do
            if ifname == "lo" then
              lo = true
            elseif aux <= 1 and ifname ~= "bnep" and ifname ~= "bnep0" then
              aux = 1
              t.address = t.inetlib.ip(ifname)
              if t.address then
                t.broadcast = t.inetlib.broadcast(ifname)
                t.interface = ifname
                aux = 2
              end
            end
          end
          if not t.address and lo and desktop then
            ifname = "lo"
            aux = 1
            t.address = t.inetlib.ip(ifname)
            if t.address then
              t.broadcast = t.inetlib.broadcast(ifname)
              t.interface = ifname
              aux = 2
            end
          end
        end
      end

      if aux == 0 then
        pit.debug(1, "no inet interface found")
      end

      if aux ~= t.status or old_interface ~= t.interface or old_address ~= t.address then
        t.status = aux
        pit.debug(1, "network status is " .. t.status)

        if t.status >= 1 then
          pit.debug(1, "network name " .. t.interface)
        end
        if t.status == 2 then
          pit.debug(1, "network address   " .. t.address)
          pit.debug(1, "network broadcast " .. t.broadcast)
        end
      end
    end

    t.getinterface = function(t)
      return t.interface
    end

    t.getaddress = function(t)
      return t.address
    end

    t.getbroadcast = function(t)
      return t.broadcast
    end

    t.getstatus = function(t)
      return t.status
    end

    return t
  end
}
