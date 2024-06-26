/************************************************************************************
  If not stated otherwise in this file or this component's LICENSE file the
  following copyright and licenses apply:

  Copyright 2018 RDK Management

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
**************************************************************************/

#ifndef	_WIFI_MON_H_
#define	_WIFI_MON_H_

#include <pthread.h>
#include "wifi_blaster.h"
#include "collection.h"
#include "scheduler.h"
#include <math.h>
#define MAX_ASSOCIATED_WIFI_DEVS    64

#ifndef WIFI_HAL_VERSION_3
#define MAX_RADIOS  2
#endif

#define MAC_ADDR_LEN	6
#define STA_KEY_LEN		2*MAC_ADDR_LEN + 6
#define MAX_IPC_DATA_LEN    1024
#define KMSG_WRAPPER_FILE_NAME  "/tmp/goodbad-rssi"

#define CLIENT_STATS_MAX_LEN_BUF    (128)
#define MIN_MAC_ADDR_LEN	2*MAC_ADDR_LEN + 1


#if defined (FEATURE_CSI)
#define IP_STR_LEN 64
#define MILLISEC_TO_MICROSEC 1000
#define IPREFRESH_PERIOD_IN_MILLISECONDS 24 * 60 * 60 * 1000
#define MAX_CSI_CLIENTS_PER_SESSION 50
#endif

#define MONITOR_RUNNING_INTERVAL_IN_MILLISEC    100

typedef unsigned char   mac_addr_t[MAC_ADDR_LEN];
typedef signed short    rssi_t;
typedef char			sta_key_t[STA_KEY_LEN];
void wifi_dbg_print(int level, char *format, ...);

typedef enum {
    monitor_event_type_diagnostics,
    monitor_event_type_connect,
    monitor_event_type_disconnect,
    monitor_event_type_deauthenticate,
	monitor_event_type_start_inst_msmt,
	monitor_event_type_stop_inst_msmt,
    monitor_event_type_StatsFlagChange,
    monitor_event_type_RadioStatsFlagChange,
    monitor_event_type_VapStatsFlagChange,
    monitor_event_type_process_active_msmt,
#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
    monitor_event_type_speedtest_started,
    monitor_event_type_speedtest_completed,
    monitor_event_type_speedtest_timeout,
#endif
#if defined (FEATURE_CSI)
    monitor_event_type_csi,
    monitor_event_type_csi_update_config,
    monitor_event_type_clientdiag_update_config,
#endif
    monitor_event_type_max
} wifi_monitor_event_type_t;

typedef struct {
    unsigned int num;
#if defined (FEATURE_CSI)
    wifi_associated_dev3_t  devs[MAX_ASSOCIATED_WIFI_DEVS];
#else
    wifi_associated_dev_t  devs[MAX_ASSOCIATED_WIFI_DEVS];
#endif
} associated_devs_t;

typedef struct {
    mac_addr_t sta_mac;
    int reason;
    unsigned int cli_CapableNumSpatialStreams;
    int cli_RSSI;
} auth_deauth_dev_t;

typedef struct {
    int type;  //Device.WiFi.X_RDKCENTRAL-COM_vAPStatsEnable= 0, Device.WiFi.AccessPoint.<vAP>.X_RDKCENTRAL-COM_StatsEnable = 1
    bool enable; // ture, false
} client_stats_enable_t;

typedef struct {
    mac_addr_t  sta_mac;
	unsigned int ap_index;
	bool active;
} instant_msmt_t;

#if defined (FEATURE_CSI)
typedef struct {
    mac_addr_t  sta_mac;
    wifi_csi_data_t csi;
} __attribute__((packed)) wifi_csi_dev_t;
#endif

typedef struct {
    unsigned int id;
#if defined (FEATURE_CSI)
    int  csi_session;
#endif    
    wifi_monitor_event_type_t   event_type;
    unsigned int    ap_index;
    union {
        auth_deauth_dev_t	dev;
        client_stats_enable_t   flag;
        instant_msmt_t		imsmt;
        associated_devs_t   devs;
#if defined (FEATURE_CSI)
        wifi_csi_dev_t csi;
#endif
    } u;
} __attribute__((__packed__)) wifi_monitor_data_t;

typedef struct {
    mac_addr_t  sta_mac;
    unsigned int    good_rssi_time;
    unsigned int    bad_rssi_time;
    unsigned int    connected_time;
    unsigned int    disconnected_time;
    unsigned int    total_connected_time;
    unsigned int    total_disconnected_time;
    struct timeval  last_connected_time;
    struct timeval  last_disconnected_time;
    unsigned int    rapid_reconnects;
	bool			updated;
    wifi_associated_dev3_t dev_stats;
    wifi_associated_dev3_t dev_stats_last;
    unsigned int    reconnect_count;
    long            assoc_monitor_start_time;
    long            gate_time;
    unsigned int    redeauth_count;
    long            deauth_monitor_start_time;
    long            deauth_gate_time;
    unsigned long long prev_cli_rx_retries;
    unsigned long long cli_rx_retries;
    active_msmt_data_t *sta_active_msmt_data;
    unsigned int cli_CapableNumSpatialStreams;
} sta_data_t;

typedef struct {
    unsigned int        rapid_reconnect_threshold;
    unsigned int        ap_rejected_sta_count;
    char                last_time_ap_rejected_sta[128];
} ap_params_t;

typedef struct {
	unsigned char		bssid[32];
	hash_map_t		*sta_map;
	ap_params_t		ap_params;
	ssid_t                  ssid;
} bssid_data_t;

typedef struct {
       char                    frequency_band[64];
       char                    ChannelsInUse[256];
       unsigned int            primary_radio_channel;
       char                    channel_bandwidth[64];
       unsigned int            RadioActivityFactor;
       unsigned int            CarrierSenseThreshold_Exceeded;
       int                     NoiseFloor;
       int                     channelUtil;
       int                     channelInterference;
} radio_data_t;

typedef struct {
       int       ch_number;
       unsigned long long ch_utilization_busy_tx;
       unsigned long long ch_utilization_busy_self;
       unsigned long long LastUpdatedTime;
} radio_chan_data_t;

typedef struct {
    queue_t             *queue;
    bssid_data_t        bssid_data[MAX_VAP];
#ifdef WIFI_HAL_VERSION_3
    radio_data_t        radio_data[MAX_NUM_RADIOS];
    radio_chan_data_t   radio_channel_data[MAX_NUM_RADIOS];
#else
    radio_data_t        radio_data[MAX_RADIOS];
    radio_chan_data_t   radio_channel_data[MAX_RADIOS];
#endif
    pthread_cond_t      cond;
    pthread_mutex_t     lock;
    pthread_t           id;
    bool                exit_monitor;
    unsigned int        blastReqInQueueCount;
    unsigned int        poll_period;
    unsigned int        upload_period;
    unsigned int        current_poll_iter;
	instant_msmt_t		inst_msmt;
    struct timeval      last_signalled_time;
    struct timeval      last_polled_time;
    rssi_t		sta_health_rssi_threshold;
    int                 sysevent_fd;
    unsigned int        sysevent_token;
    ap_params_t      	ap_params[MAX_VAP];
    char 		cliStatsList[MAX_VAP];
    int			count;
    int			maxCount;
    int			instantDefReportPeriod;
    int			instantDefOverrideTTL;
    int			instantPollPeriod;
    bool        instntMsmtenable;
    char        instantMac[MIN_MAC_ADDR_LEN];
    struct scheduler *sched;
    int chutil_id;
    int client_telemetry_id;
    int client_debug_id;
    int channel_width_telemetry_id;
    int ap_telemetry_id;
    int vap_max_client_id;
    int inst_msmt_id;
    int curr_chan_util_period;
    int refresh_task_id;
    int associated_devices_id;
    int vap_status_id;
    int radio_diagnostics_id;
    int radio_health_telemetry_logger_id;
    int upload_ap_telemetry_pmf_id;
#if defined (FEATURE_OFF_CHANNEL_SCAN_5G)
    int off_channel_scan_id;
    int curr_off_channel_scan_period;
#endif
#if defined (FEATURE_CSI)
    int clientdiag_id[MAX_VAP];
    int clientdiag_sched_arg[MAX_VAP];
    unsigned int clientdiag_sched_interval[MAX_VAP];
    int csi_sched_id;
    unsigned int csi_sched_interval;
#endif
} wifi_monitor_t;

#if defined (FEATURE_CSI)
typedef struct {
    unsigned int        interval;
} diag_data_session_t;

typedef struct {
    queue_t             *csi_queue;
    diag_data_session_t diag_session[MAX_VAP];
    char vap_ip[MAX_VAP][IP_STR_LEN];
    pthread_mutex_t     lock;
} events_monitor_t;

typedef struct {
    bool enable;
    bool subscribed;
    bool mac_is_connected[MAX_CSI_CLIENTS_PER_SESSION];
    int  csi_time_interval;
    int  no_of_mac;
    int  csi_sess_number;
    int  ap_index[MAX_CSI_CLIENTS_PER_SESSION];
    mac_addr_t mac_list[MAX_CSI_CLIENTS_PER_SESSION];
    char client_ip[MAX_CSI_CLIENTS_PER_SESSION][IP_STR_LEN];
    long  client_ip_age[MAX_CSI_CLIENTS_PER_SESSION];
    struct timeval last_snapshot_time;
} __attribute__((__packed__)) csi_session_t;

void csi_update_client_mac_status(mac_addr_t mac, bool connected, int ap_idx);
void csi_set_client_mac(char *mac_list, int csi_session_number);
void csi_enable_session(bool enable, int csi_session_number);
void csi_enable_subscription(bool subscribe, int csi_session_number);
void csi_set_interval(int interval, int csi_session_number);
void csi_create_session(int csi_session_number);
void csi_del_session(int csi_sess_number);
void diagdata_set_interval(int interval, unsigned int ap_idx);
#endif

int
wifi_stats_flag_change
    (
        int             ap_index,
        bool            enable,
        int             type
    );
wifi_monitor_t *get_wifi_monitor ();
char *get_formatted_time(char *time);
void write_to_file(const char *file_name, char *fmt, ...);
wifi_actvie_msmt_t *get_active_msmt_data();
int radio_stats_flag_change(int radio_index, bool enable);
int vap_stats_flag_change(int ap_index, bool enable);
int init_wifi_monitor();
void monitor_enable_instant_msmt(mac_address_t sta_mac, bool enable);
bool monitor_is_instant_msmt_enabled();
void instant_msmt_reporting_period(int pollPeriod);
void instant_msmt_macAddr(char *mac_addr);
void instant_msmt_ttl(int overrideTTL);
void instant_msmt_def_period(int defPeriod);
void SetINSTReportingPeriod(unsigned long pollPeriod);
void SetINSTDefReportingPeriod(int defPeriod);
void SetINSTOverrideTTL(int defTTL);
void SetINSTMacAddress(char *mac_addr);
int GetInstAssocDevSchemaIdBufferSize();
unsigned int GetINSTPollingPeriod();
unsigned int GetINSTOverrideTTL();
unsigned int GetINSTDefReportingPeriod();
void get_device_flag(char flag[], char *psmcli);
#endif	//_WIFI_MON_H_
