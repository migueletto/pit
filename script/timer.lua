timer = pit.loadlib("libtimer")

n = 1

function timer_callback()
  pit.debug(1, pit.sprintf("timer %d", n))
  n = n + 1
end

t = timer.create(timer_callback, 1000)
