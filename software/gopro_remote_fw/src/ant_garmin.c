/*
 * ant_garmin.c - ANT+ bring-up base
 * ==========================================================================
 * Near-verbatim clone of Nordic's sdk-ant `ant_plus/ant_bsc/bsc_tx` sample
 * (see ant_garmin.h for the rationale). The structure below mirrors the
 * sample's src/main.c: channel/profile config macros, the BSC event handler,
 * the simulator, the ANT event handler, profile_setup(), ant_stack_setup()
 * and the operating-time timer. The only reshaping is that the sample's
 * main() body is exposed here as ant_garmin_init(), since main.c owns main().
 */

#include "ant_garmin.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <ant_parameters.h>
#include <ant_state_indicator.h>
#include <ant_key_manager.h>
#include <ant_profiles/bsc/simulator/ant_bsc_simulator.h>

LOG_MODULE_REGISTER(ant_garmin, LOG_LEVEL_INF);

/*
 * Channel / product constants.
 * ----------------------------------------------------------------------
 * The bsc_tx sample sources these from its own Kconfig (CONFIG_BSC_TX_*,
 * CONFIG_SENSOR_TYPE). That Kconfig is not part of this project, so the
 * values below reproduce the sample's Kconfig defaults verbatim.
 */
#define BSC_TX_CHANNEL_NUM        0      /* CONFIG_BSC_TX_CHANNEL_NUM */
#define BSC_TX_NETWORK_NUM        0      /* CONFIG_BSC_TX_NETWORK_NUM */
#define BSC_TX_CHAN_ID_DEV_NUM    49     /* CONFIG_BSC_TX_CHAN_ID_DEV_NUM */
#define BSC_TX_CHAN_ID_TRANS_TYPE 1      /* CONFIG_BSC_TX_CHAN_ID_TRANS_TYPE */
#define SENSOR_TYPE               121    /* CONFIG_SENSOR_TYPE: 121 = combined speed+cadence */

#define BSC_TX_HW_VERSION         5      /* CONFIG_BSC_TX_HW_VERSION */
#define BSC_TX_MFG_ID             2      /* CONFIG_BSC_TX_MFG_ID */
#define BSC_TX_MODEL_NUM          2      /* CONFIG_BSC_TX_MODEL_NUM */
#define BSC_TX_SW_VERSION         0      /* CONFIG_BSC_TX_SW_VERSION */
#define BSC_TX_SERIAL_NUM         43981  /* CONFIG_BSC_TX_SERIAL_NUM */

#define TIMER_TICK_MS (ANT_BSC_OPERATING_TIME_UNIT * 1000)

void ant_bsc_evt_handler(ant_bsc_profile_t *p_profile, ant_bsc_evt_t event);

BSC_SENS_CHANNEL_CONFIG_DEF(bsc, BSC_TX_CHANNEL_NUM, BSC_TX_CHAN_ID_TRANS_TYPE,
                            SENSOR_TYPE, BSC_TX_CHAN_ID_DEV_NUM,
                            BSC_TX_NETWORK_NUM);
BSC_SENS_PROFILE_CONFIG_DEF(bsc, true, true, ANT_BSC_PAGE_5, ant_bsc_evt_handler);

static void timeout_handler(struct k_timer *timer_id);
static K_TIMER_DEFINE(timer, timeout_handler, NULL);

static ant_bsc_profile_t bsc;
static ant_bsc_simulator_t bsc_simulator;

void ant_bsc_evt_handler(ant_bsc_profile_t *p_profile, ant_bsc_evt_t event) {
  switch (event) {
    case ANT_BSC_PAGE_0_UPDATED:
      /* fall through */
    case ANT_BSC_PAGE_1_UPDATED:
      /* fall through */
    case ANT_BSC_PAGE_2_UPDATED:
      /* fall through */
    case ANT_BSC_PAGE_3_UPDATED:
      /* fall through */
    case ANT_BSC_PAGE_4_UPDATED:
      /* fall through */
    case ANT_BSC_PAGE_5_UPDATED:
      /* fall through */
    case ANT_BSC_COMB_PAGE_0_UPDATED:
      ant_bsc_simulator_one_iteration(&bsc_simulator);
      break;
    default:
      break;
  }
}

static void simulator_setup(void) {
  const ant_bsc_simulator_cfg_t simulator_cfg = {
      .p_profile = &bsc,
      .device_type = SENSOR_TYPE,
  };

  /* AUTO mode (sample's CONFIG_UPDATE_AUTO default): sweep automatically. */
  ant_bsc_simulator_init(&bsc_simulator, &simulator_cfg, true);
}

static void timeout_handler(struct k_timer *timer_id) {
  /** NOTE: Only the first 3 bytes of this value are taken into account. */
  bsc.BSC_PROFILE_operating_time++;
}

static int utils_setup(void) {
  int err = ant_state_indicator_init(bsc.channel_number, BSC_SENS_CHANNEL_TYPE);
  if (err) {
    LOG_ERR("ant_state_indicator_init failed: %d", err);
    return err;
  }

  return err;
}

static void ant_evt_handler(ant_evt_t *p_ant_evt) { ant_bsc_sens_evt_handler(p_ant_evt, &bsc); }

static int profile_setup(void) {
  int err = ant_bsc_sens_init(&bsc, BSC_SENS_CHANNEL_CONFIG(bsc), BSC_SENS_PROFILE_CONFIG(bsc));
  if (err) {
    LOG_ERR("ant_bsc_sens_init failed: %d", err);
    return err;
  }
  bsc.BSC_PROFILE_manuf_id = BSC_TX_MFG_ID;
  bsc.BSC_PROFILE_serial_num = BSC_TX_SERIAL_NUM;
  bsc.BSC_PROFILE_hw_version = BSC_TX_HW_VERSION;
  bsc.BSC_PROFILE_sw_version = BSC_TX_SW_VERSION;
  bsc.BSC_PROFILE_model_num = BSC_TX_MODEL_NUM;

  err = ant_bsc_sens_open(&bsc);
  if (err) {
    LOG_ERR("ant_bsc_sens_open failed: %d", err);
    return err;
  }

  ant_state_indicator_channel_opened();
  return err;
}

int ant_stack_setup(void) {
  int err = ant_init();
  if (err) {
    LOG_ERR("ant_init failed: %d", err);
    return err;
  }
  LOG_INF("ANT Version %s", ANT_VERSION_STRING);

  err = ant_cb_register(&ant_evt_handler);
  if (err) {
    LOG_ERR("ant_cb_register failed: %d", err);
    return err;
  }

  err = ant_plus_key_set(BSC_TX_NETWORK_NUM);
  if (err) {
    LOG_ERR("ant_plus_key_set failed: %d", err);
  }
  return err;
}

/*
 * Clone of the bsc_tx sample's main(): bring up the stack, the state
 * indicator, the simulator and the BSC sensor profile, then start the
 * operating-time timer. Exposed as ant_garmin_init() because main.c owns
 * the real main() and calls into this module.
 */
void ant_garmin_init(void) {
  LOG_INF("ANT+ Bicycle Speed and Cadence TX bring-up starting...");

  int err = ant_stack_setup();
  if (err) {
    goto ERROR_EXIT;
  }

  err = utils_setup();
  if (err) {
    goto ERROR_EXIT;
  }

  simulator_setup();

  err = profile_setup();
  if (err) {
    goto ERROR_EXIT;
  }

  k_timer_start(&timer, K_MSEC(TIMER_TICK_MS), K_MSEC(TIMER_TICK_MS));
  LOG_INF("ANT+ BSC sensor channel open");
  return;

ERROR_EXIT:
  ant_state_indicator_fatal_error();
  k_oops();
}

void ant_garmin_handle_button(const char *which) {
  /* Controls (Generic) profile - deferred past this BSC bring-up base. */
  LOG_WRN("Garmin button '%s' pressed - ANT+ Controls profile not implemented yet", which);
}

void ant_garmin_note_activity(void) {
  /* Activity-gated temperature session - deferred past this bring-up base. */
  LOG_DBG("ant_garmin_note_activity() - no-op in BSC bring-up base");
}
