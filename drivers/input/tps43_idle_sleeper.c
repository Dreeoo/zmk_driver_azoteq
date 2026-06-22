/**
* @file tps43_idle_sleeper.c
* @brief Integration of TPS43 touchpad power management with the ZMK power management system
* 
* This module subscribes to ZMK activity state change events and automatically
* puts the touchpad into sleep mode when the keyboard transitions to idle/sleep, which
* significantly reduces power consumption.
* 
* Works together with the automatic power management in the main driver (tps43.c),
* which tracks the touchpad's idle time.
*/

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include "tps43.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tps43_sleeper, CONFIG_INPUT_LOG_LEVEL);

/**
* @brief Macro to get a pointer to the device from the devicetree
*/
#define GET_TPS43_DEV(node_id) DEVICE_DT_GET(node_id),

/**
* @brief Array of pointers to all TPS43 devices in the system
* 
* Automatically populated from the devicetree for all devices compatible with azoteq_tps43
*/
static const struct device *tps43_devs[] = {DT_FOREACH_STATUS_OKAY(azoteq_tps43, GET_TPS43_DEV)};

/**
* @brief Handler for the ZMK activity state change event
* 
* This function is called when the keyboard's activity state changes:
* - ZMK_ACTIVITY_ACTIVE - keyboard is active, the touchpad should be woken up
* - ZMK_ACTIVITY_IDLE - keyboard is idle, the touchpad is put to sleep
* - ZMK_ACTIVITY_SLEEP - keyboard is in sleep mode, the touchpad is put to sleep
* 
* @param eh Pointer to the activity state change event
* @return 0 on successful handling
*/
static int on_activity_state(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *state_ev = as_zmk_activity_state_changed(eh);
    if (!state_ev) {
        LOG_WRN("Event not found, ignoring");
        return 0;
    }

    /* Put the touchpad to sleep if the state is not ACTIVE, otherwise wake it up */
    bool should_sleep = (state_ev->state != ZMK_ACTIVITY_ACTIVE);
    
    LOG_INF("ZMK activity state change: %d -> touchpad %s", 
            state_ev->state, should_sleep ? "sleep" : "active");
    
    // Apply the change to all TPS43 devices in the system
    for (size_t i = 0; i < ARRAY_SIZE(tps43_devs); i++) {
        int ret = tps43_set_sleep(tps43_devs[i], should_sleep);
        if (ret != 0) {
            LOG_WRN("Touchpad power management error %zu: %d", i, ret);
        }
    }

    return 0;
}

// Register the ZMK event listener
ZMK_LISTENER(tps43_idle_sleeper, on_activity_state);
// Subscribe to activity state change events
ZMK_SUBSCRIPTION(tps43_idle_sleeper, zmk_activity_state_changed);