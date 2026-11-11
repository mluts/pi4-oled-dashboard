# pi-ssd1306-dashboard

A small OLED dashboard for a Raspberry Pi, built on [luma.oled](https://github.com/rm-hull/luma.oled) and managed with [uv](https://docs.astral.sh/uv/).

For now it only draws "hello world".

## Hardware

- Raspberry Pi 4 Model B, Raspberry Pi OS trixie (64-bit)
- SSD1306 OLED, 128x64, on I2C bus 1 at address `0x3C`

| SSD1306 | Pi header         |
|---------|-------------------|
| VCC     | pin 1 (3V3)       |
| GND     | pin 6 (GND)       |
| SDA     | pin 3 (GPIO2/SDA) |
| SCL     | pin 5 (GPIO3/SCL) |

## One-time Pi setup

Enable I2C and check that the panel answers at `3c`:

```sh
sudo raspi-config nonint do_i2c 0
sudo apt install -y i2c-tools
i2cdetect -y 1
```

Install uv, then open a new shell so `uv` is on `PATH`:

```sh
curl -LsSf https://astral.sh/uv/install.sh | sh
```

Clone the repository. This needs a GitHub SSH key on the Pi:

```sh
git clone git@github.com:mluts/pi4-oled-dashboard.git
cd pi4-oled-dashboard
```

## Run

```sh
uv run hello
```

On the first run, uv creates `.venv` using the system Python 3.13 and installs the locked dependencies. You don't need to install anything separately.

On a machine without `/dev/i2c-1`, such as a Mac, the command fails with `DeviceNotFoundError` and a non-zero exit status.

## Dev loop

1. Edit on the dev machine. `uv sync` there gives the editor the dependencies.
2. `git push`
3. On the Pi, run:

   ```sh
   git pull
   uv run hello
   ```

## Notes

- The text stays on the panel after `hello` exits. The SSD1306 keeps showing its own RAM until the next draw or until it loses power.
- The Pi 4 can keep the 3.3 V rail powered after `sudo shutdown -h`. If it does, the text stays visible on a halted Pi.
- Static content left on an OLED for days can cause burn-in.
