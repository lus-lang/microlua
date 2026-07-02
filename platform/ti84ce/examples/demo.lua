-- MicroLua CE demo: bouncing ball on the graphx screen. clear exits.
gfx.begin()
local x, y, r = 160, 120, 10
local dx, dy = 3, 2
local frames = 0
while true do
  key.scan()
  if key.isDown(key.clear) then
    break
  end
  gfx.fill(255) -- white (default xlibc palette)
  gfx.color(224) -- blue
  gfx.fillCircle(x, y, r)
  gfx.color(16) -- red
  gfx.rect(0, 0, 320, 240)
  gfx.textColor(0, 255)
  gfx.text("MicroLua on the TI-84+ CE", 8, 8)
  gfx.swap()
  x = x + dx
  y = y + dy
  if x < r or x > 320 - r then
    dx = -dx
  end
  if y < r or y > 240 - r then
    dy = -dy
  end
  frames = frames + 1
end
gfx.finish()
print("frames drawn:", frames)
