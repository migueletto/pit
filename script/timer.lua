timer = pit.loadlib("libtimer")

n = 0

function timer_callback()
  pit.debug(1, "timer " .. n)
  n = n + 1
end

timer.create(timer_callback, 1000)
