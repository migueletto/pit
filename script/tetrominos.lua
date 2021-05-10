-- load libraries
timer = pit.loadlib("libtimer")
input = pit.loadlib("libinput")
display = pit.loadlib("libdisplay")
fb = pit.loadlib("libfb")

-- init display
disp = fb.create(0)
display.cls(disp)
display.font(disp, 6)

-- size of play field (including borders)
x_blocks = 12
y_blocks = 22

-- define block size and play field location
width  = display.width(disp)
height = display.height(disp)
nx = math.floor(width / x_blocks)
ny = math.floor(height / y_blocks)
side = math.min(nx, ny)
field_x = (width - x_blocks * side) / 2
field_y = (height - y_blocks * side) / 2

-- color indexes
black  = 0
gray   = 1
red    = 2
green  = 3
blue   = 4
cyan   = 5
purple = 6
yellow = 7
orange = 8

-- color values
base_colors = {}
base_colors[gray]   = { 192, 192, 192 }
base_colors[red]    = { 192, 0,   0   }
base_colors[green]  = { 0,   192, 0   }
base_colors[blue]   = { 0,   0,   192 }
base_colors[cyan]   = { 0,   192, 192 }
base_colors[purple] = { 192, 0,   192 }
base_colors[yellow] = { 192, 192, 0   }
base_colors[orange] = { 192, 128, 0   }

colors = {}
light_colors = {}
dark_colors = {}

function make_color(c)
  return display.rgb(disp, c[1], c[2], c[3])
end

function make_lighter(c)
  local r, g, b
  if c[1] > 0 then r = c[1] else r = c[1] + 63 end
  if c[2] > 0 then g = c[2] else g = c[2] + 63 end
  if c[3] > 0 then b = c[3] else b = c[3] + 63 end
  return make_color({ r, g, b })
end

function make_darker(c)
  local r, g, b
  if c[1] > 0 then r = c[1] - 63 else r = 0 end
  if c[2] > 0 then g = c[2] - 63 else g = 0 end
  if c[3] > 0 then b = c[3] - 63 else b = 0 end
  return make_color({ r, g, b })
end

colors[black] = make_color({0, 0, 0 })

-- create light and dark versions for colors
for i,c in ipairs(base_colors) do
  colors[i] = make_color(c)
  light_colors[i] = make_lighter(c)
  dark_colors[i] = make_darker(c)
end

-- define the 7 tetromino shapes

i_block = {
  color = cyan,
  shape = { { 1, 1, 1, 1 } },
  dx = { 0, -2, 0, -2 },
  dy = { -2, 0, -2, 0 }
}

j_block = {
  color = blue,
  shape = { { 1, 1, 1 }, { 1, 0, 0 } },
  dx = { 0, -1, 0, -1 },
  dy = { -1, 0, -1, 0 }
}

l_block = {
  color = orange,
  shape = { { 1, 1, 1 }, { 0, 0, 1 } },
  dx = { 0, -1, 0, -1 },
  dy = { -1, 0, -1, 0 }
}

o_block = {
  color = yellow,
  shape = { { 1, 1 }, { 1, 1 } },
  dx = { 0, 0, 0, 0 },
  dy = { 0, 0, 0, 0 }
}

s_block = {
  color = green,
  shape = { { 1, 1, 0 }, { 0, 1, 1 } },
  dx = { 0, -1, 0, -1 },
  dy = { -1, 0, -1, 0 }
}

t_block = {
  color = purple,
  shape = { { 1, 1, 1 }, { 0, 1, 0 } },
  dx = { 0, -1, 0, -1 },
  dy = { -1, 0, -1, 0 }
}

z_block = {
  color = red,
  shape = { { 0, 1, 1 }, { 1, 1, 0 } },
  dx = { 0, -1, 0, -1 },
  dy = { -1, 0, -1, 0 }
}

shapes = { i_block, j_block, l_block, o_block, s_block, t_block, z_block }

-- draw one block at column i, row j with color c
function draw_block(i, j, c)
  if i >= 1 and i <= x_blocks and j >= 1 and j <= y_blocks then
    local x = field_x + (i-1) * side
    local y = field_y + (j-1) * side
    field[j][i] = c

    -- main color
    display.color(disp, colors[c], colors[black])
    display.box(disp, x+2, y+2, x+side-3, y+side-3)

    -- light color for upper and left borders
    display.color(disp, light_colors[c], colors[black])
    display.box(disp, x, y, x+side-1, y+1)
    display.box(disp, x, y, x+1, y+side-1)

    -- dark color for bottom and right borders
    display.color(disp, dark_colors[c], colors[black])
    display.box(disp, x, y+side-2, x+side-1, y+side-1)
    display.box(disp, x+side-2, y, x+side-1, y+side-1)
  end
end

-- draw field border with gray blocks
function draw_border()
  for i=1, x_blocks do
    draw_block(i, y_blocks, 1)
  end

  for i=1, y_blocks do
    draw_block(1, i, 1)
    draw_block(x_blocks, i, 1)
  end
end

-- print updated score
function update_score(d)
  score = score + d
  local s = string.format("%05d", score)
  display.color(disp, colors[yellow], colors[black])
  display.print(disp, field_x + side + 10 + 7*display.fwidth(disp), field_y, s)
end

-- reset the play field and the score
function reset_field()
  display.color(disp, colors[black], colors[black])
  display.box(disp, field_x + side, field_y, field_x + side * (x_blocks - 1), field_y + side * (y_blocks - 1))
  field = {}
  for j = 1,y_blocks do
    field[j] = {}
    for i = 1,x_blocks do
      field[j][i] = black
    end
  end
  draw_border()
  display.color(disp, colors[yellow], colors[black])
  display.print(disp, field_x + side + 10, field_y, "SCORE: ")
  score = 0
  update_score(0)
end

function draw_shape_block(i, j, c)
  if i > 1 and i < x_blocks and j > 1 and j < y_blocks then
    draw_block(i, j, c)
  end
end

-- given a shape index, position and rotation, enumerate each block
-- and call the cb function to perform the actual action
function enumerate_shape(s, x, y, rotation, cb)
  local nrows = #shapes[s].shape
  local dx = shapes[s].dx[rotation]
  local dy = shapes[s].dy[rotation]
  local r = 0

  if rotation == 1 then
    for i = 1,nrows do
      local ncols = #shapes[s].shape[i]
      for j = 1,ncols do
        r = r + cb(s, shapes[s].shape[i][j], dx+x+i-1, dy+y+j-1, c)
      end
    end
    return r
  end

  if rotation == 2 then
    for i = 1,nrows do
      local ncols = #shapes[s].shape[nrows - i + 1]
      for j = 1,ncols do
        r = r + cb(s, shapes[s].shape[nrows - i + 1][j], dx+x+j-1, dy+y+i-1)
      end
    end
    return r
  end

  if rotation == 3 then
    for i = 1,nrows do
      local ncols = #shapes[s].shape[nrows - i + 1]
      for j = 1,ncols do
        r = r + cb(s, shapes[s].shape[nrows - i + 1][ncols - j + 1], dx+x+i-1, dy+y+j-1)
      end
    end
    return r
  end

  if rotation == 4 then
    for i = 1,nrows do
      local ncols = #shapes[s].shape[i]
      for j = 1,ncols do
        r = r + cb(s, shapes[s].shape[i][ncols - j + 1], dx+x+j-1, dy+y+i-1)
      end
    end
    return r
  end

  return 0
end

-- draw shape s at position x,y with a rotation
function draw_shape(s, x, y, rotation)
  enumerate_shape(s, x, y, rotation,
    function(s, p, x, y)
      if p == 1 then
        draw_shape_block(x, y, shapes[s].color)
      end
      return 0
    end
  )
end

-- erase shape s at position x,y with a rotation
function erase_shape(s, x, y, rotation)
  enumerate_shape(s, x, y, rotation,
    function(s, p, x, y)
      if p == 1 then
        draw_shape_block(x, y, black)
      end
      return 0
    end
  )
end

-- return true if a shape would collide with any block
function check_shape(s, x, y, rotation)
  local check = enumerate_shape(s, x, y, rotation,
    function(s, p, x, y)
      local r = 0
      if p == 1 and field[y] then
        r = field[y][x]
      end
      return r
    end
  ) > 0
  return check
end

-- drop a falling shape into the bottom
function drop_shape()
  while true do
    erase_shape(shape, shape_x, shape_y, shape_r)
    if check_shape(shape, shape_x, shape_y+1, shape_r) then
      draw_shape(shape, shape_x, shape_y, shape_r)
      check_lines()
      state = 1
      break
    else
      shape_y = shape_y + 1
      draw_shape(shape, shape_x, shape_y, shape_r)
    end
    pit.usleep(10000)
  end
end

-- called when there is an input event
function input_callback(ev, x, y)
  if ev == input.down then
    -- event is pen down
    if state == 3 then
      -- if state is game over, reset and play again
      reset_field()
      state = 1
      return
    end

    t0 = pit.clock()
    down_x = x
    down_y = y
    move_x = x
    move_y = y
    down = true
    drop = false

  elseif ev == input.up then
    -- event is pen up

    if state == 2 then
      if drop then
        drop_shape()
        drop = false
      elseif (pit.clock() - t0) < 100000 then
        if math.abs(x - down_x) < 3 and math.abs(y - down_y) < 3 then
          -- a fast tap on the screen: rotate current shape
          erase_shape(shape, shape_x, shape_y, shape_r)
          local aux = shape_r + 1
          if aux > 4 then aux = 1 end
          if not check_shape(shape, shape_x, shape_y, aux) then
            -- rotate only if shape will mot collide with border or blocks on the field
            shape_r = aux
          end
          draw_shape(shape, shape_x, shape_y, shape_r)
        end
      end
    end
    down = false

  elseif ev == input.motion and down and state == 2 then
    -- event is pen movement

    local dx = x - move_x
    local dy = y - move_y
    local incr = 0

    if dy > 10 then
      -- top to bottom movement: drop the current block
      drop = true
      move_x = x
      move_y = y
    elseif dx > 20 then
      -- left to right movement: move shape to the right
      incr = 1
      move_x = x
      move_y = y
    elseif dx < -20 then
      -- right to left movement: move shape to the left
      incr = -1
      move_x = x
      move_y = y
    end

    if incr ~= 0 then
      -- draw the shape at the new left ot right position
      erase_shape(shape, shape_x, shape_y, shape_r)
      local new_x = shape_x + incr
      if not check_shape(shape, new_x, shape_y, shape_r) then
        shape_x = new_x
      end
      draw_shape(shape, shape_x, shape_y, shape_r)
    end
  end
end

-- redraw the field
function draw_field(from, to)
  for j = from,to,-1 do
    for i = 2,x_blocks-1 do
     draw_shape_block(i, j, field[j][i])
    end
  end
end

-- print the field on the log (for debugging)
function print_field()
  for j = 1,y_blocks do
    local s = string.format("%2d: ", j)
    for i = 1,x_blocks do
      s = s .. tostring(field[j][i])
    end
    pit.debug(2, s)
  end
end

-- check if rows are full and must be collapsed
function check_lines()
  -- start at the bottom of the field
  local j = y_blocks-1
  local points = 1
  while true do
    -- count how many blocks are present on a row
    local t = 0
    for i = 2,x_blocks-1 do
      if field[j][i] ~= black then
        t = t + 1
      end
    end
    pit.debug(2, string.format("line %d: %d blocks", j, t))
    if t == 0 then
      -- empty row: leave
      pit.debug(2, string.format("line %d: empty", j))
      break
    end
    if t == x_blocks-2 then
      -- full row: collapse field
      pit.debug(2, string.format("line %d: full", j))
      print_field()
      for k = j,2,-1 do
        field[k] = field[k-1]
      end
      field[1] = {}
      field[1][1] = gray
      field[1][x_blocks] = gray
      for k = 2,x_blocks-1 do
        field[1][k] = black
      end
      -- redraw field
      pit.debug(2, "redraw field")
      print_field()
      draw_field(j, 2)
      update_score(points)
      points = points * 2
    else
      -- partially filled row: check next
      pit.debug(2, string.format("line %d: next", j))
      j = j -1
      points = 1
    end
    if j == 1 then
      -- reached top of the field: leave
      pit.debug(2, "field done")
      break
    end
  end
end

-- timer callback
function timer_callback()
  if state == 1 then
    -- a new shape must fall from the top
    shape = math.random(7)
    shape_x = math.floor(x_blocks / 2)
    shape_y = 1
    shape_r = math.random(4)

    if check_shape(shape, shape_x, shape_y, shape_r) then
      -- if there is no space to place a new shape, game over
      pit.debug(2, "game over")
      print_field()
      local fx = 11*display.fwidth(disp)
      local fy = display.fheight(disp)
      local x = (width - fx) / 2
      local y = (height - fy) / 2
      display.color(disp, colors[black], colors[black])
      display.box(disp, x-1, y-fy/2, x+fx+1, y+fy+fy/2)
      display.color(disp, colors[red], colors[black])
      display.rect(disp, x-1, y-fy/2, x+fx+1, y+fy+fy/2)
      display.print(disp, x, y, " GAME OVER ")
      state = 3
    else
      state = 2
    end
  end

  if state == 2 then
    -- normal game play
    erase_shape(shape, shape_x, shape_y, shape_r)
    if check_shape(shape, shape_x, shape_y+1, shape_r) then
      -- the falling shape collided with the field: check full rows
      draw_shape(shape, shape_x, shape_y, shape_r)
      pit.debug(2, "collision")
      print_field()
      check_lines()
      state = 1
    else
      -- move shape one position down
      shape_y = shape_y + 1
      draw_shape(shape, shape_x, shape_y, shape_r)
    end
  end
end

-- cleanup function called before program exits
function cleanup_callback()
  if disp then
    display.destroy(disp)
  end
  if inp then
    input.destroy(inp)
  end
end

-- register a cleanup function
pit.cleanup(cleanup_callback)

math.randomseed(pit.time())
reset_field()
state = 1

-- create an input handler
inp = input.create(0, width, height, input_callback)

-- start the game (tick period is 300 ms)
t = timer.create(timer_callback, 300)
