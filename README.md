# ZMK Driver for Azoteq IQS5XX Touchpads

## Compatibility

This driver should work with any IQS5XX-based touchpad (TPS43 or TPS65).

## Supported Features

- Touchpad movement.
- Single tap: registered as a left click.
- Two-finger tap: registered as a right click.
- Press and hold: registered as a continuous left click (dragging).
- Vertical scrolling.
- Horizontal scrolling.

## Usage

- In the configuration file (of the relevant half), set `CONFIG_INPUT_TPS43` to enable the driver

```
CONFIG_INPUT_TPS43=y
```

- In the `.overlay` file, add `compatible = "azoteq,tps43"` inside the i2c node where the touchpad will be used (see the example below).

> For full information on the touchpad configuration, see the file: [available touchpad settings](./dts/bindings/input/azoteq,tps43-common.yaml)

```
&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_FAST>;
    pinctrl-0 = <&i2c0_default>;  /* configuration for SDA and SCL */
    pinctrl-1 = <&i2c0_sleep>;    /* configuration for SDA and SCL */
    pinctrl-names = "default", "sleep";

    tps43_trackpad: trackpad@74 {
        compatible = "azoteq,tps43";
        reg = <0x74>;
        status = "okay";
        
        /* GPIO connections */
        rdy-gpios = <&pro_micro 21 GPIO_ACTIVE_HIGH>;  /* RDY pin */
        rst-gpios = <&pro_micro 20 GPIO_ACTIVE_HIGH>;  /* RST pin */

        enable-power-management;
        
        sensitivity = <110>;           /* 100% = normal state */
        scroll-sensitivity = <10>;     /* 50% = normal state */

        filter-settings=<0x0B>;        /* see the filter description in the `available settings` */

        scroll;
        two-finger-tap;
        single-tap;
        press-and-hold;
        swipes;

        switch-xy;
        invert-scroll-y;
    };
};
```

- Now you need to add a listener to track touches:

> It is important here that if your touchpad `.overlay` configuration is on the central device, use `Option 1`. If the touchpad is located on the peripheral part of the keyboard, use `Option 2`

---
**Option 1**

It is enough to specify the listener on the central device itself.

```
/ {
    tps43_input: tps43_input {
        compatible = "zmk,input-listener";
        device = <&tps43_trackpad>;
    };
};
```

---

... Otherwise ...

---

**Option 2**

Add `split_inputs` where the touchpad is used (on the peripheral part of the keyboard)

```
/ {
    split_inputs {
        #address-cells = <1>;
        #size-cells = <0>;

        tps43_split: tps43_split@0 {
            compatible = "zmk,input-split";
            reg = <0>;
            device = <&tps43_trackpad>;
        };
    };
};
```

Now, on the central part, you need to specify a listener but without specifying `device` (device is specified where the touchpad is used - on the peripheral part)

```
/ {
    split_inputs {
        #address-cells = <1>;
        #size-cells = <0>;

        tps43_split: tps43_split@0 {
            compatible = "zmk,input-split";
            reg = <0>;
            /* There is no device property here - this is a proxy on the central side */
        };
    };

    tps43_listener: tps43_listener {
        compatible = "zmk,input-listener";
        device = <&tps43_split>;
        status = "okay";
    };
};
```
---


> Configuring the Azoteq touchpad requires 5 pins!

Power:
3V on nice!nano -> VDD on IQS5xx.
GNG (Ground) on nice!nano -> GND on IQS5xx.

I2C signals:
SDA on nice!nano -> SDA on IQS5xx.
SCL on nice!nano -> SCL on IQS5xx.

The "DR" or "RDY" pin on IQS5xx -> Any available GPIO on nice!nano. In devicetree this pin is specified as rdy-gpios.
The "RST" pin is used to initialize a device reset. In devicetree this pin is specified as rst-gpios.


## Correct Driver Operation Sequence

```txt
1. Power on / Hardware reset
   └─> Wait 10ms
   └─> RST: LOW (10ms) → HIGH
   └─> Wait ~600ms for the firmware to load

2. Check the SHOW_RESET flag (0x000F bit 0)
   └─> Poll until the flag appears

3. Acknowledge the reset
   └─> Write ACK_RESET (0x0431 = 0x80)

4. Device configuration
   └─> System Config 1 (0x058F) - event modes
   └─> XY Config (0x0669) - axis settings
   └─> Filter, gesture, etc. settings

5. Finish configuration
   └─> Write SETUP_COMPLETE (0x058E = 0x40)

6. Configure the GPIO interrupt (RDY)
   └─> AFTER full configuration

```

## Power Management

The driver supports two independent power management mechanisms to reduce power consumption:

### Integration with the ZMK Power Management System

The touchpad automatically enters sleep mode when the keyboard transitions to the idle/sleep state.

**How it works:**
- The `tps43_idle_sleeper.c` module subscribes to ZMK `zmk_activity_state_changed` events
- When ZMK transitions to the `IDLE` or `SLEEP` state, the touchpad is put into sleep
- When returning to the `ACTIVE` state, the touchpads wake up

**ZMK States:**
- `ZMK_ACTIVITY_ACTIVE` - the keyboard is active, the touchpad is working
- `ZMK_ACTIVITY_IDLE` - the keyboard is idle, the touchpad is in sleep
- `ZMK_ACTIVITY_SLEEP` - the keyboard is in sleep mode, the touchpad is in sleep

**Important:** This mechanism only works if `enable-power-management` is enabled.

### Technical Details

**Control Register:**
- Suspend mode is controlled via the `SYSTEM_CONTROL_1` register (0x0432)
- The `TPS43_SUSPEND` bit (BIT(1)) is set to enter suspend
- In suspend mode the touchpad consumes minimal power and does not process touches

**Wakeup:**
- Automatic wakeup occurs when activity is detected via the RDY interrupt
- On wakeup the touchpad automatically processes the first touch

> Without `enable-power-management`, power management is completely disabled

