from luma.core.interface.serial import i2c
from luma.core.render import canvas
from luma.oled.device import ssd1306
from PIL import ImageFont

TEXT = "hello world"


def main() -> None:
    serial = i2c(port=1, address=0x3C)
    device = ssd1306(serial)
    font = ImageFont.load_default()

    with canvas(device) as draw:
        left, top, right, bottom = draw.textbbox((0, 0), TEXT, font=font)
        x = (device.width - (right - left)) // 2 - left
        y = (device.height - (bottom - top)) // 2 - top
        draw.text((x, y), TEXT, font=font, fill="white")

    # NOTE: luma's atexit hook clears and switches off the panel unless persist
    # is set; the SSD1306 keeps showing its RAM contents without the process.
    device.persist = True
