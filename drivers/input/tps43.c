#include <stdint.h>
#define DT_DRV_COMPAT azoteq_tps43

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <errno.h>

#include "tps43.h"

LOG_MODULE_REGISTER(tps43, CONFIG_INPUT_LOG_LEVEL);
 
/**
 * @brief Closes the communication window with the touchpad
 * 
 * After each read of the touchpad registers the communication window must be closed,
 * by writing the special address 0xEEEE, which triggers a NACK from the device.
 * This is a mandatory step according to the IQS5xx protocol.
 * 
 * @param dev Pointer to the touchpad device
 */
static void tps43_end_communication_window(const struct device *dev) {
    const struct tps43_config *config = dev->config;
    uint8_t end_buf[2];

    sys_put_be16(TPS43_REG_END_COMM_WINDOW, end_buf);

    int ret = i2c_write_dt(&config->i2c_bus, end_buf, sizeof(end_buf));
    if (ret != 0 && ret != -EIO) {
        LOG_INF("Communication window close write returned: %d (NACK expected)", ret);
    }
}

/**
 * @brief Reads a sequence of touchpad registers
 * 
 * Reads several bytes from consecutive touchpad registers,
 * starting at the specified address. Used to read related registers,
 * such as gesture events (GESTURE_EVENTS_0 and GESTURE_EVENTS_1).
 * 
 * @param dev Pointer to the touchpad device
 * @param reg Address of the starting register (16-bit)
 * @param val Pointer to the data buffer
 * @param len Number of bytes to read
 * @return 0 on success, negative error code on failure
 */
static int read_sequence_registers(const struct device *dev, uint16_t reg, void *val, size_t len) {
    const struct tps43_config *config = dev->config;
    uint8_t addr_buf[2];
    addr_buf[0] = (uint8_t)((reg >> 8) & 0xFF);
    addr_buf[1] = (uint8_t)(reg & 0xFF);

    return i2c_write_read_dt(&config->i2c_bus, addr_buf, 2, val, len);
}

/**
 * @brief Reads a 16-bit touchpad register over I2C
 * 
 * Reads a 16-bit value from the specified touchpad register.
 * The data is interpreted as big-endian (MSB first).
 * 
 * @param dev Pointer to the touchpad device
 * @param reg Register address (16-bit)
 * @param val Pointer to the variable that stores the read value
 * @return 0 on success, negative error code on failure
 */
static int tps43_i2c_read_reg16(const struct device *dev, uint16_t reg, uint16_t *val)
{
    const struct tps43_config *config = dev->config;
    uint8_t buf[2];
    // builds the 2-byte register address: (MSB, LSB)
    // MSB: shift right by 8 bits (0x2F00 -> 0x2F)
    // LSB: bitwise AND with a mask - the mask keeps only the low byte (0x2F00 -> 0x00)
    uint8_t reg_buf[2] = {reg >> 8, reg & 0xFF};
    int ret;
    
    // writes the register address (reg_buf) and reads 2 bytes of data (into the buf buffer)
    ret = i2c_write_read_dt(&config->i2c_bus, reg_buf, sizeof(reg_buf), buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("Register read error 0x%04x: %d", reg, ret);
        return ret;
    }
    
    // converts the big-endian data (MSB first) back into a 16-bit value
    *val = (buf[0] << 8) | buf[1];
    return 0;
}

/**
 * @brief Writes a 16-bit value to a touchpad register over I2C
 * 
 * Writes a 16-bit value to the specified touchpad register.
 * The data is transmitted as big-endian (MSB first).
 * 
 * @param dev Pointer to the touchpad device
 * @param reg Register address (16-bit)
 * @param val Value to write (16-bit)
 * @return 0 on success, negative error code on failure
 */
static int tps43_i2c_write_reg16(const struct device *dev, uint16_t reg, uint16_t val)
{
    const struct tps43_config *config = dev->config;
    // builds the 4-byte register address: (MSB, LSB, MSB_VALUE, LSB_VALUE)
    uint8_t buf[4] = {reg >> 8, reg & 0xFF, val >> 8, val & 0xFF};
    int ret;
    
    ret = i2c_write_dt(&config->i2c_bus, buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("Register write error 0x%04x: %d", reg, ret);
        return ret;
    }
    
    return 0;
}

/**
 * @brief Reads an 8-bit touchpad register over I2C
 * 
 * Reads an 8-bit value from the specified touchpad register.
 * Used to read most configuration and status registers.
 * 
 * @param dev Pointer to the touchpad device
 * @param reg Register address (16-bit)
 * @param val Pointer to the variable that stores the read value
 * @param with_err Flag indicating whether to log an error or expected behavior
 * @return 0 on success, negative error code on failure
 */
static int tps43_i2c_read_reg8_w_err(const struct device *dev, uint16_t reg, uint8_t *val, bool with_err)
{
    const struct tps43_config *config = dev->config;
    // builds the 2-byte register address: (MSB, LSB)
    uint8_t reg_buf[2] = {reg >> 8, reg & 0xFF};
    int ret;
    
    ret = i2c_write_read_dt(&config->i2c_bus, reg_buf, sizeof(reg_buf), val, 1);
    if (ret != 0) {
        if (!with_err) {
            LOG_INF("Expected end of register read 0x%04x: %d", reg, ret);
        } else {
            LOG_ERR("Register read error 0x%04x: %d", reg, ret);
        }
        return ret;
    }
}

static inline int tps43_i2c_read_reg8(const struct device *dev, uint16_t reg, uint8_t *val)
{
    return tps43_i2c_read_reg8_w_err(dev, reg, val, true);
}

/**
 * @brief Writes an 8-bit value to a touchpad register over I2C
 * 
 * Writes an 8-bit value to the specified touchpad register.
 * Used to write configuration and control registers.
 * 
 * @param dev Pointer to the touchpad device
 * @param reg Register address (16-bit)
 * @param val Value to write (8-bit)
 * @return 0 on success, negative error code on failure
 */
static int tps43_i2c_write_reg8(const struct device *dev, uint16_t reg, uint8_t val)
{
    const struct tps43_config *config = dev->config;
    uint8_t buf[3] = {reg >> 8, reg & 0xFF, val};
    int ret;
    
    ret = i2c_write_dt(&config->i2c_bus, buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("Register write error 0x%04x: %d", reg, ret);
        return ret;
    }
    
    return 0;
}

/**
 * @brief Callback interrupt handler for the touchpad RDY pin
 * 
 * Called when the state of the touchpad RDY (Ready) pin changes,
 * which signals that new data is available to read.
 * Schedules execution of the work handler to read the data.
 * 
 * @param dev Pointer to the touchpad device
 * @param cb Pointer to the GPIO callback structure
 * @param pins Mask of the pins that triggered the interrupt
 */
 static void tps43_rdy_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
     struct tps43_drv_data *drv_data = CONTAINER_OF(cb, struct tps43_drv_data, rdy_cb);
 
     k_work_submit(&drv_data->work);
 }
 
/**
 * @brief Internal function for switching the touchpad into suspend/resume mode
 * 
 * Controls the SYSTEM_CONTROL_1 register (0x0432), setting or clearing the SUSPEND bit.
 * In suspend mode the touchpad enters a low-power state and does not process
 * touches until it wakes up.
 * 
 * @param dev Pointer to the touchpad device
 * @param suspend true - enter suspend, false - exit suspend
 * @param lock_held true if the semaphore is already held (for internal use)
 * @return 0 on success, negative error code on failure
 */
static int tps43_set_suspend_internal(const struct device *dev, bool suspend, bool lock_held) {
    struct tps43_drv_data *drv_data = dev->data;
    const struct tps43_config *config = dev->config;
    int ret = 0;

    // If power management is disabled, do nothing
    if (!config->enable_power_management) {
        return 0;
    }

    // Acquire the semaphore if it is not already held
    if (!lock_held) {
        if (k_sem_take(&drv_data->lock, K_MSEC(100)) != 0) {
            LOG_WRN("Failed to acquire the semaphore for suspend/resume");
            return -EBUSY;
        }
    }

    // Disable the RDY interrupt when entering suspend (before any I2C operations)
    // This prevents a race condition where RDY fires between the suspend attempt and setting the flag
    if (suspend && config->rdy_gpio.port != NULL) {
        ret = gpio_pin_interrupt_configure_dt(&config->rdy_gpio, GPIO_INT_DISABLE);
        if (ret == 0) {
            LOG_INF("RDY interrupt disabled before suspend");
        }
    }

    uint8_t control_reg = 0;
    
    // When exiting suspend the first transaction returns a NACK (section 7.3.1)
    if (drv_data->suspended && !suspend) {
        ret = tps43_i2c_read_reg8_w_err(dev, TPS43_REG_SYSTEM_CONTROL_1, &control_reg, false);
        k_sleep(K_MSEC(200));
        LOG_INF("I2C Wake: device woken from suspend");
        
        // After waking up, read the register again
        ret = tps43_i2c_read_reg8_w_err(dev, TPS43_REG_SYSTEM_CONTROL_1, &control_reg, false);

    } else if (!drv_data->suspended) {
        // Read the current value only if not in suspend
        ret = tps43_i2c_read_reg8_w_err(dev, TPS43_REG_SYSTEM_CONTROL_1, &control_reg, false);
        if (ret != 0) {
            // If error -5 (EIO) occurs while trying to enter suspend - the device is already in suspend
            if (ret == -EIO && suspend) {
                LOG_INF("Device already in suspend (I2C error)");
                drv_data->suspended = true;
                ret = 0;
                goto done;
            }
            LOG_ERR("SYSTEM_CONTROL_1 read error: %d", ret);
            goto done;
        }
    }

    if (suspend) {
        control_reg |= TPS43_SUSPEND;
        LOG_INF("Entering suspend (low power)");
    } else {
        control_reg &= ~TPS43_SUSPEND;
        LOG_INF("Exiting suspend");
    }

    ret = tps43_i2c_write_reg8(dev, TPS43_REG_SYSTEM_CONTROL_1, control_reg);
    if (ret != 0) {
        if (ret == -EIO && suspend) {
            LOG_INF("Failed to write suspend, device already in suspend");
            drv_data->suspended = true;
            ret = 0;
            goto done;
        }
        LOG_ERR("SYSTEM_CONTROL_1 write error: %d", ret);
        goto done;
    }

    drv_data->suspended = suspend;

done:
    // Enable the RDY interrupt after resume
    if (!suspend && config->rdy_gpio.port != NULL) {
        ret = gpio_pin_interrupt_configure_dt(&config->rdy_gpio, GPIO_INT_EDGE_TO_ACTIVE);
        if (ret == 0) {
            LOG_INF("RDY interrupt enabled");
        }
    }
    tps43_end_communication_window(dev);
    if (!lock_held) {
        k_sem_give(&drv_data->lock);
    }
    return ret;
}

/**
 * @brief Main work handler for processing touchpad events
 * 
 * Runs when an interrupt is received from the touchpad (RDY pin).
 * Reads and processes gesture, cursor movement and scroll events,
 * converting them into input events for the ZMK system.
 * Also handles waking the touchpad from suspend mode when activity is detected.
 * 
 * Protected by a semaphore to prevent interruption by other I2C operations,
 * which ensures smooth cursor movement without interruptions.
 * 
 * @param work Pointer to the work structure
 */
static void tps43_work_handler(struct k_work *work) {
    struct tps43_drv_data *drv_data = CONTAINER_OF(work, struct tps43_drv_data, work);
    const struct device *dev = drv_data->dev;
    const struct tps43_config *config = dev->config;
    bool is_scroll_active = drv_data->scroll_active;
    bool is_drag_active = drv_data->drag_active;
    int ret;
    
    // If the device is in suspend, ignore the interrupt (RDY should be disabled)
    if (drv_data->suspended) {
        LOG_WRN("RDY interrupt in suspend mode - ignoring");
        return;
    }
    
    // Acquire the semaphore to protect all I2C operations from interruption
    // This prevents conflicts during simultaneous access to the touchpad
    k_sem_take(&drv_data->lock, K_FOREVER);

    uint8_t sys_info = 0;
    ret = tps43_i2c_read_reg8(dev, TPS43_REG_SYSTEM_INFO_1, &sys_info);
    if (ret < 0) {
        LOG_ERR("System information read error: %d", ret);
        goto done;
    }

    uint8_t gestures_events[2];
    ret = read_sequence_registers(dev, TPS43_REG_GESTURE_EVENTS_0, &gestures_events, 2);
    if (ret < 0) {
        LOG_ERR("Gesture events read error: %d", ret);
        goto done;
    }
    
    if (gestures_events[0] != 0 || gestures_events[1] != 0) {

        LOG_INF("Gestures: Single=0x%02X, Multi=0x%02X", gestures_events[0], gestures_events[1]);

        if (gestures_events[0] & TPS43_SINGLE_TAP) {
            LOG_INF("Single tap → LEFT BUTTON");
            input_report_key(dev, INPUT_BTN_0, 1, true, K_FOREVER);
            input_report_key(dev, INPUT_BTN_0, 0, true, K_FOREVER);  
        }
        if (gestures_events[1] & TPS43_TWO_FINGER_TAP) {
            LOG_INF("Two-finger tap → RIGHT BUTTON");
            input_report_key(dev, INPUT_BTN_1, 1, true, K_FOREVER);  
            input_report_key(dev, INPUT_BTN_1, 0, true, K_FOREVER); 
        }
        if ((gestures_events[0] & TPS43_PRESS_AND_HOLD) && (!(is_drag_active))) {
            LOG_INF("Press and hold detected - DRAG (HOLD LEFT BUTTON)");
            // set the internal drag flag and press the left mouse button
            is_drag_active = true;
            input_report_key(dev, INPUT_BTN_0, 1, true, K_FOREVER); 
        }
        if ((!(gestures_events[0] & TPS43_PRESS_AND_HOLD)) && (is_drag_active)) {
            LOG_INF("End of press and hold detected - RELEASE (RELEASE LEFT BUTTON)");
            // set the internal drag flag and press the left mouse button
            is_drag_active = false;
            input_report_key(dev, INPUT_BTN_0, 0, true, K_FOREVER);   // release + sync
        }
        if (gestures_events[1] & TPS43_SCROLL) {
            LOG_INF("Scroll detected - Scrolling");
            // set the scroll flag for processing in the tp_movement block
            is_scroll_active = true;
        }
    }

    if (sys_info & TPS43_TP_MOVEMENT) {
        int16_t rel_x = 0, rel_y = 0;
        ret = tps43_i2c_read_reg16(dev, TPS43_REG_REL_X, (uint16_t*)&rel_x);
        if (ret < 0) {
            LOG_ERR("REL_X read error: %d", ret);
            goto done;
        }
        ret = tps43_i2c_read_reg16(dev, TPS43_REG_REL_Y, (uint16_t*)&rel_y);
        if (ret < 0) {
            LOG_ERR("REL_Y read error: %d", ret);
            goto done;
        }
        // Send the cursor movement
        if (rel_x != 0 || rel_y != 0) {
            if (rel_x != 0 ) {
                int32_t scaled_x = ((int32_t)rel_x * config->sensitivity) / 100;
                rel_x = (int16_t)CLAMP(scaled_x, INT16_MIN, INT16_MAX);
            }
            if (rel_y != 0) { 
                int32_t scaled_y = ((int32_t)rel_y * config->sensitivity) / 100;
                rel_y = (int16_t)CLAMP(scaled_y, INT16_MIN, INT16_MAX);
            }
            LOG_INF("Sending movement: dx=%d, dy=%d", rel_x, rel_y);

            // Handle three-finger swipes
            if (config->swipes) {
                uint8_t num_fingers = 0;
                ret = tps43_i2c_read_reg8(dev, TPS43_REG_NUM_FINGERS, &num_fingers);
                if (ret < 0) {
                    LOG_ERR("NUM_FINGERS read error: %d", ret);
                    goto done;
                }
                if (num_fingers == 3) {
                    if (rel_x < 0) {
                        LOG_INF("Three-finger swipe left - mouse button 6");
                        input_report_key(dev, INPUT_BTN_6, 1, true, K_FOREVER);
                        input_report_key(dev, INPUT_BTN_6, 0, true, K_FOREVER);
                    }
                    if (rel_x > 0) {
                        LOG_INF("Three-finger swipe right - mouse button 7");
                        input_report_key(dev, INPUT_BTN_7, 1, true, K_FOREVER);
                        input_report_key(dev, INPUT_BTN_7, 0, true, K_FOREVER);
                    }
                }
            }

        
            if (is_scroll_active) {
                // Scroll handling: keep only the dominant axis
                if (abs(rel_x) > abs(rel_y)) {
                    // Horizontal scroll
                    if (config->invert_scroll_x) {
                        rel_x = -rel_x;
                    }
                    int16_t wheel = (rel_x * config->scroll_sensitivity) / 100;
                    input_report_rel(dev, INPUT_REL_HWHEEL, wheel, true, K_FOREVER);
                } else {
                    // Vertical scroll
                    if (config->invert_scroll_y) {
                        rel_y = -rel_y;
                    }
                    int16_t wheel = (rel_y * config->scroll_sensitivity) / 100;
                    input_report_rel(dev, INPUT_REL_WHEEL, wheel, true, K_FOREVER);
                }
                is_scroll_active = false;
            } else {
                // Normal cursor movement
                input_report_rel(dev, INPUT_REL_X, rel_x, false, K_FOREVER);
                input_report_rel(dev, INPUT_REL_Y, rel_y, true, K_FOREVER);
            }
        }
    }

done:
    // Save for the next call
    drv_data->scroll_active = is_scroll_active;
    drv_data->drag_active = is_drag_active;
    tps43_end_communication_window(dev);
    
    // Release the semaphore after all I2C operations are complete
    k_sem_give(&drv_data->lock);
}

/**
 * @brief Resets the driver's internal state values
 * 
 * Initializes all driver state flags to their initial values.
 * Used during device initialization and reset.
 * 
 * @param dev Pointer to the touchpad device
 * @return 0 on success
 */
static int tps43_reset_values(const struct device *dev) {
    struct tps43_drv_data *drv_data = dev->data;

    drv_data->device_ready = false;
    drv_data->initialized = false;
    drv_data->scroll_active = false;
    drv_data->drag_active = false;

    LOG_INF("Resetting values");
    return 0;
}

/**
 * @brief Configures the touchpad's system registers for operation
 * 
 * Sets up the touchpad registers to track touch, gesture and movement events.
 * Enables the required gestures (single tap, press and hold, scroll, two finger tap),
 * configures axis inversion and sets the setup-complete flag.
 * 
 * @param dev Pointer to the touchpad device
 * @return 0 on success, negative error code on failure
 */
static int tps43_configure_device(const struct device *dev) {

    const struct tps43_config *config = dev->config;
    int ret;

    // write the events to track into TPS43_REG_SYSTEM_CONFIG_1  
    uint8_t events_to_track = TPS43_TP_EVENT | TPS43_EVENT_MODE;
    
    // Gestures (single_tap, press_and_hold, scroll, two_finger_tap)
    if (config->single_tap || config->press_and_hold || 
        config->scroll || config->two_finger_tap) {
        events_to_track |= TPS43_GESTURE_EVENT;
    }
    
    // Touch events for absolute coordinates
    events_to_track |= TPS43_TOUCH_EVENT;
    
    ret = tps43_i2c_write_reg8(dev, TPS43_REG_SYSTEM_CONFIG_1, events_to_track);
    if (ret != 0) {
        LOG_WRN("Error writing events to track: %d", ret);
        return ret;
    }
    LOG_INF("Events configured: 0x%02X", events_to_track);

    // axis configuration
    uint8_t xy_config = 0;
    xy_config |= config->invert_x ? TPS43_FLIP_X : 0;
    xy_config |= config->invert_y ? TPS43_FLIP_Y : 0;
    xy_config |= config->switch_xy ? TPS43_SWITCH_XY_AXIS : 0;
    ret = tps43_i2c_write_reg8(dev, TPS43_REG_XY_CONFIG_0, xy_config);
    if (ret != 0) {
        LOG_WRN("Error writing XY configuration: %d", ret);
        return ret;
    }

    // enable single-finger gestures at the hardware level
    if (config->single_tap || config->press_and_hold || config->swipes) {
        uint8_t single_gestures = 0;
        single_gestures |= config->single_tap ? TPS43_SINGLE_TAP : 0;
        single_gestures |= config->press_and_hold ? TPS43_PRESS_AND_HOLD : 0;
        single_gestures |= config->swipes ? TPS43_SWIPE_UP : 0;
        single_gestures |= config->swipes ? TPS43_SWIPE_DOWN : 0;
        single_gestures |= config->swipes ? TPS43_SWIPE_LEFT : 0;
        single_gestures |= config->swipes ? TPS43_SWIPE_RIGHT : 0;
        
        ret = tps43_i2c_write_reg8(dev, TPS43_REG_SINGLE_FINGER_GESTURES, single_gestures);
        if (ret != 0) {
            LOG_WRN("Error configuring single-finger gestures: %d", ret);
            return ret;
        }
        LOG_INF("Single-finger gestures enabled: 0x%02X", single_gestures);
    }

    // enable multi-finger gestures
    if (config->two_finger_tap || config->scroll) {
        uint8_t multi_gestures = 0;
        multi_gestures |= config->two_finger_tap ? TPS43_TWO_FINGER_TAP : 0;
        multi_gestures |= config->scroll ? TPS43_SCROLL : 0;
        
        ret = tps43_i2c_write_reg8(dev, TPS43_REG_MULTI_FINGER_GESTURES, multi_gestures);
        if (ret != 0) {
            LOG_WRN("Error configuring multi-finger gestures: %d", ret);
            return ret;
        }
        LOG_INF("Multi-finger gestures enabled: 0x%02X", multi_gestures);
    }

    // filter configuration
    ret = tps43_i2c_write_reg8(dev, TPS43_REG_FILTER_SETTINGS, config->filter_settings);
    if (ret != 0) {
        LOG_WRN("Error writing filter settings: %d", ret);
        return ret;
    }
    LOG_INF("Filter settings applied: 0x%02X", config->filter_settings);

    // report rate configuration: set the active, idle-touch and idle modes
    // equally fast, so that slow movements do not use the slow
    // report rate (eliminating stutter during slow finger movement).
    if (config->report_rate_ms > 0) {
        ret = tps43_i2c_write_reg16(dev, TPS43_REG_REPORT_RATE_ACTIVE, config->report_rate_ms);
        if (ret == 0) {
            ret = tps43_i2c_write_reg16(dev, TPS43_REG_REPORT_RATE_IDLE_TOUCH, config->report_rate_ms);
        }
        if (ret == 0) {
            ret = tps43_i2c_write_reg16(dev, TPS43_REG_REPORT_RATE_IDLE, config->report_rate_ms);
        }
        if (ret != 0) {
            LOG_WRN("Error writing report rate: %d", ret);
            return ret;
        }
        LOG_INF("Report rate set: %d ms", config->report_rate_ms);
    }

    // XY resolution configuration: hardware scaling of coordinates without
    // software multiplication (less jitter at high cursor speed).
    if (config->x_resolution > 0) {
        ret = tps43_i2c_write_reg16(dev, TPS43_REG_X_RESOLUTION, config->x_resolution);
        if (ret != 0) {
            LOG_WRN("Error writing X resolution: %d", ret);
            return ret;
        }
    }
    if (config->y_resolution > 0) {
        ret = tps43_i2c_write_reg16(dev, TPS43_REG_Y_RESOLUTION, config->y_resolution);
        if (ret != 0) {
            LOG_WRN("Error writing Y resolution: %d", ret);
            return ret;
        }
    }
    if (config->x_resolution > 0 || config->y_resolution > 0) {
        LOG_INF("Resolution set: X=%d Y=%d", config->x_resolution, config->y_resolution);
    }

    // set the configuration-complete flag
    ret = tps43_i2c_write_reg8(dev, TPS43_REG_SYSTEM_CONFIG_0, TPS43_SETUP_COMPLETE);
    if (ret != 0) {
        LOG_WRN("Error writing setup-complete flag: %d", ret);
        return ret;
    }

    return 0;
}

/**
 * @brief Checks the device reset state and performs reconfiguration
 * 
 * Waits for the device to become ready after reset, checks the SHOW_RESET flag
 * and sends a reset acknowledgement (ACK_RESET) if needed.
 * Then performs a full device configuration.
 * 
 * @param dev Pointer to the touchpad device
 * @return 0 on success, negative error code on failure
 */
static int check_reset_and_reconfigure(const struct device *dev) {
    struct tps43_drv_data *drv_data = dev->data;
    int ret;
    uint8_t sys_info = 0;
    uint8_t wait_count = 0;
    const uint8_t max_wait_count = 50;

    // Wait for the device to become ready
    do {
        ret = tps43_i2c_read_reg8(dev, TPS43_REG_SYSTEM_INFO_0, &sys_info);
        if (ret < 0) {
            k_sleep(K_MSEC(100));
            wait_count++;
            if (wait_count >= max_wait_count) {
                LOG_ERR("Device not responding after %d ms", wait_count * 100);
                return -ETIMEDOUT;
            }
        }
    } while (ret < 0);
    
    LOG_INF("Device ready after %d ms", wait_count * 100);

    // after reset, set the flag to acknowledge that the reset was performed
    if (sys_info & TPS43_SHOW_RESET) {
        LOG_INF("SHOW_RESET detected, sending ACK_RESET");
        ret = tps43_i2c_write_reg8(dev, TPS43_REG_SYSTEM_CONTROL_0, TPS43_ACK_RESET);
        if (ret != 0) {
            LOG_ERR("Error sending ACK_RESET: %d", ret);
            return ret;
        }
        k_sleep(K_MSEC(10));
    }

    ret = tps43_configure_device(dev);
    if (ret != 0) {
        LOG_ERR("Device configuration error: %d", ret);
        return ret;
    }

    drv_data->device_ready = true;
    
    return 0;
}

/**
 * @brief Public function for switching the touchpad into suspend/resume
 * 
 * @param dev Pointer to the touchpad device
 * @param suspend true - enter suspend, false - exit suspend
 * @return 0 on success, negative error code on failure
 */
static int tps43_set_suspend(const struct device *dev, bool suspend) {
    return tps43_set_suspend_internal(dev, suspend, false);
}

/**
 * @brief Initializes the TPS43 touchpad driver
 * 
 * Performs full driver initialization: checks I2C bus availability,
 * performs a hardware reset via GPIO RST (if connected), waits for the device
 * to be ready, configures the touchpad registers and sets up the GPIO RDY interrupt.
 * Also initializes the power management subsystem if needed.
 * 
 * @param dev Pointer to the touchpad device
 * @return 0 on success, negative error code on failure
 */
static int tps43_init(const struct device *dev) {

    struct tps43_drv_data *drv_data = dev->data;
    const struct tps43_config *config = dev->config;
    int ret;

    drv_data->dev = dev;

    LOG_INF("=== Azoteq tps43 driver for device %s ===", dev->name);
    
    // Check the I2C bus
    if (!device_is_ready(config->i2c_bus.bus)) {
        LOG_ERR("I2C bus not available");
        return -ENODEV;
    }
    
    LOG_INF("I2C bus: %s", config->i2c_bus.bus->name);
    LOG_INF("I2C address: 0x%02x", config->i2c_bus.addr);

    ret = tps43_reset_values(dev);
    if (ret != 0) {
        LOG_ERR("Error resetting values: %d", ret);
        return ret;
    }

    // GPIO reset via hardware RST
    if (config->rst_gpio.port) {
        ret = gpio_pin_configure_dt(&config->rst_gpio, GPIO_OUTPUT_INACTIVE);
        if (ret != 0) {
            LOG_ERR("Error configuring RST GPIO: %d", ret);
            return ret;
        }
        
        gpio_pin_set_dt(&config->rst_gpio, 0);
        k_sleep(K_MSEC(10));
        gpio_pin_set_dt(&config->rst_gpio, 1);
        k_sleep(K_MSEC(610));
        
        LOG_INF("Hardware reset complete");
    }

    // check SHOW_RESET and configure
    ret = check_reset_and_reconfigure(dev);
    if (ret != 0) {
        LOG_ERR("Device configuration error: %d", ret);
        return ret;
    }

    // configure the RDY interrupt only AFTER the device has been configured!
    if (config->rdy_gpio.port != NULL) {
        ret = gpio_pin_configure_dt(&config->rdy_gpio, GPIO_INPUT);
        if (ret != 0) {
            LOG_WRN("Error configuring RDY GPIO: %d", ret);
        } else {
            ret = gpio_pin_interrupt_configure_dt(&config->rdy_gpio, 
                                                    GPIO_INT_EDGE_TO_ACTIVE);
            if (ret == 0) {
                gpio_init_callback(&drv_data->rdy_cb, tps43_rdy_callback, 
                                    BIT(config->rdy_gpio.pin));
                ret = gpio_add_callback(config->rdy_gpio.port, &drv_data->rdy_cb);
                if (ret == 0) {
                    LOG_INF("RDY interrupt configured");
                } else {
                    LOG_WRN("Error adding RDY callback: %d", ret);
                }
            }
        }
    }

    drv_data->initialized = true;
    drv_data->suspended = false;

    // Initialize the semaphore to protect I2C operations
    // First parameter - initial count (1 = available)
    // Second parameter - maximum count (1 = binary semaphore)
    k_sem_init(&drv_data->lock, 1, 1);

    k_work_init(&drv_data->work, tps43_work_handler);
    
    LOG_INF("TPS43 driver successfully initialized");
    return 0;
}

 
#define TPS43_INIT(inst)                                                                             \
    static struct tps43_drv_data tps43_##inst##_drvdata = {                                          \
        .device_ready = false,                                                                       \
        .initialized = false,                                                                        \
        .scroll_active = false,                                                                      \
        .drag_active = false,                                                                        \
        .suspended = false,                                                                          \
    };                                                                                               \
                                                                                                     \
    static const struct tps43_config tps43_##inst##_config = {                                       \
        .i2c_bus = I2C_DT_SPEC_INST_GET(inst),                                                       \
        .rdy_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, rdy_gpios, {0}),                                  \
        .rst_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, rst_gpios, {0}),                                  \
        .single_tap = DT_INST_PROP(inst, single_tap),                                                \
        .press_and_hold = DT_INST_PROP(inst, press_and_hold),                                        \
        .two_finger_tap = DT_INST_PROP(inst, two_finger_tap),                                        \
        .scroll = DT_INST_PROP(inst, scroll),                                                        \
        .swipes = DT_INST_PROP(inst, swipes),                                                        \
        .invert_x = DT_INST_PROP(inst, invert_x),                                                    \
        .invert_y = DT_INST_PROP(inst, invert_y),                                                    \
        .switch_xy = DT_INST_PROP(inst, switch_xy),                                                  \
        .invert_scroll_x = DT_INST_PROP(inst, invert_scroll_x),                                      \
        .invert_scroll_y = DT_INST_PROP(inst, invert_scroll_y),                                      \
        .sensitivity = DT_INST_PROP_OR(inst, sensitivity, 100),                                      \
        .scroll_sensitivity = DT_INST_PROP_OR(inst, scroll_sensitivity, 50),                         \
        .enable_power_management = DT_INST_PROP_OR(inst, enable_power_management, true),             \
        .filter_settings = DT_INST_PROP_OR(inst, filter_settings, 0x0F),                             \
        .report_rate_ms = DT_INST_PROP_OR(inst, report_rate_ms, 10),                                 \
        .x_resolution = DT_INST_PROP_OR(inst, x_resolution, 2048),                                   \
        .y_resolution = DT_INST_PROP_OR(inst, y_resolution, 1792),                                   \
    };                                                                                               \
                                                                                                     \
    DEVICE_DT_INST_DEFINE(inst, tps43_init, NULL, &tps43_##inst##_drvdata, &tps43_##inst##_config,   \
                        POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);                              \
    BUILD_ASSERT(DT_INST_REG_ADDR(inst) == TPS43_I2C_ADDR, "I2C address mismatch");


DT_INST_FOREACH_STATUS_OKAY(TPS43_INIT)

/**
 * @brief Public function for controlling the touchpad sleep mode
 * 
 * This function is used by the ZMK power management subsystem (via tps43_idle_sleeper)
 * to put the touchpad to sleep when the keyboard enters the idle/sleep state.
 * 
 * @param dev Pointer to the touchpad device
 * @param sleep true - enter sleep mode, false - wake up
 * @return 0 on success, negative error code on failure
 */
int tps43_set_sleep(const struct device *dev, bool sleep) {
    if (dev == NULL) {
        return -EINVAL;
    }
    return tps43_set_suspend(dev, sleep);
}
