#pragma once

#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C address of TPS43 */
#define TPS43_I2C_ADDR              0x74

/* TPS43 controller register definitions with IQS572 - Based on the IQS5xx-B000 datasheet */
/* https://www.azoteq.com/images/stories/pdf/iqs5xx-b000_trackpad_datasheet.pdf */

/* Gesture events */
// Read only
#define TPS43_REG_GESTURE_EVENTS_0  0x000D  /* 1 byte */
#define TPS43_REG_GESTURE_EVENTS_1  0x000E  /* 1 byte */

/* System information */
// Read only
#define TPS43_REG_SYSTEM_INFO_0     0x000F  /* 1 byte - Status flags */
#define TPS43_REG_SYSTEM_INFO_1     0x0010  /* 1 byte - Status flags */


/* System control and configuration */
// Read-write
#define TPS43_REG_SYSTEM_CONTROL_0  0x0431  /* 1 byte - Main control (ACK_RESET, AUTO_ATI) */
#define TPS43_REG_SYSTEM_CONTROL_1  0x0432  /* 1 byte - Event mode control */
// Read-write
#define TPS43_REG_SYSTEM_CONFIG_0   0x058E  /* 1 byte - System configuration */
#define TPS43_REG_SYSTEM_CONFIG_1   0x058F  /* 1 byte - Additional configuration */

/* Touch data - main coordinates */
// Read only
#define TPS43_REG_NUM_FINGERS       0x0011  /* 1 byte */
#define TPS43_REG_REL_X             0x0012  /* 2 bytes - relative movement along X */
#define TPS43_REG_REL_Y             0x0014  /* 2 bytes - relative movement along Y */
#define TPS43_REG_ABS_X             0x0016  /* 2 bytes - absolute X position */
#define TPS43_REG_ABS_Y             0x0018  /* 2 bytes - absolute Y position */
#define TPS43_REG_TOUCH_STRENGTH    0x001A  /* 2 bytes */
#define TPS43_REG_TOUCH_AREA        0x001C  /* 1 byte */

/* XY configuration */
// Read-write
#define TPS43_REG_XY_CONFIG_0       0x0669  /* 1 byte */

/* Gesture configuration */
// Read-write // Low-level gesture configuration // probably should be removed
#define TPS43_REG_SINGLE_FINGER_GESTURES 0x06B7  /* 1 byte */
#define TPS43_REG_MULTI_FINGER_GESTURES  0x06B8  /* 1 byte */


/* Filter settings */
// Read-write
#define TPS43_REG_FILTER_SETTINGS   0x0632  /* 1 byte */

/* Report rate, value in milliseconds */
// Read-write // addresses per the IQS5xx-B000 memory map
#define TPS43_REG_REPORT_RATE_ACTIVE      0x057A  /* 2 bytes */
#define TPS43_REG_REPORT_RATE_IDLE_TOUCH  0x057C  /* 2 bytes */
#define TPS43_REG_REPORT_RATE_IDLE        0x057E  /* 2 bytes */

/* XY resolution — hardware scaling of coordinates */
// Read-write
#define TPS43_REG_X_RESOLUTION            0x066E  /* 2 bytes */
#define TPS43_REG_Y_RESOLUTION            0x0670  /* 2 bytes */


/* ============================================================ */
/* System information 0 (0x000F) - Status flags - 8 bits */
/* ============================================================ */

#define TPS43_SHOW_RESET            BIT(0)      /* Flag check on system reset */

/* ============================================================ */
/* System information 1 (0x0010) - Status flags - 8 bits */
/* ============================================================ */

#define TPS43_SWITCH_STATE          BIT(5)      /* State of the SW_IN pin */
#define TPS43_SNAP_TOGGLE           BIT(4)      /* Snap channel toggle */
#define TPS43_RR_MISSED             BIT(3)      /* Report rate missed */
#define TPS43_TOO_MANY_FINGERS      BIT(2)      /* Maximum number of touch points exceeded */
#define TPS43_PALM_DETECT           BIT(1)      /* Palm detected */
#define TPS43_TP_MOVEMENT           BIT(0)      /* Trackpad movement */


/* ============================================================ */
/* System control 0 (0x0431) - 8 bits */
/* ============================================================ */

#define TPS43_ACK_RESET             BIT(7)      /* Reset acknowledgment */

/* ============================================================ */
/* System control 1 (0x0432) - 8 bits */
/* ============================================================ */

#define TPS43_RESET                 BIT(1)
#define TPS43_SUSPEND               BIT(0)      /* Sleep mode */


/* ============================================================ */
/* System configuration 0 (0x058E) - 8 bits */
/* ============================================================ */

#define TPS43_SETUP_COMPLETE        BIT(6)      /* Setup complete flag */
#define TPS43_WDT_ENABLE            BIT(5)      /* Enable watchdog timer */


/* ============================================================ */
/* System configuration 1 (0x058F) - 8 bits */
/* ============================================================ */

#define TPS43_EVENT_MODE            BIT(0)
#define TPS43_GESTURE_EVENT         BIT(1)
#define TPS43_TP_EVENT              BIT(2)
#define TPS43_TOUCH_EVENT           BIT(6)


/* ============================================================ */
/* XY configuration 0 (0x0669) - 8 bits */
/* ============================================================ */

#define TPS43_FLIP_X                BIT(0)      /* Flip the X axis */
#define TPS43_FLIP_Y                BIT(1)      /* Flip the Y axis */
#define TPS43_SWITCH_XY_AXIS        BIT(2)      /* Swap the X and Y axes */


/* ============================================================ */
/* Gesture events 0 (0x000D) - 8 bits */
/* ============================================================ */

#define TPS43_SINGLE_TAP            BIT(0)      /* Single-finger tap detected */
#define TPS43_PRESS_AND_HOLD        BIT(1)      /* Press and hold detected */
#define TPS43_SWIPE_UP              BIT(2)      /* Single-finger swipe up */
#define TPS43_SWIPE_DOWN            BIT(3)      /* Single-finger swipe down */
#define TPS43_SWIPE_LEFT            BIT(4)      /* Single-finger swipe left */
#define TPS43_SWIPE_RIGHT           BIT(5)      /* Single-finger swipe right */

/* ============================================================ */
/* Gesture events 1 (0x000E) - 8 bits */
/* ============================================================ */

#define TPS43_TWO_FINGER_TAP        BIT(0)      /* Two-finger tap */
#define TPS43_SCROLL                BIT(1)      /* Scroll up */
#define TPS43_ZOOM                  BIT(2)      /* Zoom gesture */

/* ============================================================ */
/* Filter settings 0 (0x0632) - 8 bits */
/* ============================================================ */

#define TPS43_IIR_FILTER           BIT(0)      /* IIR filter */
#define TPS43_MAV_FILTER           BIT(1)      /* MAV filter */
#define TPS43_IIR_SELECT           BIT(2)      /* IIR selection */
#define TPS43_ALP_COUNT_FILTER     BIT(3)      /* ALP count filter */


/* End of communication window - MUST be called after every read (writing to the invalid address 0xEEEE causes a NACK) */
#define TPS43_REG_END_COMM_WINDOW   0xEEEE

struct tps43_config {
    struct i2c_dt_spec i2c_bus;
    struct gpio_dt_spec rdy_gpio;
    struct gpio_dt_spec rst_gpio;

    bool single_tap;
    bool press_and_hold;
    bool two_finger_tap;
    bool scroll;
    bool swipes;            
    bool invert_x;
    bool invert_y;
    bool switch_xy;
    bool invert_scroll_x;
    bool invert_scroll_y;

    int16_t sensitivity;
    int16_t scroll_sensitivity;

    bool enable_power_management;

    uint8_t filter_settings;

    uint16_t report_rate_ms;
    uint16_t x_resolution;
    uint16_t y_resolution;
};

struct tps43_drv_data {
    const struct device *dev;
    struct k_sem lock;
    struct gpio_callback rdy_cb;
    struct k_work work;

    bool device_ready;
    bool initialized;
    bool scroll_active;
    bool drag_active;
    bool suspended;         
};

int tps43_set_sleep(const struct device *dev, bool sleep);

#ifdef __cplusplus
}
#endif

