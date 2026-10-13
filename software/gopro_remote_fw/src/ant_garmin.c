/*
 * ant_garmin.c - ANT+ bring-up base
 * ==========================================================================
 * The ANT profile is chosen at compile time via ANT_GARMIN_PROFILE:
 *
 *   ANT_GARMIN_PROFILE_VIRB (1)  <-- default
 *       ANT+ *Controls* device (device type 16) in the MASTER role. It
 *       broadcasts and is detected as a controllable device (a scanner / the
 *       Garmin Edge reports it like a Garmin VIRB). Hand-rolled on the raw
 *       channel API. This is the current working bring-up target.
 *
 *   ANT_GARMIN_PROFILE_THERMO (2)
 *       ANT+ *Environment* profile (device type 25): broadcasts the nRF52
 *       internal die temperature (via temp_sensor.h) as a temperature page,
 *       interleaved with common pages 80/81. Also hand-rolled on the raw
 *       channel API.
 *
 *   ANT_GARMIN_PROFILE_REMOTE (3)
 *       The real Garmin Edge Remote behaviour. NOT IMPLEMENTED YET - selecting
 *       it builds an inert stub (ANT stays off). Placeholder for future work
 *       against the ANT+ Controls device profile document.
 *
 *   ANT_GARMIN_PROFILE_BSC (0)
 *       Near-verbatim clone of the sdk-ant ant_plus/ant_bsc/bsc_tx sample
 *       (Bicycle Speed & Cadence sensor). Known-good reference transmitter.
 *
 * Select a non-default profile with e.g. -DANT_GARMIN_PROFILE=2. main.c only
 * ever calls ant_garmin_init().
 */

#include "ant_garmin.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <ant_parameters.h>
#include <ant_state_indicator.h>
#include <ant_key_manager.h>

LOG_MODULE_REGISTER(ant_garmin, LOG_LEVEL_DBG);

/* ------------------------------------------------------------------------ */
/* Profile selector                                                         */
/* ------------------------------------------------------------------------ */
#define ANT_GARMIN_PROFILE_BSC    0
#define ANT_GARMIN_PROFILE_VIRB   1   /* ANT+ Controls master (current) */
#define ANT_GARMIN_PROFILE_THERMO 2   /* ANT+ Environment temperature */
#define ANT_GARMIN_PROFILE_REMOTE 3   /* Garmin Edge remote - Controls SLAVE */

#ifndef ANT_GARMIN_PROFILE
#define ANT_GARMIN_PROFILE ANT_GARMIN_PROFILE_THERMO // ANT_GARMIN_PROFILE_REMOTE //ANT_GARMIN_PROFILE_VIRB
#endif

/* ======================================================================== */
#if ANT_GARMIN_PROFILE == ANT_GARMIN_PROFILE_BSC
/* ======================================================================== */
/* Reference profile: clone of the bsc_tx sample (speed & cadence sensor).  */

#include <ant_profiles/bsc/simulator/ant_bsc_simulator.h>

/*
 * The bsc_tx sample sources these from its own Kconfig (CONFIG_BSC_TX_*,
 * CONFIG_SENSOR_TYPE); that Kconfig is not part of this project, so the
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

static int ant_stack_setup(void) {
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
  LOG_WRN("Garmin button '%s' - no command in BSC reference profile", which);
}

void ant_garmin_note_activity(void) {
  LOG_DBG("ant_garmin_note_activity() - no-op in BSC reference profile");
}

/* ======================================================================== */
#elif ANT_GARMIN_PROFILE == ANT_GARMIN_PROFILE_VIRB
/* ======================================================================== */
/* ANT+ Controls device (device type 16) MASTER - broadcasts; detected as a */
/* controllable device (VIRB-like). Hand-rolled on the raw channel API.     */

#include <ant_init.h>
#include <ant_interface.h>
#include <ant_channel_config.h>

/*
 * The data pages below (Control Device Availability 0x02 + common 80/81) are
 * enough to be detected as a device-type-16 master. The exact capability
 * fields and command handling are not filled in (that is the real-remote
 * work, deferred to ANT_GARMIN_PROFILE_REMOTE). RF 2457 MHz is the fixed
 * ANT+ constant; the period/transmission type are the commonly cited values.
 */
#define CONTROLS_CHANNEL_NUM   0
#define CONTROLS_NETWORK_NUM   0
#define CONTROLS_DEVICE_TYPE   16      /* ANT+ Controls */
#define CONTROLS_DEV_NUM       36
#define CONTROLS_TRANS_TYPE    5
#define CONTROLS_RF_FREQ       57      /* 2457 MHz */
#define CONTROLS_CHANNEL_PERIOD 8192   /* 4.06 Hz */

#define CONTROLS_HW_VERSION    1
#define CONTROLS_MFG_ID        0x00FF
#define CONTROLS_MODEL_NUM     0x0001
#define CONTROLS_SW_VERSION    1
#define CONTROLS_SERIAL_NUM    0x3ADE68B1u

static void build_common_page_80(uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE]) {
  page[0] = 0x50;
  page[1] = 0xFF;
  page[2] = 0xFF;
  page[3] = CONTROLS_HW_VERSION;
  page[4] = (uint8_t)(CONTROLS_MFG_ID & 0xFF);
  page[5] = (uint8_t)((CONTROLS_MFG_ID >> 8) & 0xFF);
  page[6] = (uint8_t)(CONTROLS_MODEL_NUM & 0xFF);
  page[7] = (uint8_t)((CONTROLS_MODEL_NUM >> 8) & 0xFF);
}

static void build_common_page_81(uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE]) {
  page[0] = 0x51;
  page[1] = 0xFF;
  page[2] = 0xFF;
  page[3] = CONTROLS_SW_VERSION;
  page[4] = (uint8_t)(CONTROLS_SERIAL_NUM & 0xFF);
  page[5] = (uint8_t)((CONTROLS_SERIAL_NUM >> 8) & 0xFF);
  page[6] = (uint8_t)((CONTROLS_SERIAL_NUM >> 16) & 0xFF);
  page[7] = (uint8_t)((CONTROLS_SERIAL_NUM >> 24) & 0xFF);
}

static void build_controls_availability_page(uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE]) {
  memset(page, 0xFF, ANT_STANDARD_DATA_PAYLOAD_SIZE);
  page[0] = 0x02; /* Control Device Availability (capability bytes = placeholder) */
}

static void controls_build_next_page(uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE]) {
  static uint8_t counter;

  counter++;
  if (counter % 10 == 1) {
    build_common_page_80(page);
  } else if (counter % 10 == 2) {
    build_common_page_81(page);
  } else {
    build_controls_availability_page(page);
  }
}

static void controls_tx_next(void) {
  uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE];

  controls_build_next_page(page);

  ant_err_t err = ant_broadcast_message_tx(CONTROLS_CHANNEL_NUM,
                                           ANT_STANDARD_DATA_PAYLOAD_SIZE, page);
  if (err) {
    LOG_WRN("ant_broadcast_message_tx() failed: 0x%X", err);
  }
}

static void ant_evt_handler(ant_evt_t *p_ant_evt) {
  if (p_ant_evt->channel != CONTROLS_CHANNEL_NUM) {
    return;
  }

  switch (p_ant_evt->event) {
    case EVENT_TX:
      controls_tx_next();
      break;
    case EVENT_CHANNEL_CLOSED:
      LOG_WRN("ANT Controls ch%u: EVENT_CHANNEL_CLOSED", p_ant_evt->channel);
      break;
    default:
      break;
  }
}

static int ant_stack_setup(void) {
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

  err = ant_plus_key_set(CONTROLS_NETWORK_NUM);
  if (err) {
    LOG_ERR("ant_plus_key_set failed: %d", err);
  }
  return err;
}

static int controls_setup(void) {
  const ant_channel_config_t channel_config = {
      .channel_number    = CONTROLS_CHANNEL_NUM,
      .channel_type      = CHANNEL_TYPE_MASTER,
      .ext_assign        = 0x00,
      .rf_freq           = CONTROLS_RF_FREQ,
      .transmission_type = CONTROLS_TRANS_TYPE,
      .device_type       = CONTROLS_DEVICE_TYPE,
      .device_number     = CONTROLS_DEV_NUM,
      .channel_period    = CONTROLS_CHANNEL_PERIOD,
      .network_number    = CONTROLS_NETWORK_NUM,
  };

  int err = ant_channel_init(&channel_config);
  if (err) {
    LOG_ERR("ant_channel_init failed: 0x%X", err);
    return err;
  }

  controls_tx_next(); /* stage first frame before open */

  err = ant_channel_open(CONTROLS_CHANNEL_NUM);
  if (err) {
    LOG_ERR("ant_channel_open failed: 0x%X", err);
    return err;
  }

  ant_state_indicator_channel_opened();
  return err;
}

void ant_garmin_init(void) {
  LOG_INF("ANT+ Controls master (device type %d, VIRB-like) bring-up starting...",
          CONTROLS_DEVICE_TYPE);

  int err = ant_stack_setup();
  if (err) {
    goto ERROR_EXIT;
  }

  err = ant_state_indicator_init(CONTROLS_CHANNEL_NUM, CHANNEL_TYPE_MASTER);
  if (err) {
    LOG_ERR("ant_state_indicator_init failed: %d", err);
    goto ERROR_EXIT;
  }

  err = controls_setup();
  if (err) {
    goto ERROR_EXIT;
  }

  LOG_INF("ANT+ Controls channel open (dev# %d, type %d)", CONTROLS_DEV_NUM, CONTROLS_DEVICE_TYPE);
  return;

ERROR_EXIT:
  ant_state_indicator_fatal_error();
  k_oops();
}

void ant_garmin_handle_button(const char *which) {
  LOG_WRN("Garmin button '%s' pressed - ANT+ Controls command not implemented yet", which);
}

void ant_garmin_note_activity(void) {
  LOG_DBG("ant_garmin_note_activity() - no-op in VIRB profile");
}

/* ======================================================================== */
#elif ANT_GARMIN_PROFILE == ANT_GARMIN_PROFILE_THERMO
/* ======================================================================== */
/* ANT+ Environment profile (device type 25) MASTER - broadcasts the nRF52  */
/* internal die temperature. Hand-rolled on the raw channel API.           */

#include "temp_sensor.h"

#include <ant_init.h>
#include <ant_interface.h>
#include <ant_channel_config.h>

/*
 * NOTE: the ANT+ Environment temperature page byte layout below is the
 * project's earlier best-effort encoding, NOT verified against the ANT+
 * Environment device profile document (a different doc than Controls). The
 * channel is a correct device-type-25 master; refine the page bytes against
 * the real Environment profile before trusting a specific Garmin's reading.
 */
#define ENV_CHANNEL_NUM    0
#define ENV_NETWORK_NUM    0
#define ENV_DEVICE_TYPE    25      /* ANT+ Environment */
#define ENV_DEV_NUM        1
#define ENV_TRANS_TYPE     5
#define ENV_RF_FREQ        57      /* 2457 MHz */
#define ENV_CHANNEL_PERIOD 8192    /* 4.06 Hz (unverified for Environment) */

#define ENV_HW_VERSION     1
#define ENV_MFG_ID         0x00FF
#define ENV_MODEL_NUM      0x0001
#define ENV_SW_VERSION     1
#define ENV_SERIAL_NUM     0x3ADE68B1u

/* Latest die temperature in hundredths of a degree C, refreshed off-radio by
 * a periodic workqueue item (sensor reads shouldn't run in the ANT callback). */
static int16_t env_temp_centi = 2000; /* 20.00 C default until first read */

static void env_temp_work_handler(struct k_work *work) {
  int16_t t;
  if (temp_sensor_read(&t)) {
    env_temp_centi = t;
    LOG_DBG("Environment: temp = %d.%02d C", t / 100, (t < 0 ? -t : t) % 100);
  } else {
    LOG_WRN("Environment: temperature read failed");
  }
}
K_WORK_DEFINE(env_temp_work, env_temp_work_handler);

static void env_temp_timer_expiry(struct k_timer *timer_id) { k_work_submit(&env_temp_work); }
K_TIMER_DEFINE(env_temp_timer, env_temp_timer_expiry, NULL);

static void build_common_page_80(uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE]) {
  page[0] = 0x50;
  page[1] = 0xFF;
  page[2] = 0xFF;
  page[3] = ENV_HW_VERSION;
  page[4] = (uint8_t)(ENV_MFG_ID & 0xFF);
  page[5] = (uint8_t)((ENV_MFG_ID >> 8) & 0xFF);
  page[6] = (uint8_t)(ENV_MODEL_NUM & 0xFF);
  page[7] = (uint8_t)((ENV_MODEL_NUM >> 8) & 0xFF);
}

static void build_common_page_81(uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE]) {
  page[0] = 0x51;
  page[1] = 0xFF;
  page[2] = 0xFF;
  page[3] = ENV_SW_VERSION;
  page[4] = (uint8_t)(ENV_SERIAL_NUM & 0xFF);
  page[5] = (uint8_t)((ENV_SERIAL_NUM >> 8) & 0xFF);
  page[6] = (uint8_t)((ENV_SERIAL_NUM >> 16) & 0xFF);
  page[7] = (uint8_t)((ENV_SERIAL_NUM >> 24) & 0xFF);
}

/* Environment "temperature" data page (page 1). Layout is the project's
 * earlier encoding - see NOTE above. Current temperature is signed, 0.01 C. */
static void build_env_temperature_page(uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE]) {
  static uint8_t event_count;
  int16_t temp_centi = env_temp_centi;

  /* 24h min/max: 0.1 C scale, offset -204.8 C, 12 bits. Here min == max ==
   * current (no running history kept yet). */
  uint16_t encoded = (uint16_t)((temp_centi / 100.0f + 204.8f) * 10.0f);

  page[0] = 0x01;                /* page number */
  page[1] = event_count++;       /* measurement event count */
  page[2] = (uint8_t)(encoded & 0xFF);                        /* low temp LSB */
  page[3] = (uint8_t)(((encoded >> 8) & 0x0F) | ((encoded & 0x0F) << 4));
  page[4] = (uint8_t)((encoded >> 4) & 0xFF);                 /* high temp MSB */
  page[5] = (uint8_t)(temp_centi & 0xFF);                     /* current temp LSB */
  page[6] = (uint8_t)((temp_centi >> 8) & 0xFF);              /* current temp MSB */
  page[7] = 0xFF;                /* reserved */
}

static void env_build_next_page(uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE]) {
  static uint8_t counter;

  counter++;
  if (counter % 10 == 1) {
    build_common_page_80(page);
  } else if (counter % 10 == 2) {
    build_common_page_81(page);
  } else {
    build_env_temperature_page(page);
  }
}

static void env_tx_next(void) {
  uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE];

  env_build_next_page(page);

  ant_err_t err = ant_broadcast_message_tx(ENV_CHANNEL_NUM,
                                           ANT_STANDARD_DATA_PAYLOAD_SIZE, page);
  if (err) {
    LOG_WRN("ant_broadcast_message_tx() failed: 0x%X", err);
  }
}

static void ant_evt_handler(ant_evt_t *p_ant_evt) {
  if (p_ant_evt->channel != ENV_CHANNEL_NUM) {
    return;
  }

  switch (p_ant_evt->event) {
    case EVENT_TX:
      env_tx_next();
      break;
    case EVENT_CHANNEL_CLOSED:
      LOG_WRN("ANT Environment ch%u: EVENT_CHANNEL_CLOSED", p_ant_evt->channel);
      break;
    default:
      break;
  }
}

static int ant_stack_setup(void) {
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

  err = ant_plus_key_set(ENV_NETWORK_NUM);
  if (err) {
    LOG_ERR("ant_plus_key_set failed: %d", err);
  }
  return err;
}

static int env_setup(void) {
  const ant_channel_config_t channel_config = {
      .channel_number    = ENV_CHANNEL_NUM,
      .channel_type      = CHANNEL_TYPE_MASTER,
      .ext_assign        = 0x00,
      .rf_freq           = ENV_RF_FREQ,
      .transmission_type = ENV_TRANS_TYPE,
      .device_type       = ENV_DEVICE_TYPE,
      .device_number     = ENV_DEV_NUM,
      .channel_period    = ENV_CHANNEL_PERIOD,
      .network_number    = ENV_NETWORK_NUM,
  };

  int err = ant_channel_init(&channel_config);
  if (err) {
    LOG_ERR("ant_channel_init failed: 0x%X", err);
    return err;
  }

  env_tx_next(); /* stage first frame before open */

  err = ant_channel_open(ENV_CHANNEL_NUM);
  if (err) {
    LOG_ERR("ant_channel_open failed: 0x%X", err);
    return err;
  }

  ant_state_indicator_channel_opened();
  return err;
}

void ant_garmin_init(void) {
  LOG_INF("ANT+ Environment (temperature, device type %d) bring-up starting...",
          ENV_DEVICE_TYPE);

  int err = ant_stack_setup();
  if (err) {
    goto ERROR_EXIT;
  }

  err = ant_state_indicator_init(ENV_CHANNEL_NUM, CHANNEL_TYPE_MASTER);
  if (err) {
    LOG_ERR("ant_state_indicator_init failed: %d", err);
    goto ERROR_EXIT;
  }

  /* Refresh the die temperature every 2 s, off the radio callback. */
  k_work_submit(&env_temp_work);
  k_timer_start(&env_temp_timer, K_SECONDS(2), K_SECONDS(2));

  err = env_setup();
  if (err) {
    goto ERROR_EXIT;
  }

  LOG_INF("ANT+ Environment channel open (dev# %d, type %d)", ENV_DEV_NUM, ENV_DEVICE_TYPE);
  return;

ERROR_EXIT:
  ant_state_indicator_fatal_error();
  k_oops();
}

void ant_garmin_handle_button(const char *which) {
  LOG_WRN("Garmin button '%s' - no command in Environment profile", which);
}

void ant_garmin_note_activity(void) {
  LOG_DBG("ant_garmin_note_activity() - no-op in Environment profile");
}

/* ======================================================================== */
#elif ANT_GARMIN_PROFILE == ANT_GARMIN_PROFILE_REMOTE
/* ======================================================================== */
/* The real Garmin Edge Remote behaviour (ANT+ Controls device profile).    */
/*                                                                           */
/* Role split (ANT+ Controls profile): the *controllable* device (the Edge) */
/* is the MASTER and broadcasts the Control Device Availability page (0x02); */
/* the *control* device (this remote) is the SLAVE. We search for any Edge   */
/* master (wildcard device number / transmission type), receive its 0x02     */
/* page, and on a button press send the Generic Command page (0x49 = 73) as  */
/* an *acknowledged* message, exactly like the OpenSourceEBike wireless      */
/* remote (firmware/antplus_controls). The command number (bytes 6-7, LE)    */
/* tells the Edge what to do: 0x0000 page (long), 0x0001 page (short),       */
/* 0x0024 lap.                                                               */

#include <ant_init.h>
#include <ant_interface.h>
#include <ant_channel_config.h>

#define CONTROLS_CHANNEL_NUM   0
#define CONTROLS_NETWORK_NUM   0
#define CONTROLS_DEVICE_TYPE   16      /* ANT+ Controls */
/* SLAVE search: wildcards so we pair with any Edge acting as master. */
#define CONTROLS_DEV_NUM       0       /* wildcard: match any master */
#define CONTROLS_TRANS_TYPE    0       /* wildcard */
#define CONTROLS_RF_FREQ       57      /* 2457 MHz */
#define CONTROLS_CHANNEL_PERIOD 8192   /* 4.06 Hz */

/* Identity advertised in the Generic Command page 0x49 (slave serial /
 * manufacturer id). Values mirror the OpenSourceEBike remote. */
#define CONTROLS_SERIAL_NUM    0x0001
#define CONTROLS_MFG_ID        0x000F

/* ANT+ Controls "Generic Command Number" codes (page 0x49, bytes 6-7 LE). */
#define CTRL_CMD_PAGE_LONG     0x0000u
#define CTRL_CMD_PAGE_SHORT    0x0001u
#define CTRL_CMD_LAP           0x0024u

/* Control Device Availability page received from the master (the Edge). */
#define CTRL_PAGE_AVAILABILITY 0x02
/* Generic Command page we transmit to the master. */
#define CTRL_PAGE_GENERIC_CMD  0x49

/* Running sequence number for the Generic Command page (byte 5). */
static uint8_t controls_cmd_sequence;
/* Set once the master's availability page has been seen at least once. */
static bool controls_master_seen;

/*
 * Build + send the ANT+ Controls Generic Command page (0x49 / 73) as an
 * acknowledged message to the paired master (the Edge). Layout (per the
 * ANT+ Controls device profile, matching OpenSourceEBike):
 *   [0] = 0x49 page number
 *   [1..2] = slave serial number (LE)
 *   [3..4] = slave manufacturer id (LE)
 *   [5]    = sequence number (increments each command)
 *   [6..7] = command number (LE)
 */
static void controls_send_generic_command(uint16_t command) {
  uint8_t page[ANT_STANDARD_DATA_PAYLOAD_SIZE] = {
      CTRL_PAGE_GENERIC_CMD,
      (uint8_t)(CONTROLS_SERIAL_NUM & 0xFF),
      (uint8_t)((CONTROLS_SERIAL_NUM >> 8) & 0xFF),
      (uint8_t)(CONTROLS_MFG_ID & 0xFF),
      (uint8_t)((CONTROLS_MFG_ID >> 8) & 0xFF),
      controls_cmd_sequence++,
      (uint8_t)(command & 0xFF),
      (uint8_t)((command >> 8) & 0xFF),
  };

  ant_err_t err = ant_acknowledge_message_tx(CONTROLS_CHANNEL_NUM,
                                             ANT_STANDARD_DATA_PAYLOAD_SIZE, page);
  if (err) {
    LOG_WRN("page 0x49 ant_acknowledge_message_tx() failed: 0x%X", err);
  } else {
    LOG_INF("ANT+ Controls: sent Generic Command 0x%04X (seq %u)",
            command, (unsigned)(controls_cmd_sequence - 1));
  }
}

static void controls_handle_rx(const uint8_t *payload) {
  uint8_t page = payload[0];

  if (page == CTRL_PAGE_AVAILABILITY) {
    /* The master broadcasts this at ~4 Hz; byte 7 is the control
     * capabilities bitfield. Log only on pairing and when caps change,
     * so we don't flood the console with a steady 4 Hz stream. */
    static bool have_caps;
    static uint8_t last_caps;
    uint8_t caps = payload[7];

    if (!controls_master_seen) {
      controls_master_seen = true;
      LOG_INF("ANT+ Controls: paired with master, Device Availability page 0x02 received");
    }
    if (!have_caps || caps != last_caps) {
      have_caps = true;
      last_caps = caps;
      LOG_DBG("RX page 0x02: %02X %02X %02X %02X %02X %02X %02X %02X (caps=0x%02X)",
              payload[0], payload[1], payload[2], payload[3],
              payload[4], payload[5], payload[6], payload[7], caps);
    }
  } else {
    /* Any other (rarer) page: trace it as it arrives. */
    LOG_DBG("RX page 0x%02X: %02X %02X %02X %02X %02X %02X %02X %02X",
            page, payload[0], payload[1], payload[2], payload[3],
            payload[4], payload[5], payload[6], payload[7]);
  }
}

static void ant_evt_handler(ant_evt_t *p_ant_evt) {
  if (p_ant_evt->channel != CONTROLS_CHANNEL_NUM) {
    return;
  }

  switch (p_ant_evt->event) {
    case EVENT_RX: {
      uint8_t msg_id = p_ant_evt->message.ANT_MESSAGE_ucMesgID;
      if (msg_id == MESG_BROADCAST_DATA_ID ||
          msg_id == MESG_ACKNOWLEDGED_DATA_ID ||
          msg_id == MESG_BURST_DATA_ID) {
        controls_handle_rx(p_ant_evt->message.ANT_MESSAGE_aucPayload);
      }
      break;
    }
    case EVENT_RX_SEARCH_TIMEOUT:
      LOG_WRN("ANT Controls ch%u: RX search timeout", p_ant_evt->channel);
      break;
    case EVENT_RX_FAIL_GO_TO_SEARCH:
      LOG_WRN("ANT Controls ch%u: RX fail, back to search", p_ant_evt->channel);
      controls_master_seen = false;
      break;
    case EVENT_CHANNEL_CLOSED:
      LOG_WRN("ANT Controls ch%u: EVENT_CHANNEL_CLOSED, reopening", p_ant_evt->channel);
      controls_master_seen = false;
      ant_channel_open(CONTROLS_CHANNEL_NUM);
      break;
    default:
      break;
  }
}

static int ant_stack_setup(void) {
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

  err = ant_plus_key_set(CONTROLS_NETWORK_NUM);
  if (err) {
    LOG_ERR("ant_plus_key_set failed: %d", err);
  }
  return err;
}

static int controls_setup(void) {
  const ant_channel_config_t channel_config = {
      .channel_number    = CONTROLS_CHANNEL_NUM,
      .channel_type      = CHANNEL_TYPE_SLAVE,
      .ext_assign        = 0x00,
      .rf_freq           = CONTROLS_RF_FREQ,
      .transmission_type = CONTROLS_TRANS_TYPE,
      .device_type       = CONTROLS_DEVICE_TYPE,
      .device_number     = CONTROLS_DEV_NUM,
      .channel_period    = CONTROLS_CHANNEL_PERIOD,
      .network_number    = CONTROLS_NETWORK_NUM,
  };

  int err = ant_channel_init(&channel_config);
  if (err) {
    LOG_ERR("ant_channel_init failed: 0x%X", err);
    return err;
  }

  /* Slave: no frame to stage, we open in RX search and wait for the Edge. */
  err = ant_channel_open(CONTROLS_CHANNEL_NUM);
  if (err) {
    LOG_ERR("ant_channel_open failed: 0x%X", err);
    return err;
  }

  ant_state_indicator_channel_opened();
  return err;
}

void ant_garmin_init(void) {
  LOG_INF("ANT+ REMOTE (Controls slave, device type %d) bring-up starting...",
          CONTROLS_DEVICE_TYPE);

  int err = ant_stack_setup();
  if (err) {
    goto ERROR_EXIT;
  }

  err = ant_state_indicator_init(CONTROLS_CHANNEL_NUM, CHANNEL_TYPE_SLAVE);
  if (err) {
    LOG_ERR("ant_state_indicator_init failed: %d", err);
    goto ERROR_EXIT;
  }

  err = controls_setup();
  if (err) {
    goto ERROR_EXIT;
  }

  LOG_INF("ANT+ REMOTE slave searching for Edge master (type %d, wildcard dev#)",
          CONTROLS_DEVICE_TYPE);
  return;

ERROR_EXIT:
  ant_state_indicator_fatal_error();
  k_oops();
}

void ant_garmin_handle_button(const char *which) {
  uint16_t command;

  if (strcmp(which, "page_right") == 0) {
    command = CTRL_CMD_PAGE_SHORT;
  } else if (strcmp(which, "page_left") == 0) {
    command = CTRL_CMD_PAGE_LONG;
  } else if (strcmp(which, "lap") == 0) {
    command = CTRL_CMD_LAP;
  } else {
    LOG_WRN("Garmin button '%s' unknown - no ANT+ command sent", which);
    return;
  }

  if (!controls_master_seen) {
    LOG_WRN("button '%s' pressed but no Edge master paired yet - sending anyway", which);
  }
  controls_send_generic_command(command);
}

void ant_garmin_note_activity(void) {
  LOG_DBG("ant_garmin_note_activity() - no-op in REMOTE profile");
}

/* ======================================================================== */
#else
#error "Invalid ANT_GARMIN_PROFILE (BSC=0, VIRB=1, THERMO=2, REMOTE=3)"
#endif
/* ======================================================================== */
