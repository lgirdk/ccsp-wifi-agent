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

#include <telemetry_busmessage_sender.h>
#include "cosa_wifi_apis.h"
#include "ccsp_psm_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include "collection.h"
#include "wifi_hal.h"
#include "wifi_monitor.h"
#include "wifi_blaster.h"
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include <time.h>
#include <sys/un.h>
#include <assert.h>
#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
#include <rbus/rbus.h>
#endif
#include "ansc_status.h"
#include <sysevent/sysevent.h>
#include "ccsp_base_api.h"
#include "harvester.h"
#include "cosa_wifi_passpoint.h"
#include "safec_lib_common.h"
#include "ccsp_WifiLog_wrapper.h"


#if defined (FEATURE_CSI)
#include <netinet/tcp.h>    //Provides declarations for tcp header
#include <netinet/ip.h> //Provides declarations for ip header
#include <arpa/inet.h> // inet_addr
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/ip_icmp.h>
#include <sys/stat.h>
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include "wifi_events.h"

#define NDA_RTA(r) \
                ((struct rtattr *)(((char *)(r)) + NLMSG_ALIGN(sizeof(struct ndmsg))))
static events_monitor_t g_events_monitor;
static struct timeval csi_prune_timer;
#endif

extern void* bus_handle;
extern char g_Subsystem[32];
#define DEFAULT_CHANUTIL_LOG_INTERVAL 900
#define SINGLE_CLIENT_WIFI_AVRO_FILENAME "WifiSingleClient.avsc"
#define MIN_MAC_LEN 12
#define DEFAULT_INSTANT_POLL_TIME 5
#define DEFAULT_INSTANT_REPORT_TIME 0

#define RADIO_HEALTH_TELEMETRY_INTERVAL_MS 900000 //15 minutes
#define REFRESH_TASK_INTERVAL_MS 5*60*1000 //5 minutes
#define ASSOCIATED_DEVICE_DIAG_INTERVAL_MS 5*60*1000 // 5 minutes
#define CAPTURE_VAP_STATUS_INTERVAL_MS 5*60*1000 // 5 minutes
#define UPLOAD_AP_TELEMETRY_INTERVAL_MS 24*60*60*1000 // 24 Hours
#define RADIO_STATS_INTERVAL_MS 30000 //30 seconds

#define MIN_TO_MILLISEC 60000
#define SEC_TO_MILLISEC 1000

#define SIZE_OF_BUFF 8

#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
#define SPEEDTEST_STATUS "Device.IP.Diagnostics.X_RDKCENTRAL-COM_SpeedTest.Status"
#define SPEEDTEST_SUBSCRIBE "Device.IP.Diagnostics.X_RDK_SpeedTest.SubscriberUnPauseTimeOut"
#define ARRAY_SZ(x)    (sizeof(x) / sizeof((x)[0]))

void speedtest_handler (rbusHandle_t handle, rbusEvent_t const* event, rbusEventSubscription_t* subscription);
void update_speedtest_tout_value();

rbusEventSubscription_t st_events[] = {
        { SPEEDTEST_STATUS, NULL, 0, 0, speedtest_handler, NULL, NULL, NULL, false},
        { SPEEDTEST_SUBSCRIBE, NULL, 0, 0, speedtest_handler, NULL, NULL, NULL, false},
};

rbusHandle_t st_handle;
char g_component_name[RBUS_MAX_NAME_LENGTH];
int speedtest_timeout = 1;
#endif

char *instSchemaIdBuffer = "8b27dafc-0c4d-40a1-b62c-f24a34074914/4ce3404669247e94eecb36cf3af21750";

static wifi_monitor_t g_monitor_module;
static wifi_actvie_msmt_t g_active_msmt;
static unsigned msg_id = 1000;
static const char *wifi_health_log = "/rdklogs/logs/wifihealth.txt";
static unsigned int vap_up_arr[MAX_VAP]={0};
static unsigned int vap_iteration=0;
static unsigned char vap_nas_status[MAX_VAP]={0};
#if defined (DUAL_CORE_XB3)
static unsigned char erouterIpAddrStr[32];
unsigned char wifi_pushSecureHotSpotNASIP(int apIndex, unsigned char erouterIpAddrStr[]);
#endif
int radio_stats_monitor = 0;
int chan_util_upload_period = 0;
time_t lastupdatedtime = 0;
time_t chutil_last_updated_time = 0;
time_t lastpolledtime = 0;
ULONG radio_health_last_updated_time = 0;

#if defined (FEATURE_OFF_CHANNEL_SCAN_5G)
#define DEFAULT_RADIO_X_RDK_OFFCHANN_RFC_ENABLE 0          // 5G Off channel scan feature is off by default. can be enabled thru RFC
#define DEFAULT_RADIO_X_RDK_OFFCHANNELTSCAN 63             //Default Dwell time  value 63 milli seconds
#define DEFAULT_RADIO_X_RDK_OFFCHANNELNSCAN 10800          //Default Scan frequency per day is 8. (24/8) = 3Hrs ==> which is 3 * 3600 seconds = 10800
#define DEFAULT_RADIO_X_RDK_OFFCHANNEL_TIDLE 5             //Default TIDLE Value 5 seconds

#define RADIO_OFF_CHANNEL_NO_OF_PARAMS 4
#define MAX_5G_CHANNELS                25
#define RADIO_5G                       1
#define DFS_CH_START                   52
#define DFS_CH_END                     144

// This integer array holds the default value of the parameters used in 5G off channel scan
int Radio_Off_Channel_Default[RADIO_OFF_CHANNEL_NO_OF_PARAMS] = {
    DEFAULT_RADIO_X_RDK_OFFCHANN_RFC_ENABLE,
    DEFAULT_RADIO_X_RDK_OFFCHANNELTSCAN,
    DEFAULT_RADIO_X_RDK_OFFCHANNELNSCAN,
    DEFAULT_RADIO_X_RDK_OFFCHANNEL_TIDLE
};

// This integer array holds the current value of the parameters used in 5G off channel scan, this array is updated dynamically based on the poll period i.e, NSCAN
int Radio_Off_Channel_current[RADIO_OFF_CHANNEL_NO_OF_PARAMS] ={0};

time_t Off_Channel_5g_last_updated_time = 0;
int Off_Channel_5g_upload_period = 0;

static int update_Off_Channel_5g_Scan_Params(void);
static int  Off_Channel_5g_scanner(void *arg);
static void Off_Channel_5g_print_data();
#define arrayDup(DST,SRC,LEN) \
    size_t TMPSZ = sizeof(*(SRC)) * (LEN); \
    if ( ((DST) = malloc(TMPSZ)) != NULL ) \
        memcpy((DST), (SRC), TMPSZ);
#endif // (FEATURE_OFF_CHANNEL_SCAN_5G)

pthread_mutex_t g_apRegister_lock = PTHREAD_MUTEX_INITIALIZER;
void deinit_wifi_monitor    (void);
int device_deauthenticated(int apIndex, char *mac, int reason);
int device_associated(int apIndex, wifi_associated_dev_t *associated_dev);

unsigned int get_upload_period  (int);
long get_sys_uptime();
void process_disconnect    (unsigned int ap_index, auth_deauth_dev_t *dev);
static void logVAPUpStatus();
extern BOOL sWiFiDmlvApStatsFeatureEnableCfg;

#if defined (FEATURE_HOSTAP_AUTHENTICATOR)
BOOL isLibHostapDisAssocEvent = FALSE;
#endif


int executeCommand(char* command,char* result);
void associated_client_diagnostics();
void process_instant_msmt_stop (unsigned int ap_index, instant_msmt_t *msmt);
void process_instant_msmt_start        (unsigned int ap_index, instant_msmt_t *msmt);
void process_active_msmt_step();
void get_self_bss_chan_statistics (int radiocnt , UINT *Tx_perc, UINT  *Rx_perc);
int get_chan_util_upload_period(void);
static int configurePktgen(pktGenConfig* config);

static void upload_client_debug_stats_acs_stats(int apIndex);
static void upload_client_debug_stats_sta_fa_info(int apIndex, sta_data_t *sta);
static void upload_client_debug_stats_sta_fa_lmac_data_stats(int apindex, sta_data_t *sta);
static void upload_client_debug_stats_sta_fa_lmac_mgmt_stats(int apIndex, sta_data_t *sta);
static void upload_client_debug_stats_sta_vap_activity_stats(int apIndex);
static void upload_client_debug_stats_transmit_power_stats(int apIndex);
static void upload_client_debug_stats_chan_stats(int apIndex);

int associated_devices_diagnostics(void *arg);
int process_instant_msmt_monitor(void *arg);
static int refresh_task_period(void *arg);
int upload_radio_chan_util_telemetry(void *arg);
int radio_diagnostics(void *arg);
int associated_device_diagnostics_send_event(void *arg);
static void scheduler_telemetry_tasks(void);
int upload_vap_rejection_sta_count_telemetry(void *arg);
#if (defined(_CBR_PRODUCT_REQ_) && !defined(_CBR2_PRODUCT_REQ_))
int upload_vap_rejection_sta_count_telemetry_cbr(int apIndex);
int upload_vap_rejection_sta_count_telemetry_cbr_time(int apIndex);
#endif

#if defined (FEATURE_CSI)

#if defined (_XB7_PRODUCT_REQ_)
#define FEATURE_CSI_CALLBACK 1
#endif

#if defined (FEATURE_CSI_CALLBACK)
INT process_csi(mac_address_t mac_addr, wifi_csi_data_t  *csi_data);
#endif

int csi_getCSIData(void * arg);
int csi_sendPingData(void * arg);
static void csi_refresh_session(void);
static int csi_sheduler_enable(void);
static int clientdiag_sheduler_enable(int ap_index);
static void csi_vap_down_update(int ap_idx);
static void csi_disable_client(csi_session_t *r_csi);
#endif //FEATURE_CSI

pktGenConfig config;
pktGenFrameCountSamples  *frameCountSample = NULL;
pthread_t startpkt_thread_id = 0;

static inline char *to_sta_key    (mac_addr_t mac, sta_key_t key) {
    snprintf(key, STA_KEY_LEN, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return (char *)key;
}

static void to_mac_bytes (mac_addr_str_t key, mac_address_t bmac) {
   unsigned int mac[6];
   if(strlen(key) > MIN_MAC_LEN)
       sscanf(key, "%02x:%02x:%02x:%02x:%02x:%02x",
             &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
   else
       sscanf(key, "%02x%02x%02x%02x%02x%02x",
             &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
   bmac[0] = mac[0]; bmac[1] = mac[1]; bmac[2] = mac[2];
   bmac[3] = mac[3]; bmac[4] = mac[4]; bmac[5] = mac[5];

}


static void to_plan_id (unsigned char *PlanId, unsigned char Plan[])
{
    int i=0;
    for (i=0; i < PLAN_ID_LENGTH; i++)
    {
         sscanf((char*)PlanId,"%2hhx",(char*)&Plan[i]);
         PlanId += 2;
    }
}

char *get_formatted_time(char *time)
{
    struct tm *tm_info;
    struct timeval tv_now;
    char tmp[128];
    errno_t rc = -1;

    gettimeofday(&tv_now, NULL);
    tm_info = localtime(&tv_now.tv_sec);

    strftime(tmp, 128, "%y%m%d-%T", tm_info);
    
    rc = sprintf_s(time, 128, "%s.%06d", tmp, (int)tv_now.tv_usec);
    if(rc < EOK) ERR_CHK(rc);
    return time;
}

void write_to_file(const char *file_name, char *fmt, ...)
{
    FILE *fp = NULL;
    va_list args;

    fp = fopen(file_name, "a+");
    if (fp == NULL) {
        return;
    }
   
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    fflush(fp);
    fclose(fp);
}

BOOL check_ErrorsReceivedRFC_enabled() {
    char *ErrorsReceivedRFC = "DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.ErrorsReceived.Enable", *strValue = NULL;
    BOOL enable = FALSE;
    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem, ErrorsReceivedRFC, NULL, &strValue) != CCSP_SUCCESS)
    {
      CcspTraceError(("%s: fail to get PSM record for %s!\n", __FUNCTION__, ErrorsReceivedRFC));
    }
    else
    {
      enable = atoi(strValue);
      ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }
    return enable;
}

#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : speedtest_handler                                             */
/*                                                                               */
/* DESCRIPTION   : Update the speedtest status and timeout values via rbus       */
/*                 evemts and push the corresponding events to queue             */
/*                                                                               */
/* INPUT         : Arg 1 : rbus handler                                          */
/*                 Arg 2 : speedtest events which changes its value              */
/*                 Arg 3 : subscribed events which contains change in values     */
/*                                                                               */
/*  OUTPUT        : NONE                                                         */
/*                                                                               */
/*********************************************************************************/

void speedtest_handler (rbusHandle_t handle, rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    rbusValue_t value;
    wifi_monitor_data_t *data = NULL;
    int speedtest_running = 0;
    char * pTmp = NULL;

    if(!event) {
        wifi_dbg_print(1, "%s: %d Invalid event received\n", __func__, __LINE__);
        return;
    }

    if (event->data == NULL) {
        wifi_dbg_print(1, "%s: %d Invalid event data received\n", __func__, __LINE__);
        return;
    }

    value = rbusObject_GetValue(event->data, "value");
    if (!value) {
        wifi_dbg_print(1, "%s:%d: Invalid value for event:%s", __func__, __LINE__,
                           subscription->eventName);
        return;
    }

    pTmp =  rbusValue_ToString(value, NULL, 0);
    if(pTmp == NULL) {
        wifi_dbg_print(1, "%s:%d: Unable to get data for event:%s\n", __func__, __LINE__,
                           subscription->eventName);
        return;
    }

    wifi_dbg_print(1, "%s: %d event name : [%s] Data received : [%u]\n", __func__, __LINE__,
                       event->name, atoi(pTmp));

    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    if (data == NULL) {
        wifi_dbg_print(1, "%s:%d: Malloc failed\n", __func__, __LINE__);
        free(pTmp);
        pTmp = NULL;
        return;
    }
    memset(data, 0, sizeof(wifi_monitor_data_t));

    if ((strcmp(subscription->eventName, SPEEDTEST_STATUS)) == 0) {
        speedtest_running = atoi(pTmp);

        if (speedtest_running == 1) {
            data->id = msg_id++;
            data->event_type = monitor_event_type_speedtest_started;
            pthread_mutex_lock(&g_monitor_module.lock);
            queue_push(g_monitor_module.queue, data);
            pthread_cond_signal(&g_monitor_module.cond);
            pthread_mutex_unlock(&g_monitor_module.lock);
        }

        else if (speedtest_running == 5) {
            data->event_type = monitor_event_type_speedtest_completed;
            pthread_mutex_lock(&g_monitor_module.lock);
            queue_push(g_monitor_module.queue, data);
            pthread_cond_signal(&g_monitor_module.cond);
            pthread_mutex_unlock(&g_monitor_module.lock);
        }
    }

    else if ((strcmp(subscription->eventName, SPEEDTEST_SUBSCRIBE)) == 0) {
        data->id = msg_id++;
        data->u.flag.type = atoi(pTmp);
        data->event_type = monitor_event_type_speedtest_timeout;
        pthread_mutex_lock(&g_monitor_module.lock);
        queue_push(g_monitor_module.queue, data);
        pthread_cond_signal(&g_monitor_module.cond);
        pthread_mutex_unlock(&g_monitor_module.lock);

    }

    free(pTmp);
    pTmp = NULL;
}
#endif

/* get_self_bss_chan_statistics () will get channel statistics from driver and calculate self bss channel utilization */


void get_self_bss_chan_statistics (int radiocnt , UINT *Tx_perc, UINT  *Rx_perc)
{
    ULONG timediff = 0, currentchannel = 0;
    wifi_channelStats_t chan_stats = {0};
    ULLONG Tx_count = 0, Rx_count = 0;
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);

    ULONG currentTime = tv_now.tv_sec;
    ULLONG bss_total = 0;
    *Tx_perc = 0;
    *Rx_perc = 0;
    if (wifi_getRadioChannel(radiocnt, &currentchannel) == RETURN_OK)
    {
        chan_stats.ch_number = currentchannel;
        chan_stats.ch_in_pool= TRUE;
        if (wifi_getRadioChannelStats(radiocnt, &chan_stats, 1) == RETURN_OK)
        {
            timediff = currentTime - g_monitor_module.radio_channel_data[radiocnt].LastUpdatedTime;
            /* if the last poll was within 5 seconds skip the  calculation*/
            if  (timediff > 5 )
            {
                g_monitor_module.radio_channel_data[radiocnt].LastUpdatedTime = currentTime;
                if ((g_monitor_module.radio_channel_data[radiocnt].ch_number == chan_stats.ch_number) && 
                   (chan_stats.ch_utilization_busy_tx > g_monitor_module.radio_channel_data[radiocnt].ch_utilization_busy_tx))
                {
                    Tx_count = chan_stats.ch_utilization_busy_tx - g_monitor_module.radio_channel_data[radiocnt].ch_utilization_busy_tx;
                }
                else
                {
                    Tx_count = chan_stats.ch_utilization_busy_tx;
                }

                if ((g_monitor_module.radio_channel_data[radiocnt].ch_number == chan_stats.ch_number) && 
                   (chan_stats.ch_utilization_busy_self > g_monitor_module.radio_channel_data[radiocnt].ch_utilization_busy_self))
                {
                    Rx_count =  chan_stats.ch_utilization_busy_self - g_monitor_module.radio_channel_data[radiocnt].ch_utilization_busy_self;
                }
                else
                {
                    Rx_count = chan_stats.ch_utilization_busy_self;

                }
            }
            wifi_dbg_print(1, "%s: %d Radio %d Current channel %d new stats Tx_self : %llu Rx_self: %llu  \n"
                           ,__FUNCTION__,__LINE__,radiocnt,chan_stats.ch_number,chan_stats.ch_utilization_busy_tx
                           , chan_stats.ch_utilization_busy_self);

            bss_total = Tx_count + Rx_count;
            if (bss_total)
            {
                *Tx_perc = (UINT)round( (float) Tx_count / bss_total * 100 );
                *Rx_perc = (UINT)round( (float) Rx_count / bss_total * 100 );
            }
            wifi_dbg_print(1,"%s: %d Radio %d channel stats Tx_count : %llu Rx_count: %llu Tx_perc: %d Rx_perc: %d\n"
                            ,__FUNCTION__,__LINE__,radiocnt,Tx_count, Rx_count, *Tx_perc, *Rx_perc);
            /* Update prev var for next call */
            g_monitor_module.radio_channel_data[radiocnt].ch_number = chan_stats.ch_number;
            g_monitor_module.radio_channel_data[radiocnt].ch_utilization_busy_tx = chan_stats.ch_utilization_busy_tx;
            g_monitor_module.radio_channel_data[radiocnt].ch_utilization_busy_self = chan_stats.ch_utilization_busy_self;
        }
        else
        {
            wifi_dbg_print(1, "%s : %d wifi_getRadioChannelStats failed for rdx : %d\n",__func__,__LINE__,radiocnt);
        }
    }
    return;
}

// upload_radio_chan_util_telemetry()  will update the channel stats in telemetry marker
int upload_radio_chan_util_telemetry(void *arg)
{
    static int radiocnt = 0;
    static int total_radiocnt = 0;
    static int new_chan_util_period = 0;
    UINT  Tx_perc = 0, Rx_perc = 0;
    UINT bss_Tx_cu = 0 , bss_Rx_cu = 0;
    char tmp[128] = {0};
    char log_buf[1024] = {0};
    char telemetry_buf[1024] = {0};
    BOOL radio_Enabled = FALSE;
    errno_t rc = -1;
  
    if (total_radiocnt == 0) {
#ifdef WIFI_HAL_VERSION_3
        total_radiocnt = (int)getNumberRadios();
#else
        if (wifi_getRadioNumberOfEntries((ULONG*)&total_radiocnt) != RETURN_OK)
        {
            wifi_dbg_print(1, "%s : %d Failed to get radio count\n",__func__,__LINE__);
            radiocnt = 0;
            return TIMER_TASK_ERROR;
        }
#endif
    }
    {
        if (wifi_getRadioEnable(radiocnt, &radio_Enabled) == RETURN_OK)
        {
            if (radio_Enabled)
            {
                get_self_bss_chan_statistics(radiocnt, &Tx_perc, &Rx_perc);

                /* calculate Self bss Tx and Rx channel utilization */

                bss_Tx_cu = (UINT)round( (float) g_monitor_module.radio_data[radiocnt].RadioActivityFactor * Tx_perc / 100 );
                bss_Rx_cu = (UINT)round( (float) g_monitor_module.radio_data[radiocnt].RadioActivityFactor * Rx_perc / 100 );

                wifi_dbg_print(1,"%s: channel Statistics results for Radio %d: Activity: %d AFTX : %d AFRX : %d ChanUtil: %d CSTE: %d\n"
                               ,__func__, radiocnt, g_monitor_module.radio_data[radiocnt].RadioActivityFactor
                               ,bss_Tx_cu,bss_Rx_cu,g_monitor_module.radio_data[radiocnt].channelUtil
                               ,g_monitor_module.radio_data[radiocnt].CarrierSenseThreshold_Exceeded);

                // Telemetry:
                // "header":  "CHUTIL_1_split"
                // "content": "CHUTIL_1_split:"
                // "type": "wifihealth.txt",
                rc = sprintf_s(telemetry_buf, sizeof(telemetry_buf), "%d,%d,%d", bss_Tx_cu, bss_Rx_cu, g_monitor_module.radio_data[radiocnt].CarrierSenseThreshold_Exceeded);
                if(rc < EOK)
                {
                    ERR_CHK(rc);
                }
                get_formatted_time(tmp);
#ifdef WIFI_HAL_VERSION_3
                rc = sprintf_s(log_buf, sizeof(log_buf), "%s CHUTIL_%d_split:%s\n", tmp, getPrivateApFromRadioIndex(radiocnt)+1, telemetry_buf);
#else
                rc = sprintf_s(log_buf, sizeof(log_buf), "%s CHUTIL_%d_split:%s\n", tmp, ((radiocnt == 2)?15:radiocnt+1), telemetry_buf);
#endif
                if(rc < EOK)
                {
                    ERR_CHK(rc);
                }
                write_to_file(wifi_health_log, log_buf);
                wifi_dbg_print(1, "%s", log_buf);

#ifdef WIFI_HAL_VERSION_3
                rc = sprintf_s(tmp, sizeof(tmp), "CHUTIL_%d_split", getPrivateApFromRadioIndex(radiocnt)+1);
#else
                rc = sprintf_s(tmp, sizeof(tmp), "CHUTIL_%d_split", ((radiocnt == 2)?15:radiocnt+1));
#endif
                if(rc < EOK)
                {
                    ERR_CHK(rc);
                }
                t2_event_s(tmp, telemetry_buf);
            }
            else
            {
                wifi_dbg_print(1, "%s : %d Radio : %d is not enabled\n",__func__,__LINE__,radiocnt);
            }
        }
        else
        {
            wifi_dbg_print(1, "%s : %d wifi_getRadioEnable failed for rdx : %d\n",__func__,__LINE__,radiocnt);
        }
    }

    radiocnt++;
    if (radiocnt >= total_radiocnt) {
        radiocnt = 0;
        new_chan_util_period = get_chan_util_upload_period();
        if((g_monitor_module.curr_chan_util_period != new_chan_util_period) 
               && (new_chan_util_period != 0)) {
            scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.chutil_id, new_chan_util_period*1000);
            g_monitor_module.curr_chan_util_period = new_chan_util_period;
        }
        return TIMER_TASK_COMPLETE;
    }
    return TIMER_TASK_CONTINUE;
}

int radio_health_telemetry_logger(void *arg)
{
    int output_percentage = 0;
    unsigned int i = 0;
    char buff[256] = {0}, tmp[128] = {0}, telemetry_buf[64] = {0};
    BOOL radio_Enabled=FALSE;
#ifdef WIFI_HAL_VERSION_3
    for (i = 0; i < getNumberRadios(); i++)
#else
    for (i = 0; i < MAX_RADIOS; i++)
#endif
    {
        memset(buff, 0, sizeof(buff));
        memset(tmp, 0, sizeof(tmp));
        get_formatted_time(tmp);
        wifi_getRadioEnable(i, &radio_Enabled);
        //Printing the utilization of Radio if and only if the radio is enabled
        if(radio_Enabled) {
            wifi_getRadioBandUtilization(i, &output_percentage);
            snprintf(buff, 256, "%s WIFI_BANDUTILIZATION_%d:%d\n", tmp, i + 1, output_percentage);
            memset(tmp, 0, sizeof(tmp));
#ifdef WIFI_HAL_VERSION_3
            snprintf(tmp, sizeof(tmp), "Wifi_%dG_utilization_split", convertRadioIndexToFrequencyNum(i));
#else
            snprintf(tmp, sizeof(tmp), ((i == 0) ? "Wifi_2G_utilization_split": "Wifi_5G_utilization_split"));
#endif
            //updating T2 Marker here
            memset(telemetry_buf, 0, sizeof(telemetry_buf));
            snprintf(telemetry_buf, sizeof(telemetry_buf), "%d", output_percentage);
            t2_event_s(tmp, telemetry_buf);
        } else {
            snprintf(buff, 256, "%s Radio_%d is down, so not printing WIFI_BANDUTILIZATION marker", tmp, i + 1);
        }
        write_to_file(wifi_health_log, buff);
    }
    return TIMER_TASK_COMPLETE;
}

/* Function to calculate time for CBR last rejected sta time for deauth 17*/
#if (defined(_CBR_PRODUCT_REQ_) && !defined(_CBR2_PRODUCT_REQ_))
int upload_vap_rejection_sta_count_telemetry_cbr_time(INT apIndex)
{
    char cmd[256] = {0};
    snprintf(cmd,sizeof(cmd),"journalctl |  grep 'BRCM-WIFI' |grep 'denied association due to max' | grep -v '\\[' | grep 'wl%d' | tail -1 | awk '{print $1,$2,$3,$4}'",apIndex);
    char buff[24] = {0};
    FILE* pipe = NULL;
    pipe = (FILE*)(long int)v_secure_popen("r",cmd);
    if(pipe == NULL)
    {
        wifi_dbg_print(1,"Pipe opening has failed in %s\n",__FUNCTION__);
        return RETURN_ERR;
    }
    fgets(buff,sizeof(buff),pipe);
    v_secure_pclose(pipe);
    int i = 1;
    char year[5] = {0};
    char mon[4] = {0};
    char day[3] = {0};
    char time_deauth[9] = {0};
    char* token = strtok(buff, " ");
    while(token)
    {
        if(i == 1)
        {
            strncpy(year,token+2,sizeof(year));
        }
        else if(i == 2)
        {
            strncpy(mon,token,sizeof(mon));
        }
        else if(i == 3)
        {
            strncpy(day,token,sizeof(day));
        }
        else if(i == 4)
        {
            strncpy(time_deauth,token,sizeof(time_deauth));
        }
        else
        {
            break;
        }
        i++;
        token = strtok(NULL," ");
    }
    char formatted_time[24] = {0};
    int today = 0;
    if(strcmp(mon,"Jan") == 0)
    {
        today = 1;
    }
    else if(strcmp(mon,"Feb") == 0)
    {
        today = 2;
    }
    else if(strcmp(mon,"Mar") == 0)
    {
        today = 3;
    }
    else if(strcmp(mon,"Apr") == 0)
    {
        today = 4;
    }
    else if(strcmp(mon,"May") == 0)
    {
        today = 5;
    }
    else if(strcmp(mon,"Jun") == 0)
    {
        today = 6;
    }
    else if(strcmp(mon,"Jul") == 0)
    {
        today = 7;
    }
    else if(strcmp(mon,"Aug") == 0)
    {
        today = 8;
    }
    else if(strcmp(mon,"Sep") == 0)
    {
        today = 9;
    }
    else if(strcmp(mon,"Oct") == 0)
    {
        today = 10;
    }
    else if(strcmp(mon,"Nov") == 0)
    {
        today = 11;
    }
    else if(strcmp(mon,"Dec") == 0)
    {
        today = 12;
    }
    else
    {
        wifi_dbg_print(1,"Invalid month in %s returning status: %d\n",__FUNCTION__,RETURN_ERR);
        return RETURN_ERR;
    }
    if(today < 10)
    {
        snprintf(formatted_time, sizeof(formatted_time), "%s0%d%s-%s",year,today,day,time_deauth);
    }
    else
    {
        snprintf(formatted_time, sizeof(formatted_time), "%s%d%s-%s",year,today,day,time_deauth);
    }
    strncpy(g_monitor_module.ap_params[apIndex].last_time_ap_rejected_sta, formatted_time, sizeof(g_monitor_module.ap_params[apIndex].last_time_ap_rejected_sta));
    return RETURN_OK;
}

/* The upload_vap_rejection_sta_count_telemetry update rejected client counts to telemetry for CBR as reason code 17 is not available ( Association denied because AP is unable to handle additional associated stations(*/
int upload_vap_rejection_sta_count_telemetry_cbr(INT apIndex)
{
    char cmd[256] = {0};
    snprintf(cmd,sizeof(cmd), "journalctl |  grep 'BRCM-WIFI' |grep 'denied association due to max' | grep -v '\\[' | grep 'wl%d' | wc -l",apIndex);
    static int prev_count_1 = 0;
    static int prev_count_2 = 0;
    char* buff = (char*)malloc(SIZE_OF_BUFF*sizeof(char));
    if(buff == NULL)
    {
        wifi_dbg_print(1,"Memory allocation has failed in %s\n",__FUNCTION__);
        return RETURN_ERR;
    }
    FILE* pipe = NULL;
    pipe =(FILE*)(long int) v_secure_popen("r",cmd);
    if(pipe == NULL)
    {
        wifi_dbg_print(1,"Pipe opening has failed in %s\n",__FUNCTION__);
        free(buff);
        buff = NULL;
        return RETURN_ERR;
    }
    fgets(buff,SIZE_OF_BUFF,pipe);
    v_secure_pclose(pipe);
    int buff_value = atoi(buff);
    if(apIndex == 0)
    {
        if(buff_value >= prev_count_1)
        {
            g_monitor_module.ap_params[apIndex].ap_rejected_sta_count = buff_value - prev_count_1;
        }
        else
        {
            g_monitor_module.ap_params[apIndex].ap_rejected_sta_count = buff_value;
        }
        prev_count_1 = buff_value;
        if(buff_value == 0)
        {
            return RETURN_OK;
        }
    }
    else if(apIndex == 1)
    {
        if(buff_value >= prev_count_2)
        {
            g_monitor_module.ap_params[apIndex].ap_rejected_sta_count = buff_value - prev_count_2;
        }
        else
        {
            g_monitor_module.ap_params[apIndex].ap_rejected_sta_count = buff_value;
        }
        prev_count_2 = buff_value;
        if(buff_value == 0)
        {
            return RETURN_OK;
        }
    }
    free(buff);
    buff = NULL;
    int res = upload_vap_rejection_sta_count_telemetry_cbr_time(apIndex);
    if(res != RETURN_ERR)
    {
        return RETURN_OK;
    }
    else
    {
        wifi_dbg_print(1,"%s failed with return status : %d\n",__FUNCTION__,res);
        return RETURN_ERR;
    }
}
#endif

/* The upload_vap_rejection_sta_count_telemetry update rejected client couts to telemetry for reason code 17 ( Association denied because AP is unable to handle additional associated stations(*/

int upload_vap_rejection_sta_count_telemetry (void *arg)
{
    int VapIndex = 0;
    char VapStatus[16] = {0} , LogBuf[1024] = {0}, Temp[128] = {0};
    ap_params_t *ap_params = NULL;
    wifi_dbg_print(1, "Entering %s:%d \n", __FUNCTION__, __LINE__);
#ifdef WIFI_HAL_VERSION_3
    for ( VapIndex = 0; VapIndex < (int)getTotalNumberVAPs(); VapIndex++)
#else
    for ( VapIndex = 0; VapIndex < MAX_VAP; VapIndex++)
#endif
    {
        if ( wifi_getApStatus(VapIndex, VapStatus) == RETURN_OK ) {

            if ( strncasecmp(VapStatus, "Up", 2) == 0 ) {

                ap_params =  &g_monitor_module.ap_params[VapIndex];
#if (defined(_CBR_PRODUCT_REQ_) && !defined(_CBR2_PRODUCT_REQ_))
                int res = upload_vap_rejection_sta_count_telemetry_cbr(VapIndex);
                if(res == RETURN_OK)
                {
#endif
                if ( ap_params->ap_rejected_sta_count ) {

                    get_formatted_time(Temp);
                    snprintf(LogBuf, sizeof(LogBuf), "%s RejectedClientsCountMaxThreshold_%d:%u\n", Temp, VapIndex+1, ap_params->ap_rejected_sta_count);
                    write_to_file(wifi_health_log, LogBuf);
                    wifi_dbg_print(1, "%s", LogBuf);

                    if ( ap_params->last_time_ap_rejected_sta != NULL ) {
                        wifi_dbg_print(1, "%s :  For Vap %d => ap_rejected_sta_count %d , ap_rejected_last_client %s\n", __FUNCTION__, VapIndex,ap_params->ap_rejected_sta_count, ap_params->last_time_ap_rejected_sta);

                        memset(LogBuf, 0, sizeof(LogBuf));
                        snprintf(LogBuf, sizeof(LogBuf), "%s LastClientRejectedTime_%d_split:%s\n", Temp, VapIndex+1, ap_params->last_time_ap_rejected_sta);
                        write_to_file(wifi_health_log, LogBuf);
                        wifi_dbg_print(1, "%s", LogBuf);
                    }
                    g_monitor_module.ap_params[VapIndex].ap_rejected_sta_count = 0;
                    memset(g_monitor_module.ap_params[VapIndex].last_time_ap_rejected_sta, '\0', sizeof(g_monitor_module.ap_params[VapIndex].last_time_ap_rejected_sta));
                }
#if (defined(_CBR_PRODUCT_REQ_) && !defined(_CBR2_PRODUCT_REQ_))
                }
                else
                {
                    wifi_dbg_print(1,"%s has failed with return status : %d\n",__FUNCTION__, res);
                    return TIMER_TASK_ERROR;
                }
#endif
            } else {
                wifi_dbg_print(1, "%s : vap %d is down\n", __FUNCTION__, VapIndex);
            }
        } else {
            wifi_dbg_print(1, "%s : Failed to get %d VAP Status from HAL \n", __FUNCTION__, VapIndex);
        }
    }
    return TIMER_TASK_COMPLETE;
}


int upload_ap_telemetry_data(void *arg)
{
    char buff[1024];
    char tmp[128];
    unsigned int i;
    BOOL radio_Enabled=FALSE;
#ifdef WIFI_HAL_VERSION_3
    for (i = 0; i < getNumberRadios(); i++)
#else
    for (i = 0; i < MAX_RADIOS; i++)
#endif
    {
        wifi_getRadioEnable(i, &radio_Enabled);
        if(radio_Enabled)
        {
            get_formatted_time(tmp);
            snprintf(buff, 1024, "%s WIFI_NOISE_FLOOR_%d:%d\n", tmp, i + 1, g_monitor_module.radio_data[i].NoiseFloor);

            write_to_file(wifi_health_log, buff);
            wifi_dbg_print(1, "%s", buff);
        }
    }
    return TIMER_TASK_COMPLETE;
}

BOOL client_fast_reconnect(unsigned int apIndex, char *mac)
{
    extern UINT assocCountThreshold;
    extern int assocMonitorDuration;
    extern int assocGateTime;
    sta_data_t  *sta;
    hash_map_t  *sta_map;
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);

    if(!assocMonitorDuration) {
             wifi_dbg_print(1, "%s: Client fast reconnection check disabled, assocMonitorDuration:%d \n", __func__, assocMonitorDuration);
             return FALSE;
    }

    wifi_dbg_print(1, "%s: Checking for client:%s connection on ap:%d\n", __func__, mac, apIndex);

    pthread_mutex_lock(&g_monitor_module.lock);
    sta_map = g_monitor_module.bssid_data[apIndex].sta_map;
    sta = (sta_data_t *)hash_map_get(sta_map, mac);
    if (sta == NULL) {
        wifi_dbg_print(1, "%s: Client:%s could not be found on sta map of ap:%d\n", __func__, mac, apIndex);
        pthread_mutex_unlock(&g_monitor_module.lock);
        return FALSE;
    }

    if(sta->gate_time && (tv_now.tv_sec < sta->gate_time)) {
             wifi_dbg_print(1, "%s: Blocking burst client connections for few more seconds\n", __func__);
             pthread_mutex_unlock(&g_monitor_module.lock);
             return TRUE;
    } else {
             wifi_dbg_print(1, "%s: processing further\n", __func__);
    }

    wifi_dbg_print(1, "%s: assocCountThreshold:%d assocMonitorDuration:%d assocGateTime:%d \n", __func__, assocCountThreshold, assocMonitorDuration, assocGateTime);

    if((tv_now.tv_sec - sta->assoc_monitor_start_time) < assocMonitorDuration)
    {
        sta->reconnect_count++;
        wifi_dbg_print(1, "%s: reconnect_count:%d \n", __func__, sta->reconnect_count);
        if(sta->reconnect_count > assocCountThreshold) {
             wifi_dbg_print(1, "%s: Blocking client connections for assocGateTime:%d \n", __func__, assocGateTime);
             t2_event_d("SYS_INFO_ClientConnBlock", 1);
             sta->reconnect_count = 0;
             sta->gate_time = tv_now.tv_sec + assocGateTime;
             pthread_mutex_unlock(&g_monitor_module.lock);
             return TRUE;
        }
    } else {
             sta->assoc_monitor_start_time = tv_now.tv_sec;
             sta->reconnect_count = 0;
             sta->gate_time = 0;
             wifi_dbg_print(1, "%s: resetting reconnect_count and assoc_monitor_start_time \n", __func__);
             pthread_mutex_unlock(&g_monitor_module.lock);
             return FALSE;
    }
    pthread_mutex_unlock(&g_monitor_module.lock);
    return FALSE;
}

BOOL client_fast_redeauth(unsigned int apIndex, char *mac)
{
    extern UINT deauthCountThreshold;
    extern int deauthMonitorDuration;
    extern int deauthGateTime;
    sta_data_t  *sta;
    hash_map_t  *sta_map;
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);

    if(!deauthMonitorDuration) {
             wifi_dbg_print(1, "%s: Client fast deauth check disabled, deauthMonitorDuration:%d \n", __func__, deauthMonitorDuration);
             return FALSE;
    }

    wifi_dbg_print(1, "%s: Checking for client:%s deauth on ap:%d\n", __func__, mac, apIndex);

    pthread_mutex_lock(&g_monitor_module.lock);
    sta_map = g_monitor_module.bssid_data[apIndex].sta_map;
    sta = (sta_data_t *)hash_map_get(sta_map, mac);

    if (sta == NULL  ) {
        wifi_dbg_print(1, "%s: Client:%s could not be found on sta map of ap:%d,  Blocking client deauth notification\n", __func__, mac, apIndex);
        pthread_mutex_unlock(&g_monitor_module.lock);
        return TRUE;
    }

    if(sta->deauth_gate_time && (tv_now.tv_sec < sta->deauth_gate_time)) {
             wifi_dbg_print(1, "%s: Blocking burst client deauth for few more seconds\n", __func__);
             pthread_mutex_unlock(&g_monitor_module.lock);
             return TRUE;
    } else {
             wifi_dbg_print(1, "%s: processing further\n", __func__);
    }

    wifi_dbg_print(1, "%s: deauthCountThreshold:%d deauthMonitorDuration:%d deauthGateTime:%d \n", __func__, deauthCountThreshold, deauthMonitorDuration, deauthGateTime);

    if((tv_now.tv_sec - sta->deauth_monitor_start_time) < deauthMonitorDuration)
    {
        sta->redeauth_count++;
        wifi_dbg_print(1, "%s: redeauth_count:%d \n", __func__, sta->redeauth_count);
        if(sta->redeauth_count > deauthCountThreshold) {
             wifi_dbg_print(1, "%s: Blocking client deauth for deauthGateTime:%d \n", __func__, deauthGateTime);
             sta->redeauth_count = 0;
             sta->deauth_gate_time = tv_now.tv_sec + deauthGateTime;
             pthread_mutex_unlock(&g_monitor_module.lock);
             return TRUE;
        }
    } else {
             sta->deauth_monitor_start_time = tv_now.tv_sec;
             sta->redeauth_count = 0;
             sta->deauth_gate_time = 0;
             wifi_dbg_print(1, "%s: resetting redeauth_count and deauth_monitor_start_time \n", __func__);
             pthread_mutex_unlock(&g_monitor_module.lock);
             return FALSE;
    }
    pthread_mutex_unlock(&g_monitor_module.lock);
    return FALSE;
}

#define MAX_BUFFER 4096
#define TELEMETRY_MAX_BUFFER 4096
int upload_client_telemetry_data(void *arg)
{
    hash_map_t     *sta_map;
    sta_key_t    sta_key;
    unsigned int num_devs;
    sta_data_t *sta;
    char buff[MAX_BUFFER];
    char telemetryBuff[TELEMETRY_MAX_BUFFER] = { '\0' };
    char tmp[128];
    int rssi, rapid_reconn_max;
    BOOL sendIndication = false;
    static char trflag[MAX_VAP] = {0};
    static char nrflag[MAX_VAP] = {0};
    static char stflag[MAX_VAP] = {0};
    static char snflag[MAX_VAP] = {0};
    static char rxretryflag[MAX_VAP] = {0};
  
    static unsigned int phase = 0;
    static unsigned int i = 0;
    errno_t           rc = -1;
    static BOOL isErrorsReceivedRFCenabled = FALSE;
    static unsigned long long rxretry_diff = 0;
    bool report_ss_info = false;

    CHAR eventName[32] = {0};

    if (phase == 0) {
        // IsCosaDmlWiFivAPStatsFeatureEnabled needs to be set to get telemetry of some stats, the TR object is 
        // Device.WiFi.X_RDKCENTRAL-COM_vAPStatsEnable

        get_device_flag(trflag, "dmsb.device.deviceinfo.X_RDKCENTRAL-COM_WHIX.TxRxRateList");
        get_device_flag(nrflag, "dmsb.device.deviceinfo.X_RDKCENTRAL-COM_WHIX.NormalizedRssiList");
        get_device_flag(stflag, "dmsb.device.deviceinfo.X_RDKCENTRAL-COM_WHIX.CliStatList");
        get_device_flag(rxretryflag, "dmsb.device.deviceinfo.X_RDKCENTRAL-COM_WHIX.RxRetryList");
	isErrorsReceivedRFCenabled = check_ErrorsReceivedRFC_enabled();
#if !defined (_XB6_PRODUCT_REQ_) && !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
        // see if list has changed
#ifdef WIFI_HAL_VERSION_3
        BOOL enableRadioDetailStats[MAX_NUM_RADIOS] = {FALSE};
#else
        bool enable24detailstats = false;
        bool enable5detailstats = false;
        unsigned int itr = 0;
#endif
        if (strncmp(stflag, g_monitor_module.cliStatsList, MAX_VAP) != 0) {
            strncpy(g_monitor_module.cliStatsList, stflag, MAX_VAP);
            // check if we should enable of disable detailed client stats collection on XB3

#ifdef WIFI_HAL_VERSION_3
            UINT radioIndex = 0; 
            for (itr = 0; itr < (UINT)getTotalNumberVAPs(); itr++) 
            {
                if (stflag[itr] == 1) 
                {
                    radioIndex = getRadioIndexFromAp(itr);
                    enableRadioDetailStats[radioIndex] = TRUE;
                }
            }
            for (radioIndex = 0; radioIndex < getNumberRadios(); ++radioIndex)
            {
                wifi_radio_operationParam_t* radioOperation = getRadioOperationParam(radioIndex);
                if (radioOperation == NULL)
                {
                    CcspTraceWarning(("%s : failed to getRadioOperationParam with radio index \n", __FUNCTION__));
                    phase = 0;
                    return TIMER_TASK_COMPLETE;
                }
                switch (radioOperation->band)
                {
                    case WIFI_FREQUENCY_2_4_BAND:
                        wifi_dbg_print(1, "%s:%d: client detailed stats collection for 2.4GHz radio set to %s\n", __func__, __LINE__, 
                                radioIndex, (enableRadioDetailStats[radioIndex] == TRUE)?"enabled":"disabled");
                    case WIFI_FREQUENCY_5_BAND:
                        wifi_dbg_print(1, "%s:%d: client detailed stats collection for 5GHz radio set to %s\n", __func__, __LINE__, 
                                radioIndex, (enableRadioDetailStats[radioIndex] == TRUE)?"enabled":"disabled");
                    case WIFI_FREQUENCY_6_BAND:
                        wifi_dbg_print(1, "%s:%d: client detailed stats collection for 6GHz radio set to %s\n", __func__, __LINE__, 
                                radioIndex, (enableRadioDetailStats[radioIndex] == TRUE)?"enabled":"disabled");
                    default:
                        break;
                }
            }

#else
            for (itr = 0; itr < MAX_VAP; itr++) {
                if ((itr%2) == 0) {

                    // 2.4GHz radio
                    if (stflag[itr] == 1) {
                        enable24detailstats = true;
                    }
                } else {
                    // 5GHz radio
                    if (stflag[itr] == 1) {
                        enable5detailstats = true;
                    }
                }
            }

            wifi_dbg_print(1, "%s:%d: client detailed stats collection for 2.4GHz radio set to %s\n", __func__, __LINE__, 
                    (enable24detailstats == true)?"enabled":"disabled");
            wifi_dbg_print(1, "%s:%d: client detailed stats collection for 5GHz radio set to %s\n", __func__, __LINE__, 
                    (enable5detailstats == true)?"enabled":"disabled");

#endif  //WIFI_HAL_VERSION_3
#if !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
#ifdef WIFI_HAL_VERSION_3
            for (radioIndex = 0; radioIndex < getNumberRadios(); radioIndex++)
            {
                wifi_setClientDetailedStatisticsEnable(radioIndex, enableRadioDetailStats[radioIndex]);
            }
#else
            wifi_setClientDetailedStatisticsEnable(0, enable24detailstats);
            wifi_setClientDetailedStatisticsEnable(1, enable5detailstats);
#endif //WIFI_HAL_VERSION_3
#endif //(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
        }
#endif //!defined (_XB6_PRODUCT_REQ_) && !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
        get_device_flag(snflag, "dmsb.device.deviceinfo.X_RDKCENTRAL-COM_WIFI_TELEMETRY.SNRList");
        memset(buff, 0, MAX_BUFFER);
        phase++;
        return TIMER_TASK_CONTINUE;
    }
    if (phase == 1) {
        sta_map = g_monitor_module.bssid_data[i].sta_map;
        memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
        int compare = i + 1 ;
            get_formatted_time(tmp);
       	    snprintf(buff, 2048, "%s WIFI_MAC_%d:", tmp, i + 1);

	    num_devs = 0;
       	    sta = hash_map_get_first(sta_map);
       	    while (sta != NULL) {
                if (sta->dev_stats.cli_Active == true) {
		    snprintf(tmp, 32, "%s,", to_sta_key(sta->sta_mac, sta_key));
		    strncat(buff, tmp, MAX_BUFFER - strlen(buff) - 1);
		    strncat(telemetryBuff, tmp, TELEMETRY_MAX_BUFFER - strlen(buff) - 1);
		    num_devs++;
		}
	        sta = hash_map_get_next(sta_map, sta);
       	    }
       	    strncat(buff, "\n", MAX_BUFFER - strlen(buff) - 1);
       	    // RDKB-28827 don't print this marker if there is no client connected
            if(0 != num_devs)
            {
                write_to_file(wifi_health_log, buff);
            }
            /*
            "header": "2GclientMac_split", "content": "WIFI_MAC_1:", "type": "wifihealth.txt",
            "header": "5GclientMac_split", "content": "WIFI_MAC_2:", "type": "wifihealth.txt",
            "header": "xh_mac_3_split",    "content": "WIFI_MAC_3:", "type": "wifihealth.txt",
            "header": "xh_mac_4_split",    "content": "WIFI_MAC_4:", "type": "wifihealth.txt",
       	     */
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                snprintf(eventName, sizeof(eventName), "%uGclientMac_split", convertRadioIndexToFrequencyNum(getRadioIndexFromAp(i)));
                t2_event_s(eventName, telemetryBuff);
            }
            else if (isVapXhs(i))
            {
                snprintf(eventName, sizeof(eventName), "xh_mac_%d_split", compare);
                t2_event_s(eventName, telemetryBuff);
            }
#else
       	    if (1 == compare) {
       	        t2_event_s("2GclientMac_split", telemetryBuff);
       	    } else if (2 == compare) {
       	        t2_event_s("5GclientMac_split", telemetryBuff);
       	    } else if (3 == compare) {
                t2_event_s("xh_mac_3_split", telemetryBuff);
            } else if (4 == compare) {
                t2_event_s("xh_mac_4_split", telemetryBuff);
            }
#endif
       	    wifi_dbg_print(1, "%s", buff);

	    get_formatted_time(tmp);
	    snprintf(buff, 2048, "%s WIFI_MAC_%d_TOTAL_COUNT:%d\n", tmp, i + 1, num_devs);
	    write_to_file(wifi_health_log, buff);
	    //    "header": "Total_2G_clients_split", "content": "WIFI_MAC_1_TOTAL_COUNT:", "type": "wifihealth.txt",
	    //    "header": "Total_5G_clients_split", "content": "WIFI_MAC_2_TOTAL_COUNT:","type": "wifihealth.txt",
	    //    "header": "xh_cnt_1_split","content": "WIFI_MAC_3_TOTAL_COUNT:","type": "wifihealth.txt",
            //    "header": "xh_cnt_2_split","content": "WIFI_MAC_4_TOTAL_COUNT:","type": "wifihealth.txt",
#ifdef WIFI_HAL_VERSION_3
        if (isVapPrivate(i))
        {
            if (0 == num_devs) {
                snprintf(eventName, sizeof(eventName), "WIFI_INFO_Zero_%uG_Clients", convertRadioIndexToFrequencyNum(getRadioIndexFromAp(i)));
                t2_event_d(eventName, 1);
            } else {
                snprintf(eventName, sizeof(eventName), "Total_%uG_clients_split", convertRadioIndexToFrequencyNum(getRadioIndexFromAp(i)));
                t2_event_d(eventName, num_devs);
            }
        }else if (isVapXhs(i))
        {
            snprintf(eventName, sizeof(eventName), "xh_cnt_%u_split", getRadioIndexFromAp(i)+1);
            t2_event_d(eventName, num_devs);

        }else if (isVapMesh(i))
        {
            snprintf(eventName, sizeof(eventName), "Total_%uG_PodClients_split", convertRadioIndexToFrequencyNum(getRadioIndexFromAp(i)));
            t2_event_d(eventName, num_devs);
        }
#else
            if (1 == compare) {
                if (0 == num_devs) {
                    t2_event_d("WIFI_INFO_Zero_2G_Clients", 1);
                } else {
                	t2_event_d("Total_2G_clients_split", num_devs);
                }

            } else if (2 == compare) {
                if(0 == num_devs) {
                    t2_event_d("WIFI_INFO_Zero_5G_Clients", 1);
                } else{
                	t2_event_d("Total_5G_clients_split", num_devs);
                }

            } else if (3 == compare) {
                    t2_event_d("xh_cnt_1_split", num_devs);
            } else if (4 == compare) {
                    t2_event_d("xh_cnt_2_split", num_devs);
            } else if (13 == compare) {
                    t2_event_d("Total_2G_PodClients_split", num_devs);
            } else if (14 == compare) {
                    t2_event_d("Total_5G_PodClients_split", num_devs);            
            }
#endif
            wifi_dbg_print(1, "%s", buff);

	get_formatted_time(tmp);
	memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
#if !defined(_XB7_PRODUCT_REQ_) && !defined(_PLATFORM_TURRIS_) && !defined(_HUB4_PRODUCT_REQ_)
        wifi_VAPTelemetry_t telemetry;
        char vap_status[16];
        memset(vap_status,0,16);
        wifi_getApStatus(i, vap_status);
        wifi_getVAPTelemetry(i, &telemetry);
        if(strncmp(vap_status,"Up",2)==0)
        {
            get_formatted_time(tmp);
            snprintf(buff, 2048, "%s WiFi_TX_Overflow_SSID_%d:%u\n", tmp, i + 1, telemetry.txOverflow);
            write_to_file(wifi_health_log, buff);
            wifi_dbg_print(1, "%s", buff);
        }
#endif
       // RDKB-28827 no need for markers of client details if there is no client connected
        if (0 == num_devs) {
            i++;
#ifdef WIFI_HAL_VERSION_3
            if (i >= getTotalNumberVAPs()) {
#else
            if (i >= MAX_VAP) {
#endif
                i = 0;
                phase++;
            }
            return TIMER_TASK_CONTINUE;
        }
        snprintf(buff, 2048, "%s WIFI_RSSI_%d:", tmp, i + 1);
       	sta = hash_map_get_first(sta_map);
       	while (sta != NULL) {
			if (sta->dev_stats.cli_Active == true) {
				snprintf(tmp, 32, "%d,", sta->dev_stats.cli_RSSI);
				strncat(buff, tmp, 128);
				strncat(telemetryBuff, tmp, 128);
			}

			sta = hash_map_get_next(sta_map, sta);
       	}
       	strncat(buff, "\n", 2);
       	write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                snprintf(eventName, sizeof(eventName), "%uGRSSI_split", convertRadioIndexToFrequencyNum(getRadioIndexFromAp(i)));
                t2_event_s(eventName, telemetryBuff);
            }
            else if (isVapXhs(i))
            {
                snprintf(eventName, sizeof(eventName), "xh_rssi_%u_split", compare);
                t2_event_s(eventName, telemetryBuff);
            }
#else
        if ( 1 == compare ) {
            t2_event_s("2GRSSI_split", telemetryBuff);
        } else if ( 2 == compare ) {
            t2_event_s("5GRSSI_split", telemetryBuff);
        } else if ( 3 == compare ) {
            t2_event_s("xh_rssi_3_split", telemetryBuff);
        } else if ( 4 == compare ) {
            t2_event_s("xh_rssi_4_split", telemetryBuff);
        }
#endif
       	wifi_dbg_print(1, "%s", buff);

		get_formatted_time(tmp);
	memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
       	snprintf(buff, 2048, "%s WIFI_CHANNEL_WIDTH_%d:", tmp, i + 1);
       	sta = hash_map_get_first(sta_map);
       	while (sta != NULL) {
			if (sta->dev_stats.cli_Active == true) {
				rc = sprintf_s(tmp, 64, "%s,", sta->dev_stats.cli_OperatingChannelBandwidth);
				if(rc < EOK) ERR_CHK(rc);
				strncat(buff, tmp, 128);
				strncat(telemetryBuff, tmp, 128);
			}

			sta = hash_map_get_next(sta_map, sta);
       	}
       	strncat(buff, "\n", 2);
       	write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
        if (isVapPrivate(i))
        {
            snprintf(eventName, sizeof(eventName), "WIFI_CW_%d_split", compare);
            t2_event_s(eventName, telemetryBuff);
        }
#else
        if ( 1 == compare ) {
            t2_event_s("WIFI_CW_1_split", telemetryBuff);
        } else if ( 2 == compare ) {
            t2_event_s("WIFI_CW_2_split", telemetryBuff);
        }
#endif
       	wifi_dbg_print(1, "%s", buff);

		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && nrflag[i]) {
			get_formatted_time(tmp);
       		snprintf(buff, 2048, "%s WIFI_NORMALIZED_RSSI_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%d,", sta->dev_stats.cli_SignalStrength);
					strncat(buff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
       		wifi_dbg_print(1, "%s", buff);
		}	
		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && snflag[i]) {
			get_formatted_time(tmp);
		memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
       		snprintf(buff, 2048, "%s WIFI_SNR_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%d,", sta->dev_stats.cli_SNR);
					strncat(buff, tmp, 128);
					strncat(telemetryBuff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                snprintf(eventName, sizeof(eventName), "WIFI_SNR_%d_split", compare);
                t2_event_s(eventName, telemetryBuff);
            }
#else
                if ( 1 == compare ) {
                    t2_event_s("WIFI_SNR_1_split", telemetryBuff);
                } else if ( 2 == compare ) {
                    t2_event_s("WIFI_SNR_2_split", telemetryBuff);
                }
#endif
       		wifi_dbg_print(1, "%s", buff);
		}  
        if ((isErrorsReceivedRFCenabled == TRUE) && rxretryflag[i]) {
	    get_formatted_time(tmp);
	    memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
	    snprintf(buff, 2048, "%s UP_RetransCount_%d:", tmp, i + 1);
	    sta = hash_map_get_first(sta_map);
	    while (sta != NULL) {
		if (sta->dev_stats.cli_Active == true) {
                    sta->prev_cli_rx_retries = ((sta->prev_cli_rx_retries <= sta->cli_rx_retries) ? sta->prev_cli_rx_retries : 0);
		    rxretry_diff = sta->cli_rx_retries - sta->prev_cli_rx_retries;
		    snprintf(tmp, 32, "%llu,", rxretry_diff);
		    strncat(buff, tmp, 128);
		    strncat(telemetryBuff, tmp, 128);
                    sta->prev_cli_rx_retries = sta->cli_rx_retries;
		}
	        sta = hash_map_get_next(sta_map, sta);
	    }
	    strncat(buff, "\n", 2);
	    write_to_file(wifi_health_log, buff);
	    snprintf(eventName, sizeof(eventName), "WIFIUpRetransCnt_%d_split", compare);
	    t2_event_s(eventName, telemetryBuff);
	    wifi_dbg_print(1, "%s", buff);
        }
          
		get_formatted_time(tmp);
	memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
       	snprintf(buff, 2048, "%s WIFI_TXCLIENTS_%d:", tmp, i + 1);
       	sta = hash_map_get_first(sta_map);
       	while (sta != NULL) {
			if (sta->dev_stats.cli_Active == true) {
				snprintf(tmp, 32, "%d,", sta->dev_stats.cli_LastDataDownlinkRate);
				strncat(buff, tmp, 128);
				strncat(telemetryBuff, tmp, 128);
			}
            
			sta = hash_map_get_next(sta_map, sta);
       	}
       	strncat(buff, "\n", 2);
       	write_to_file(wifi_health_log, buff);

#ifdef WIFI_HAL_VERSION_3
        if (isVapPrivate(i))
        {
            snprintf(eventName, sizeof(eventName), "WIFI_TX_%d_split", compare);
            t2_event_s(eventName, telemetryBuff);
        }
#else
        if ( 1 == compare ) {
            t2_event_s("WIFI_TX_1_split", telemetryBuff);
        } else if ( 2 == compare ) {
            t2_event_s("WIFI_TX_2_split", telemetryBuff);
        }
#endif
       	wifi_dbg_print(1, "%s", buff);

		get_formatted_time(tmp);
	memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
       	snprintf(buff, 2048, "%s WIFI_RXCLIENTS_%d:", tmp, i + 1);
       	sta = hash_map_get_first(sta_map);
       	while (sta != NULL) {
			if (sta->dev_stats.cli_Active == true) {
				snprintf(tmp, 32, "%d,", sta->dev_stats.cli_LastDataUplinkRate);
				strncat(buff, tmp, 128);
				strncat(telemetryBuff, tmp, 128);
			}
            
			sta = hash_map_get_next(sta_map, sta);
       	}
       	strncat(buff, "\n", 2);
       	write_to_file(wifi_health_log, buff);
       	//  "header": "WIFI_RX_1_split", "content": "WIFI_RXCLIENTS_1:", "type": "wifihealth.txt",
#ifdef WIFI_HAL_VERSION_3
        if (isVapPrivate(i))
        {
            snprintf(eventName, sizeof(eventName), "WIFI_RX_%d_split", compare);
            t2_event_s(eventName, telemetryBuff);
        }
#else
       	if ( 1 == compare ) {
       	    t2_event_s("WIFI_RX_1_split", telemetryBuff);
       	} else if ( 2 == compare ) {
       	    t2_event_s("WIFI_RX_2_split", telemetryBuff);
       	}
#endif
       	wifi_dbg_print(1, "%s", buff);
	
		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && trflag[i]) {
			get_formatted_time(tmp);
		memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
       		snprintf(buff, 2048, "%s WIFI_MAX_TXCLIENTS_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%u,", sta->dev_stats.cli_MaxDownlinkRate);
					strncat(buff, tmp, 128);
					strncat(telemetryBuff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                snprintf(eventName, sizeof(eventName), "MAXTX_%d_split", compare);
                t2_event_s(eventName, telemetryBuff);
            }
#else
                if ( 1 == compare ) {
                    t2_event_s("MAXTX_1_split", telemetryBuff);
                } else if ( 2 == compare ) {
                    t2_event_s("MAXTX_2_split", telemetryBuff);
                }
#endif
       		wifi_dbg_print(1, "%s", buff);
		}
		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && trflag[i]) {
			get_formatted_time(tmp);
		memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
       		snprintf(buff, 2048, "%s WIFI_MAX_RXCLIENTS_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%u,", sta->dev_stats.cli_MaxUplinkRate);
					strncat(buff, tmp, 128);
					strncat(telemetryBuff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                snprintf(eventName, sizeof(eventName), "MAXRX_%d_split", compare);
                t2_event_s(eventName, telemetryBuff);
            }
#else
                if ( 1 == compare ) {
                    t2_event_s("MAXRX_1_split", telemetryBuff);
                } else if ( 2 == compare ) {
                    t2_event_s("MAXRX_2_split", telemetryBuff);
                }
#endif
       		wifi_dbg_print(1, "%s", buff);
		}

                if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && trflag[i])
                {
                    get_formatted_time(tmp);
                    snprintf(buff, 2048, "%s WIFI_RXTXCLIENTDELTA_%d:", tmp, i + 1);
                    sta = hash_map_get_first(sta_map);
                    while (sta != NULL)
                    {
                        if (sta->dev_stats.cli_Active == true)
                        {
                            snprintf(tmp, 32, "%u,", (sta->dev_stats.cli_LastDataDownlinkRate - sta->dev_stats.cli_LastDataUplinkRate));
                            strncat(buff, tmp, 128);
                        }
                        sta = hash_map_get_next(sta_map, sta);
                    }
                    strncat(buff, "\n", 2);
                    write_to_file(wifi_health_log, buff);
                    wifi_dbg_print(1, "%s", buff);
                }
		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && stflag[i]) {
			get_formatted_time(tmp);
       		snprintf(buff, 2048, "%s WIFI_BYTESSENTCLIENTS_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%lu,", sta->dev_stats.cli_BytesSent - sta->dev_stats_last.cli_BytesSent);
                                        sta->dev_stats_last.cli_BytesSent = sta->dev_stats.cli_BytesSent;
					strncat(buff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
       		wifi_dbg_print(1, "%s", buff);
		}

		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && stflag[i]) {
			get_formatted_time(tmp);
       		snprintf(buff, 2048, "%s WIFI_BYTESRECEIVEDCLIENTS_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%lu,", sta->dev_stats.cli_BytesReceived - sta->dev_stats_last.cli_BytesReceived);
                                        sta->dev_stats_last.cli_BytesReceived = sta->dev_stats.cli_BytesReceived;
					strncat(buff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
       		wifi_dbg_print(1, "%s", buff);
		}

		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && stflag[i]) {
			get_formatted_time(tmp);
		memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
       		snprintf(buff, 2048, "%s WIFI_PACKETSSENTCLIENTS_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%lu,", sta->dev_stats.cli_PacketsSent - sta->dev_stats_last.cli_PacketsSent);
                                        sta->dev_stats_last.cli_PacketsSent = sta->dev_stats.cli_PacketsSent;
					strncat(buff, tmp, 128);
					strncat(telemetryBuff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                rc = sprintf_s(eventName, sizeof(eventName), "WIFI_PACKETSSENTCLIENTS_%d_split", compare);
                if(rc < EOK) ERR_CHK(rc);
                t2_event_s(eventName, telemetryBuff);
            }
#else
                if ( 1 == compare ) {
                    t2_event_s("WIFI_PACKETSSENTCLIENTS_1_split", telemetryBuff);
                } else if ( 2 == compare ) {
                    t2_event_s("WIFI_PACKETSSENTCLIENTS_2_split", telemetryBuff);
                }
#endif
       		wifi_dbg_print(1, "%s", buff);
		}
		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && stflag[i]) {
			get_formatted_time(tmp);
       		snprintf(buff, 2048, "%s WIFI_PACKETSRECEIVEDCLIENTS_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%lu,", sta->dev_stats.cli_PacketsReceived - sta->dev_stats_last.cli_PacketsReceived);
                                        sta->dev_stats_last.cli_PacketsReceived = sta->dev_stats.cli_PacketsReceived;
					strncat(buff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
       		wifi_dbg_print(1, "%s", buff);
		}

		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && stflag[i]) {
			get_formatted_time(tmp);
		memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
       		snprintf(buff, 2048, "%s WIFI_ERRORSSENT_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%lu,", sta->dev_stats.cli_ErrorsSent - sta->dev_stats_last.cli_ErrorsSent);
                                        sta->dev_stats_last.cli_ErrorsSent = sta->dev_stats.cli_ErrorsSent;       
					strncat(buff, tmp, 128);
					strncat(telemetryBuff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                snprintf(eventName, sizeof(eventName), "WIFI_ERRORSSENT_%d_split", compare);
                t2_event_s(eventName, telemetryBuff);
            }
#else
                if ( 1 == compare ) {
                    t2_event_s("WIFI_ERRORSSENT_1_split", telemetryBuff);
                } else if ( 2 == compare ) {
                    t2_event_s("WIFI_ERRORSSENT_2_split", telemetryBuff);
                }
#endif
       		wifi_dbg_print(1, "%s", buff);
		}

		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && stflag[i]) {
			get_formatted_time(tmp);
                memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
       		snprintf(buff, 2048, "%s WIFI_RETRANSCOUNT_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%lu,", sta->dev_stats.cli_RetransCount - sta->dev_stats_last.cli_RetransCount);
                                        sta->dev_stats_last.cli_RetransCount = sta->dev_stats.cli_RetransCount;
					strncat(buff, tmp, 128);
                                        strncat(telemetryBuff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                snprintf(eventName, sizeof(eventName), "WIFIRetransCount%d_split", compare);
                t2_event_s(eventName, telemetryBuff);
            }
#else
                if ( 1 == compare ) {
                    t2_event_s("WIFIRetransCount1_split", telemetryBuff);
                } else if ( 2 == compare ) {
                    t2_event_s("WIFIRetransCount2_split", telemetryBuff);
                }
#endif
       		wifi_dbg_print(1, "%s", buff);
		}
		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && stflag[i]) {
			get_formatted_time(tmp);
       		snprintf(buff, 2048, "%s WIFI_FAILEDRETRANSCOUNT_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%lu,", sta->dev_stats.cli_FailedRetransCount - sta->dev_stats_last.cli_FailedRetransCount);
                                        sta->dev_stats_last.cli_FailedRetransCount = sta->dev_stats.cli_FailedRetransCount;
					strncat(buff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
       		wifi_dbg_print(1, "%s", buff);
		}

		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && stflag[i]) {
			get_formatted_time(tmp);
       		snprintf(buff, 2048, "%s WIFI_RETRYCOUNT_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%lu,", sta->dev_stats.cli_RetryCount - sta->dev_stats_last.cli_RetryCount);
                                        sta->dev_stats_last.cli_RetryCount = sta->dev_stats.cli_RetryCount;
					strncat(buff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
       		wifi_dbg_print(1, "%s", buff);
		}
		if ((sWiFiDmlvApStatsFeatureEnableCfg == true) && stflag[i]) {
			get_formatted_time(tmp);
       		snprintf(buff, 2048, "%s WIFI_MULTIPLERETRYCOUNT_%d:", tmp, i + 1);
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {
				if (sta->dev_stats.cli_Active == true) {
					snprintf(tmp, 32, "%lu,", sta->dev_stats.cli_MultipleRetryCount - sta->dev_stats_last.cli_MultipleRetryCount);
                                        sta->dev_stats_last.cli_MultipleRetryCount = sta->dev_stats.cli_MultipleRetryCount;
					strncat(buff, tmp, 128);
				}
            
				sta = hash_map_get_next(sta_map, sta);
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
       		wifi_dbg_print(1, "%s", buff);
		}

       	// Every hour, for private SSID(s) we need to calculate the good rssi time and bad rssi time 
		// and write into wifi log in following format
       	// WIFI_GOODBADRSSI_$apindex: $MAC,$GoodRssiTime,$BadRssiTime; $MAC,$GoodRssiTime,$BadRssiTime; ....
#ifdef  WIFI_HAL_VERSION_3
                if (i < (UINT)getTotalNumberVAPs()) {
#else
                if (i < 2) {
#endif
			get_formatted_time(tmp);
       		memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
                snprintf(buff, 2048, "%s WIFI_GOODBADRSSI_%d:", tmp, i + 1);
        
       		sta = hash_map_get_first(sta_map);
       		while (sta != NULL) {

			sta->total_connected_time += sta->connected_time;
			sta->connected_time = 0;

			sta->total_disconnected_time += sta->disconnected_time;
			sta->disconnected_time = 0;

			if (sta->dev_stats.cli_Active == true) {
				snprintf(tmp, 128, "%s,%d,%d;", to_sta_key(sta->sta_mac, sta_key), (sta->good_rssi_time)/60, (sta->bad_rssi_time)/60);
				strncat(buff, tmp, 128);
                                strncat(telemetryBuff, tmp, 128);
			}

			sta->good_rssi_time = 0;
			sta->bad_rssi_time = 0;

			sta = hash_map_get_next(sta_map, sta);
            
       		}
       		strncat(buff, "\n", 2);
       		write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                snprintf(eventName, sizeof(eventName), "GB_RSSI_%d_split", compare);
                t2_event_s(eventName, telemetryBuff);
            }
#else
                if ( 1 == compare ) {
                    t2_event_s("GB_RSSI_1_split", telemetryBuff);
                } else if ( 2 == compare ) {
                    t2_event_s("GB_RSSI_2_split", telemetryBuff);
                }
#endif
       		wifi_dbg_print(1, "%s", buff);		
		}
		// check if failure indication is enabled in TR swicth
    	CosaDmlWiFi_GetRapidReconnectIndicationEnable(&sendIndication, false);

		if (sendIndication == true) {                
			BOOLEAN bReconnectCountEnable = 0;

			// check whether Reconnect Count is enabled or not fro individual vAP
			CosaDmlWiFi_GetRapidReconnectCountEnable(i , &bReconnectCountEnable, false);
			if (bReconnectCountEnable == true)
			{
				get_formatted_time(tmp);
				memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
				snprintf(buff, 2048, "%s WIFI_RECONNECT_%d:", tmp, i + 1);
				sta = hash_map_get_first(sta_map);
				while (sta != NULL) {
				
					snprintf(tmp, 128, "%s,%d;", to_sta_key(sta->sta_mac, sta_key), sta->rapid_reconnects);
					strncat(buff, tmp, 128);
					strncat(telemetryBuff, tmp, 128);
				
					sta->rapid_reconnects = 0;
				
					sta = hash_map_get_next(sta_map, sta);
				
				}
				strncat(buff, "\n", 2);
				write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
            if (isVapPrivate(i))
            {
                snprintf(eventName, sizeof(eventName), "WIFI_REC_%d_split", compare);
                t2_event_s(eventName, telemetryBuff);
            }
#else
                                if ( 1 == compare ) {
                                    t2_event_s("WIFI_REC_1_split", telemetryBuff);
                                } else if ( 2 == compare ) {
                                    t2_event_s("WIFI_REC_2_split", telemetryBuff);
                                }
#endif
				wifi_dbg_print(1, "%s", buff);
			}
		}
#ifdef WIFI_HAL_VERSION_3
        if (isVapPrivate(i)) {
            report_ss_info = true;
#else
            if ( 1 == compare || 2 == compare ) {
                report_ss_info = true;
#endif
            } else {
                report_ss_info = false;
            }

            /* Report client's Spatial Stream info
             * connected to Private SSID
             */
            if (report_ss_info) {
                get_formatted_time(tmp);
                memset(telemetryBuff, 0, TELEMETRY_MAX_BUFFER);
                snprintf(buff, 2048, "%s WIFI_CAPSS_%d:", tmp, i + 1);
                sta = hash_map_get_first(sta_map);
                while (sta != NULL) {
                    if (sta->dev_stats.cli_Active == true) {
                        snprintf(tmp, 32, "%d/%d,", sta->cli_CapableNumSpatialStreams,
                                        sta->dev_stats.cli_activeNumSpatialStreams);
                        strncat(buff, tmp, 128);
                        strncat(telemetryBuff, tmp, 128);
                    }

                    sta = hash_map_get_next(sta_map, sta);
                }
                strncat(buff, "\n", 2);
                write_to_file(wifi_health_log, buff);
#ifdef WIFI_HAL_VERSION_3
                if (isVapPrivate(i)) {
                    snprintf(eventName, sizeof(eventName), "WIFI_CAPSS_%d_split", compare);
                    t2_event_s(eventName, telemetryBuff);
                }
#else
                if ( 1 == compare ) {
                    t2_event_s("WIFI_CAPSS_1_split", telemetryBuff);
                } else if ( 2 == compare ) {
                    t2_event_s("WIFI_CAPSS_2_split", telemetryBuff);
                }
#endif
            } /*report_ss_info*/

        i++;
#ifdef WIFI_HAL_VERSION_3
        if(i >= getTotalNumberVAPs()) {
#else
        if(i >= MAX_VAP) {
#endif
            i = 0;
            phase++;
        }
        return TIMER_TASK_CONTINUE;
    }
    
	// update thresholds if changed
	if (CosaDmlWiFi_GetGoodRssiThresholdValue(&rssi) == ANSC_STATUS_SUCCESS) {
		g_monitor_module.sta_health_rssi_threshold = rssi;
	}

#ifdef WIFI_HAL_VERSION_3
        for (i = 0; i < getTotalNumberVAPs(); i++) {
#else
        for (i = 0; i < MAX_VAP; i++) {
#endif		// update rapid reconnect time limit if changed
            if (CosaDmlWiFi_GetRapidReconnectThresholdValue(i, &rapid_reconn_max) == ANSC_STATUS_SUCCESS) {
                g_monitor_module.bssid_data[i].ap_params.rapid_reconnect_threshold = rapid_reconn_max;
            }  
	}    
    logVAPUpStatus();
    i = 0;
    phase = 0;
    return TIMER_TASK_COMPLETE;
}

#if defined (FEATURE_CSI)
static char*
macbytes_to_string(mac_address_t mac, unsigned char* string)
{
    sprintf((char *)string, "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0] & 0xff,
            mac[1] & 0xff,
            mac[2] & 0xff,
            mac[3] & 0xff,
            mac[4] & 0xff,
            mac[5] & 0xff);
    return (char *)string;
}
#endif

#if 0
/* cleanup_client_stats_info */
void cleanup_client_stats_info(void)
{
    hash_map_t *sta_map;
    sta_data_t *sta = NULL;
    sta_key_t sta_key;
    int ap_index;

    for (ap_index = 0; ap_index < MAX_VAP; ap_index++)
    {
        sta_map = g_monitor_module.sta_map[ap_index];

        // now check all sta(s) and cleanup disconnected clients.
        sta = hash_map_get_first(sta_map);
        while (sta != NULL)
        {
            /*
             * If the device is disconnected,
             * the cli_disconnect_poll_count is set to 1
             * in process_disconnect().
             */
            if (sta->cli_disconnect_poll_count > 0)
            {
                sta->cli_disconnect_poll_count += 1;
                /* keep the stas for 3 poll count after client is disconnected */
                if (sta->cli_disconnect_poll_count > 4)
                {
                    sta = (sta_data_t *)hash_map_remove(sta_map,
                                to_sta_key(sta->sta_mac, sta_key));
                    if (sta == NULL)
                    {
                        wifi_dbg_print(1, "%s:%d: Device: NOT FOUND in hash map\n",
                                        __func__, __LINE__);
                        assert(0);
                    }
                }
            }
            sta = hash_map_get_next(sta_map, sta);
        }
    }
}
#endif

static void
reset_client_stats_info(unsigned int apIndex)
{
	sta_data_t      *sta = NULL;
	hash_map_t      *sta_map;

	sta_map = g_monitor_module.bssid_data[apIndex].sta_map;

	sta = hash_map_get_first(sta_map);
	while (sta != NULL) {
		memset((unsigned char *)&sta->dev_stats_last, 0, sizeof(wifi_associated_dev3_t));
		memset((unsigned char *)&sta->dev_stats, 0,  sizeof(wifi_associated_dev3_t));
		sta->cli_rx_retries = 0;
		sta->prev_cli_rx_retries = 0;
		sta = hash_map_get_next(sta_map, sta);
	}

}

void
get_device_flag(char flag[], char *psmcli)
{
    int retPsmGet = CCSP_SUCCESS;
    char *strValue = NULL;
    char buf[CLIENT_STATS_MAX_LEN_BUF] = {0};
    char tempBuf[8] = {0}, tempPsmBuf[64] = {0};
    bool isPsmsetneeded =  false;
    retPsmGet = PSM_Get_Record_Value2(bus_handle, g_Subsystem,
            psmcli, NULL, &strValue);
    errno_t rc = -1;

    if (retPsmGet == CCSP_SUCCESS)
    {
        memset(flag, 0, MAX_VAP);
        if (strlen(strValue))
        {
            strncpy(buf, strValue, CLIENT_STATS_MAX_LEN_BUF-1);
            buf[strlen(strValue)] = '\0';
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
            int buf_int[MAX_VAP] = {0}, i = 0, j = 0;

            for (i = 0; buf[i] != '\0'; i++)
            {
                if (buf[i] == ',')
                {
                    j++;
                }
                else if (buf[i] == '"')
                {
                    isPsmsetneeded = true;
                    continue;
                }
                else
                {
                    buf_int[j] = buf_int[j] * 10 + (buf[i] - 48);
                }
            }
            int len = sizeof(buf_int)/sizeof(buf_int[0]);
            for(i = 0;  i < len; i ++)
            {
#ifdef WIFI_HAL_VERSION_3
                if((buf_int[i] <= (int)getTotalNumberVAPs()) && (buf_int[i] > 0))
#else
                if((buf_int[i] <= MAX_VAP) && (buf_int[i] > 0))
#endif
                {
                    flag[(buf_int[i] - 1)] = 1;
                    if(isPsmsetneeded)
                    {
                        memset(tempBuf, 0, sizeof(tempBuf));
                        rc = sprintf_s(tempBuf, sizeof(tempBuf), "%d,",  buf_int[i]);
                        if(rc < EOK) ERR_CHK(rc);
                        strncat(tempPsmBuf, tempBuf, strlen(tempBuf));
                    }
                }
            }
            if(isPsmsetneeded)
            {
                tempPsmBuf[strlen(tempPsmBuf) - 1] = '\0';
                PSM_Set_Record_Value2(bus_handle,g_Subsystem, psmcli, ccsp_string, tempPsmBuf);
            }
        }
    }
    else
    {
        flag[0] = 1;
        flag[1] = 1;
    }
}

static void upload_client_debug_stats_chan_stats(INT apIndex) 
{

    char tmp[128] = {0};
    ULONG channel = 0;
#ifdef WIFI_HAL_VERSION_3
    CHAR eventName[32] = {0};
#endif

#ifdef WIFI_HAL_VERSION_3
    wifi_getRadioChannel(getRadioIndexFromAp(apIndex), &channel);
    memset(tmp, 0, sizeof(tmp));
    get_formatted_time(tmp);
    write_to_file(wifi_health_log, "\n%s WIFI_CHANNEL_%d:%lu\n", tmp, apIndex+1, channel);
    if (isVapPrivate(apIndex))
    {
        snprintf(eventName, sizeof(eventName), "WIFI_CH_%d_split", apIndex + 1 );
        t2_event_d(eventName, channel);
        if (getRadioIndexFromAp(apIndex) == 1)
        {
            if( 1 == channel )
            {
                //         "header": "WIFI_INFO_UNI3_channel", "content": "WIFI_CHANNEL_2:1", "type": "wifihealth.txt",
                t2_event_d("WIFI_INFO_UNI3_channel", 1);
            } else if (( 3 == channel || 4 == channel)) \
            {
                t2_event_d("WIFI_INFO_UNII_channel", 1);
            }
        }
    }
#else
    wifi_getRadioChannel(apIndex%2, &channel);
    memset(tmp, 0, sizeof(tmp));
    get_formatted_time(tmp);
    write_to_file(wifi_health_log, "\n%s WIFI_CHANNEL_%d:%lu\n", tmp, apIndex+1, channel);

    if ( 1 == apIndex+1 ) {
        // Eventing for telemetry profile = "header": "WIFI_CH_1_split", "content": "WIFI_CHANNEL_1:", "type": "wifihealth.txt",
        t2_event_d("WIFI_CH_1_split", channel);
    } else if ( 2 == apIndex+1 ) {
        // Eventing for telemetry profile = "header": "WIFI_CH_2_split", "content": "WIFI_CHANNEL_2:", "type": "wifihealth.txt",
        t2_event_d("WIFI_CH_2_split", channel);
        if( 1 == channel ) {
            //         "header": "WIFI_INFO_UNI3_channel", "content": "WIFI_CHANNEL_2:1", "type": "wifihealth.txt",
            t2_event_d("WIFI_INFO_UNI3_channel", 1);
        } else if (( 3 == channel || 4 == channel)) {
            t2_event_d("WIFI_INFO_UNII_channel", 1);
        }
    }
#endif
}

static void  upload_client_debug_stats_transmit_power_stats(INT apIndex)
{
    char tmp[128] = {0};
    char buf[CLIENT_STATS_MAX_LEN_BUF] = {0};
    ULONG txpower = 0;
    ULONG txpwr_pcntg = 0;
#ifdef WIFI_HAL_VERSION_3
    CHAR eventName[32] = {0};
#endif


#ifdef WIFI_HAL_VERSION_3
    if (isVapPrivate(apIndex))
#else
    if ((apIndex == 0) || (apIndex == 1))
#endif
    {
        txpower = 0;
        memset (buf, 0, CLIENT_STATS_MAX_LEN_BUF);

        /* adding transmit power and countrycode */
#ifdef WIFI_HAL_VERSION_3
        wifi_getRadioCountryCode(getRadioIndexFromAp(apIndex), buf);
#else //WIFI_HAL_VERSION_3
        wifi_getRadioCountryCode(apIndex, buf);
#endif //WIFI_HAL_VERSION_3
        memset(tmp, 0, sizeof(tmp));
        get_formatted_time(tmp);
        write_to_file(wifi_health_log, "\n%s WIFI_COUNTRY_CODE_%d:%s", tmp, apIndex+1, buf);
#ifdef WIFI_HAL_VERSION_3
        wifi_getRadioTransmitPower(getRadioIndexFromAp(apIndex), &txpower);
#else
        wifi_getRadioTransmitPower(apIndex, &txpower);//Absolute power API for XB6 to be added
#endif
        memset(tmp, 0, sizeof(tmp));
        get_formatted_time(tmp);
        write_to_file(wifi_health_log, "\n%s WIFI_TX_PWR_dBm_%d:%lu", tmp, apIndex+1, txpower);
        //    "header": "WIFI_TXPWR_1_split",   "content": "WIFI_TX_PWR_dBm_1:", "type": "wifihealth.txt",
        //    "header": "WIFI_TXPWR_2_split",   "content": "WIFI_TX_PWR_dBm_2:", "type": "wifihealth.txt",
#ifdef WIFI_HAL_VERSION_3
        snprintf(eventName, sizeof(eventName), "WIFI_TXPWR_%d_split", apIndex + 1 );
        t2_event_d(eventName, txpower);
#else
        if(1 == (apIndex+1)) {
            t2_event_d("WIFI_TXPWR_1_split", txpower);
        } else if (2 == (apIndex+1)) {
            t2_event_d("WIFI_TXPWR_2_split", txpower);
        }
#endif
                //XF3-5424
#if defined(DUAL_CORE_XB3) || (defined(_XB6_PRODUCT_REQ_) && defined(_XB7_PRODUCT_REQ_) && !defined(_COSA_BCM_ARM_)) || defined(_HUB4_PRODUCT_REQ_) 
                static char *TransmitPower = "eRT.com.cisco.spvtg.ccsp.tr181pa.Device.WiFi.Radio.%d.TransmitPower";
                char recName[256];
                int retPsmGet = CCSP_SUCCESS;
                char *strValue = NULL;
                errno_t rc = -1;
                rc = sprintf_s(recName, sizeof(recName), TransmitPower, apIndex+1);
                if(rc < EOK)
                {
                    ERR_CHK(rc);
                }
                retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, recName, NULL, &strValue);
                if (retPsmGet == CCSP_SUCCESS) {
                    int transPower = atoi(strValue);
                    txpwr_pcntg = (ULONG)transPower;
                    ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
                }
#else
#ifdef WIFI_HAL_VERSION_3
        wifi_getRadioPercentageTransmitPower(getRadioIndexFromAp(apIndex), &txpwr_pcntg);
#else //WIFI_HAL_VERSION_3
        wifi_getRadioPercentageTransmitPower(apIndex, &txpwr_pcntg);//percentage API for XB3 to be added.
#endif //WIFI_HAL_VERSION_3
#endif
        memset(tmp, 0, sizeof(tmp));
        get_formatted_time(tmp);
        write_to_file(wifi_health_log, "\n%s WIFI_TX_PWR_PERCENTAGE_%d:%lu", tmp, apIndex+1, txpwr_pcntg);
#ifdef WIFI_HAL_VERSION_3
        errno_t safec_rc = -1;
        safec_rc = sprintf_s(eventName, sizeof(eventName), "WIFI_TXPWR_PCNTG_%u_split", apIndex + 1 );
        if(safec_rc < EOK) ERR_CHK(safec_rc);
        t2_event_d("WIFI_TXPWR_PCNTG_1_split", txpwr_pcntg);
#else
        if(1 == (apIndex+1)) {
            t2_event_d("WIFI_TXPWR_PCNTG_1_split", txpwr_pcntg);
        } else if (2 == (apIndex+1)) {
            t2_event_d("WIFI_TXPWR_PCNTG_2_split", txpwr_pcntg);
        }
#endif
    }
}

static void upload_client_debug_stats_acs_stats(INT apIndex) 
{
    BOOL enable = false;
    char tmp[128] = {0};
#ifdef WIFI_HAL_VERSION_3
    CHAR eventName[32] = {0};
#endif

#ifdef WIFI_HAL_VERSION_3
    if (isVapPrivate(apIndex))
#else
    if ((apIndex == 0) || (apIndex == 1))
#endif
    {

        wifi_getBandSteeringEnable(&enable);
        memset(tmp, 0, sizeof(tmp));
        get_formatted_time(tmp);
        write_to_file(wifi_health_log, "\n%s WIFI_ACL_%d:%d", tmp, apIndex+1, enable);

#ifdef WIFI_HAL_VERSION_3
        wifi_getRadioAutoChannelEnable(getRadioIndexFromAp(apIndex), &enable);
#else
        wifi_getRadioAutoChannelEnable(apIndex, &enable);
#endif
        if (true == enable)
        {
            memset(tmp, 0, sizeof(tmp));
            get_formatted_time(tmp);
            write_to_file(wifi_health_log, "\n%s WIFI_ACS_%d:true\n", tmp, apIndex+1);
            // "header": "WIFI_ACS_1_split",  "content": "WIFI_ACS_1:", "type": "wifihealth.txt",
            // "header": "WIFI_ACS_2_split", "content": "WIFI_ACS_2:", "type": "wifihealth.txt",
#ifdef WIFI_HAL_VERSION_3
            snprintf(eventName, sizeof(eventName), "WIFI_ACS_%d_split", apIndex + 1 );
            t2_event_s(eventName, "true");
#else
            if ( 1 == (apIndex+1) ) {
                t2_event_s("WIFI_ACS_1_split", "true");
            } else if ( 2 == (apIndex+1) ) {
                t2_event_s("WIFI_ACS_2_split", "true");
            }
#endif
        }
        else
        {
            memset(tmp, 0, sizeof(tmp));
            get_formatted_time(tmp);
            write_to_file(wifi_health_log, "\n%s WIFI_ACS_%d:false\n", tmp, apIndex+1);
            // "header": "WIFI_ACS_1_split",  "content": "WIFI_ACS_1:", "type": "wifihealth.txt",
            // "header": "WIFI_ACS_2_split", "content": "WIFI_ACS_2:", "type": "wifihealth.txt",
#ifdef WIFI_HAL_VERSION_3
            snprintf(eventName, sizeof(eventName), "WIFI_ACS_%d_split", apIndex + 1 );
            t2_event_s(eventName,  "false");
#else
            if ( 1 == (apIndex+1) ) {
                t2_event_s("WIFI_ACS_1_split", "false");
            } else if ( 2 == (apIndex+1) ) {
                t2_event_s("WIFI_ACS_2_split", "false");
            }
#endif
        }
    }
}

static void upload_client_debug_stats_sta_fa_info(INT apIndex, sta_data_t *sta) 
{

    INT len = 0;
    char *value = NULL;
    char *saveptr = NULL;
    char *ptr = NULL;
    FILE *fp  = NULL;
    char tmp[128] = {0};
    sta_key_t sta_key;
    char buf[CLIENT_STATS_MAX_LEN_BUF] = {0};
    
    memset (buf, 0, CLIENT_STATS_MAX_LEN_BUF);
    if (sta != NULL) {
        fp = (FILE *)(long int)v_secure_popen("r", "dmesg | grep FA_INFO_%s | tail -1", to_sta_key(sta->sta_mac, sta_key));
        if (fp) {
            fgets(buf, CLIENT_STATS_MAX_LEN_BUF, fp);
            v_secure_pclose(fp);

            len = strlen(buf);
            if (len) {
                ptr = buf + len;
                while (len-- && ptr-- && *ptr != ':');
                ptr++;

                value = strtok_r(ptr, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_AID_%d:%s", tmp, apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_TIM_%d:%s", tmp, apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_BMP_SET_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_BMP_CLR_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_TX_PKTS_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_TX_DISCARDS_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "\n%s WIFI_UAPSD_%d:%s", tmp,
                        apIndex+1, value);
            }
        }
        else {
            wifi_dbg_print(1, "Failed to run popen command\n");
        }
    }
    else {
        wifi_dbg_print(1, "NULL sta\n");
    }
}

static void upload_client_debug_stats_sta_fa_lmac_data_stats(INT apIndex, sta_data_t *sta)
{
    INT len = 0;
    char *value = NULL;
    char *saveptr = NULL;
    char *ptr = NULL;
    FILE *fp  = NULL;
    char tmp[128] = {0};
    sta_key_t sta_key;
    char buf[CLIENT_STATS_MAX_LEN_BUF] = {0};

    memset (buf, 0, CLIENT_STATS_MAX_LEN_BUF);

    if (sta != NULL) {
        fp = (FILE *)(long int)v_secure_popen("r", "dmesg | grep FA_LMAC_DATA_STATS_%s | tail -1", to_sta_key(sta->sta_mac, sta_key));
        if (fp) {
            fgets(buf, CLIENT_STATS_MAX_LEN_BUF, fp);
            v_secure_pclose(fp);

            len = strlen(buf);
            if (len) {
                ptr = buf + len;
                while (len-- && ptr-- && *ptr != ':');
                ptr++;

                value = strtok_r(ptr, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_DATA_QUEUED_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_DATA_DROPPED_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_DATA_DEQUED_TX_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_DATA_DEQUED_DROPPED_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_DATA_EXP_DROPPED_CNT_%d:%s", tmp,
                        apIndex+1, value);
            }
        }
        else {
            wifi_dbg_print(1, "Failed to run popen command\n" );
        }
    }
    else {
        wifi_dbg_print(1, "NULL sta\n" );
    }
}

static void upload_client_debug_stats_sta_fa_lmac_mgmt_stats(INT apIndex, sta_data_t *sta)
{
    INT len = 0;
    char *value = NULL;
    char *saveptr = NULL;
    char *ptr = NULL;
    FILE *fp  = NULL;
    sta_key_t sta_key;
    char tmp[128] = {0};
    char buf[CLIENT_STATS_MAX_LEN_BUF] = {0};

    memset (buf, 0, CLIENT_STATS_MAX_LEN_BUF);
    if(sta != NULL) {
        fp = (FILE *)(long int)v_secure_popen("r", "dmesg | grep FA_LMAC_MGMT_STATS_%s | tail -1", to_sta_key(sta->sta_mac, sta_key));
        if (fp) {
            fgets(buf, CLIENT_STATS_MAX_LEN_BUF, fp);
            v_secure_pclose(fp);

            len = strlen(buf);
            if (len)
            {
                ptr = buf + len;
                while (len-- && ptr-- && *ptr != ':');
                ptr++;

                value = strtok_r(ptr, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_MGMT_QUEUED_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_MGMT_DROPPED_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_MGMT_DEQUED_TX_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_MGMT_DEQUED_DROPPED_CNT_%d:%s", tmp,
                        apIndex+1, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log,
                        "\n%s WIFI_MGMT_EXP_DROPPED_CNT_%d:%s", tmp,
                        apIndex+1, value);
            }
        }
        else {
            wifi_dbg_print(1, "Failed to run popen command\n" );
        }
    }
    else {
        wifi_dbg_print(1, "NULL sta\n");
    }
}

static void upload_client_debug_stats_sta_vap_activity_stats(INT apIndex)
{
    INT len = 0;
    char *value = NULL;
    char *saveptr = NULL;
    char *ptr = NULL;
    FILE *fp  = NULL;
    char tmp[128] = {0};
    char buf[CLIENT_STATS_MAX_LEN_BUF] = {0};

    if (0 == apIndex) {

        memset (buf, 0, CLIENT_STATS_MAX_LEN_BUF);
        fp = (FILE *)(long int)v_secure_popen("r", "dmesg | grep VAP_ACTIVITY_ath0 | tail -1");
        if (fp)
        {
            fgets(buf, CLIENT_STATS_MAX_LEN_BUF, fp);
            v_secure_pclose(fp);

            len = strlen(buf);
            if (len)
            {
                ptr = buf + len;
                while (len-- && ptr-- && *ptr != ':');
                ptr += 3;

                value = strtok_r(ptr, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_1:%s\n", tmp, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_DATA_QUEUE_LEN_1:%s\n", tmp,
                        value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_DATA_QUEUE_BYTES_1:%s\n", tmp,
                        value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_DATA_FRAME_LEN_1:%s\n", tmp,
                        value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_DATA_FRAME_COUNT_1:%s\n", tmp,
                        value);
            }
        }
        else {
            wifi_dbg_print(1, "Failed to run popen command\n" );
        }

    }
    if (1 == apIndex) {
        memset (buf, 0, CLIENT_STATS_MAX_LEN_BUF);

        fp = (FILE *)(long int)v_secure_popen("r", "dmesg | grep VAP_ACTIVITY_ath1 | tail -1");
        if (fp)
        {
            fgets(buf, CLIENT_STATS_MAX_LEN_BUF, fp);
            v_secure_pclose(fp);

            len = strlen(buf);
            if (len)
            {
                ptr = buf + len;
                while (len-- && ptr-- && *ptr != ':');
                ptr += 3;

                value = strtok_r(ptr, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_2:%s\n", tmp, value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_DATA_QUEUE_LEN_2:%s\n", tmp,
                        value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_DATA_QUEUE_BYTES_2:%s\n", tmp,
                        value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_DATA_FRAME_LEN_2:%s\n", tmp,
                        value);
                value = strtok_r(NULL, ",", &saveptr);
                memset(tmp, 0, sizeof(tmp));
                get_formatted_time(tmp);
                write_to_file(wifi_health_log, "%s WIFI_PS_CLIENTS_DATA_FRAME_COUNT_2:%s\n", tmp,
                        value);
            }
        }
        else
        {
            wifi_dbg_print(1, "Failed to run popen command\n" );
        }
    }
}



/*
 * This API will Create telemetry and data model for client activity stats
 * like BytesSent, BytesReceived, RetransCount, FailedRetransCount, etc...
 */
int upload_client_debug_stats(void *arg)
{
    static INT apIndex = 0;
    static unsigned int phase = 0;
    static char vap_status[16] = {0};
    static hash_map_t     *sta_map;
    static sta_data_t *sta;
    static int phase_sta = 0;
    static int phase_fp = 0;

    if  (false == sWiFiDmlvApStatsFeatureEnableCfg)
    {
        wifi_dbg_print(1, "Client activity stats feature is disabled\n");
        phase_sta = 0;
        phase_fp = 0;
        apIndex = 0;
        phase = 0;
        return TIMER_TASK_COMPLETE;
    }

    if (phase == 0) {
        if (false == IsCosaDmlWiFiApStatsEnable(apIndex))
        {
            wifi_dbg_print(1, "Stats feature is disabled for apIndex = %d\n",apIndex+1);
            apIndex++;

#ifdef WIFI_HAL_VERSION_3
            if (apIndex >= (int)getTotalNumberVAPs())
#else
            if (apIndex >= MAX_VAP)
#endif
            {
                apIndex = 0;
                phase = 0;
                return TIMER_TASK_COMPLETE;
            }
            return TIMER_TASK_CONTINUE;
        }
        memset(vap_status,0,16);
        wifi_getApStatus(apIndex, vap_status);
        phase++;
        return TIMER_TASK_CONTINUE;
    }

    
    if(strncasecmp(vap_status,"Up",2)==0) {
        if (phase == 1) {
            upload_client_debug_stats_chan_stats(apIndex);
            phase++;
            return TIMER_TASK_CONTINUE;
        } else if (phase == 2) {
                if(phase_sta == 0){
                    sta_map = g_monitor_module.bssid_data[apIndex].sta_map;
                    sta = hash_map_get_first(sta_map);
                }
                if (sta != NULL) {
                    if (phase_fp == 0) {
                        upload_client_debug_stats_sta_fa_info(apIndex, sta);
                        phase_fp++;
                        return TIMER_TASK_CONTINUE;
                    } else if (phase_fp == 1) {
                        upload_client_debug_stats_sta_fa_lmac_data_stats(apIndex, sta);
                        phase_fp++;
                        return TIMER_TASK_CONTINUE;
                    } else if (phase_fp == 2) {
                        upload_client_debug_stats_sta_fa_lmac_mgmt_stats(apIndex, sta);
                        phase_fp++;
                        return TIMER_TASK_CONTINUE;
                    } else if(phase_fp ==3) {
                        upload_client_debug_stats_sta_vap_activity_stats(apIndex);
                    }
                    sta = hash_map_get_next(sta_map, sta);
                    phase_fp = 0;
                    if(sta != NULL){
                        phase_sta++;
                        return TIMER_TASK_CONTINUE;
                    }
                }
                phase++;
                phase_sta=0;
                return TIMER_TASK_CONTINUE;
        } else if (phase == 3) {
            upload_client_debug_stats_transmit_power_stats(apIndex);
            phase++;
            return TIMER_TASK_CONTINUE;
        } else if (phase == 4) {
            upload_client_debug_stats_acs_stats(apIndex);
        }
    }
    apIndex++;
    phase = 0;
#ifdef WIFI_HAL_VERSION_3
    if (apIndex >= (int)getTotalNumberVAPs())
#else
    if (apIndex >= MAX_VAP)
#endif
    {
        apIndex = 0;
        return TIMER_TASK_COMPLETE;
    }
    return TIMER_TASK_CONTINUE;
}

static void
process_stats_flag_changed(unsigned int ap_index, client_stats_enable_t *flag)
{

    if (0 == flag->type) //Device.WiFi.X_RDKCENTRAL-COM_vAPStatsEnable = 0
    {
	int idx;

	write_to_file(wifi_health_log, "WIFI_STATS_FEATURE_ENABLE:%s\n",
                (flag->enable) ? "true" : "false");
#ifdef WIFI_HAL_VERSION_3
        for(idx = 0; idx < (int)getTotalNumberVAPs(); idx++)
#else
	for(idx = 0; idx < MAX_VAP; idx++)
#endif
        {
            reset_client_stats_info(idx);
        }
    }
    else if (1 == flag->type) //Device.WiFi.AccessPoint.<vAP>.X_RDKCENTRAL-COM_StatsEnable = 1
    {
#ifdef WIFI_HAL_VERSION_3
        if(ap_index < getTotalNumberVAPs())
#else
        if(MAX_VAP > ap_index)
#endif
        {
            reset_client_stats_info(ap_index);
            write_to_file(wifi_health_log, "WIFI_STATS_ENABLE_%d:%s\n", ap_index+1,
                    (flag->enable) ? "true" : "false");
        }
    }
}

static void
radio_stats_flag_changed(unsigned int radio_index, client_stats_enable_t *flag)
{
#ifdef WIFI_HAL_VERSION_3
    for(UINT apIndex = 0; apIndex < getTotalNumberVAPs(); apIndex++)
    {
        if (radio_index == getRadioIndexFromAp(apIndex))
        {
            reset_client_stats_info(apIndex);
        }
        write_to_file(wifi_health_log, "WIFI_RADIO_STATUS_ENABLE_%d:%s\n", radio_index+1,
        (flag->enable) ? "true" : "false");
    }
#else
	int idx = 0;
	if (0 == radio_index)	//2.4GHz
	{

		for(idx = 0; idx <= MAX_VAP - 2; idx = idx + 2)
		{
			reset_client_stats_info(idx);
		}
		write_to_file(wifi_health_log, "WIFI_RADIO_STATUS_ENABLE_%d:%s\n", radio_index+1,
				(flag->enable) ? "true" : "false");
	} else {	//5GHz
		for(idx = 1; idx < MAX_VAP; idx = idx + 2)
		{
			reset_client_stats_info(idx);
		}
		write_to_file(wifi_health_log, "WIFI_RADIO_STATUS_ENABLE_%d:%s\n", radio_index+1,
				(flag->enable) ? "true" : "false");
	}
#endif
}

static void
vap_stats_flag_changed(unsigned int ap_index, client_stats_enable_t *flag)
{
      //Device.WiFi.SSID.<vAP>.Enable = 0
            reset_client_stats_info(ap_index);
            write_to_file(wifi_health_log, "WIFI_VAP_STATUS_ENABLE_%d:%s\n", ap_index+1,
                    (flag->enable) ? "true" : "false");
}

static void
get_sub_string(char *bandwidth, char *dest)
{
    if (5 == strlen(bandwidth)) // 20MHz. Copy only the first 2 bytes
        strncpy(dest, bandwidth, 2);
    else
        strncpy(dest, bandwidth, 3); //160MHz Copy only the first 3 bytes
}

#if defined (FEATURE_OFF_CHANNEL_SCAN_5G)

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : update_Off_Channel_5g_Scan_Params                             */
/*                                                                               */
/* DESCRIPTION   : This function update the parameters related to                */
/*                  5G off channel scan feature                                  */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NSCAN .i.e., Poll period for this feature                     */
/*                                                                               */
/*********************************************************************************/

int  update_Off_Channel_5g_Scan_Params()
{
    int i = 0, retPsmGet = CCSP_SUCCESS;
    char *strValue = NULL;

    char *Off_Channel_5g_params[RADIO_OFF_CHANNEL_NO_OF_PARAMS] = {
               "Device.DeviceInfo.X_RDK_RFC.Feature.OffChannelScan.Enable",
               "Device.WiFi.Radio.2.Radio_X_RDK_OffChannelTscan",
               "Device.WiFi.Radio.2.Radio_X_RDK_OffChannelNscan",
               "Device.WiFi.Radio.2.Radio_X_RDK_OffChannelTidle"
    };

    for (i = 0; i < RADIO_OFF_CHANNEL_NO_OF_PARAMS; i ++)
    {

        retPsmGet = PSM_Get_Record_Value2(bus_handle, g_Subsystem, Off_Channel_5g_params[i], NULL, &strValue);
        // if we get value from PSMDB successfully, update it to current values array
        // if PSM DB value fetch is a failed operation, update the current array with default values

/*********************************************************************************/
/*                                                                               */
/*      Radio_Off_Channel_current = {                                            */
/*          RFC,                                                                 */
/*          TSCAN,                                                               */
/*          NSCAN,                                                               */
/*          TIDLE                                                                */
/*      }                                                                        */
/*                                                                               */
/*********************************************************************************/

        if ((retPsmGet == CCSP_SUCCESS) && (strValue) && (strlen(strValue)))
        {
            Radio_Off_Channel_current[i] = atoi(strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
        else
        {
            CcspTraceError(("%s:%d The PSM_Get_Record_Value2  is failed with %d retval for %s \n",__FUNCTION__,__LINE__, retPsmGet, Off_Channel_5g_params[i]));
            Radio_Off_Channel_current[i] = Radio_Off_Channel_Default[i];
        }
    }
    CcspTraceInfo(("Off_channel_scan RFC = %d; TScan = %d; NScan = %d; TIDLE = %d \n ", Radio_Off_Channel_current[0], Radio_Off_Channel_current[1], 
	                                               ((Radio_Off_Channel_current[2] > 0) ? (24*3600/Radio_Off_Channel_current[2]) : Radio_Off_Channel_current[2]), Radio_Off_Channel_current[3] ));

    // 3rd element of Radio_Off_Channel_current is Nscan and has it value in seconds. This is our poll period
    return Radio_Off_Channel_current[2];
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : Off_Channel_5g_print_data                                     */
/*                                                                               */
/* DESCRIPTION   : This function prints the required information for             */
/*                  5G off channel scan feature into WiFiLog.txt                 */
/*                                                                               */
/* INPUT         : Neighbor report array and its size                            */
/*                 For XB6, channel list and number of channels is passed        */
/*                 additionally                                                  */
/*                                                                               */
/* OUTPUT        : Logs into  WiFiLog.txt                                        */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

#if (defined(FEATURE_OFF_CHANNEL_SCAN_5G_XB6))
static void
Off_Channel_5g_print_data(wifi_neighbor_ap2_t *neighbor_ap_array, int array_size, UINT *cur_channel_list, int channel_num)
{
    int i = 0, j = 0;
    wifi_neighbor_ap2_t *pt = NULL, *channel = NULL;
    CcspTraceDebug(("Off_channel_scan Total channels: %d \n", channel_num));
    for (i = 0; i < channel_num; i++) {
        int count = 0;
        for (j = 0, channel = neighbor_ap_array; j < array_size; j++, channel++) {
            if (channel->ap_Channel > *(cur_channel_list + i)) {
                break;
            } else if (channel->ap_Channel < *(cur_channel_list + i)) {
                continue;
            }
            count++;
        }
        CcspTraceInfo(("Off_channel_scan Total Scan Results:%d for channel %d \n", count, *(cur_channel_list + i)));
        int neighbor = 0;
        if (count > 0) {
            for (j = 0, pt = neighbor_ap_array; j < array_size; j++, pt++)
            {
                if (pt->ap_Channel > *(cur_channel_list + i)) {
                    break;
                } else if (pt->ap_Channel < *(cur_channel_list + i)) {
                    continue;
                }
                neighbor++;
                CcspTraceInfo(("Off_channel_scan Neighbor:%d ap_BSSID:%s ap_SignalStrength: %d\n", neighbor, pt->ap_BSSID, pt->ap_SignalStrength));
            }
        }
    }
}
#else /* (defined(FEATURE_OFF_CHANNEL_SCAN_5G_XB6)) */
static void
Off_Channel_5g_print_data(wifi_neighbor_ap2_t *neighbor_ap_array, int array_size)
{
    int i = 0;
    wifi_neighbor_ap2_t *pt = NULL;

    for (i = 0, pt = neighbor_ap_array; i < array_size; i++, pt++)
    {
        CcspTraceInfo(("Off_channel_scan Neighbor:%d ap_BSSID:%s ap_SignalStrength: %d\n", i + 1, pt->ap_BSSID, pt->ap_SignalStrength));
    }

}

#endif /* (defined(FEATURE_OFF_CHANNEL_SCAN_5G_XB6)) */

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : Off_Channel_5g_scanner                                        */
/*                                                                               */
/* DESCRIPTION   : This function prints the required information for             */
/*                  5G off channel scan feature into WiFiLog.txt                 */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : Status of 5G Off channel scan feature, DFS Feature,           */
/*                 value of Parameters related to 5G Off channel scan.           */
/*                                                                               */
/*                 if scanned, No of BSS heard on each channel into  WiFiLog.txt */
/*                                                                               */
/* RETURN VALUE  : INT                                                           */
/*                                                                               */
/*********************************************************************************/

static int
Off_Channel_5g_scanner(void *arg)
{

    //  let's check if the feature is enabled, and the values of Tscan, Nscan, Tidle are non zero .
    // if any of the above conditions fail, we will not scan and return back

/*********************************************************************************/
/*                                                                               */
/*      Radio_Off_Channel_current = {                                            */
/*          RFC,                                                                 */
/*          TSCAN,                                                               */
/*          NSCAN,                                                               */
/*          TIDLE                                                                */
/*      }                                                                        */
/*                                                                               */
/*********************************************************************************/

   char tmp[256] = {0},  tempBuf[32] = {0};
   int new_off_channel_scan_period = 0;

    if( (Radio_Off_Channel_current[0] == 0) || (Radio_Off_Channel_current[1] == 0) || (Radio_Off_Channel_current[2] == 0) || Radio_Off_Channel_current[3] == 0)
    {
        CcspTraceInfo(("Off_channel_scan feature is disabled returning RFC = %d; TScan = %d; NScan = %d; TIDLE = %d\n", Radio_Off_Channel_current[0], Radio_Off_Channel_current[1], Radio_Off_Channel_current[2], Radio_Off_Channel_current[3]));

        // this is to put an entry in psm with value of timestamp in which the last occurence of the function was called

        memset(tmp, 0, sizeof(tmp));
        memset(tempBuf, 0, sizeof(tempBuf));

        get_formatted_time(tempBuf);
        snprintf(tmp, 256, "Device.WiFi.Radio.2.Radio_X_RDK_OffChannel_LastUpdatedTime");
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, tmp, ccsp_string, tempBuf);
        new_off_channel_scan_period = update_Off_Channel_5g_Scan_Params();
        if ((g_monitor_module.curr_off_channel_scan_period != new_off_channel_scan_period) && (new_off_channel_scan_period != 0)) {
            scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.off_channel_scan_id, new_off_channel_scan_period*1000);
            g_monitor_module.curr_off_channel_scan_period = new_off_channel_scan_period; 
        }
        return TIMER_TASK_COMPLETE;
    }


    char *DFS_checker = "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.DFS.Enable", *strValue = NULL, countryStr[64] = {0};
    int isDFSenabled =  0, len = 0, iter = 0;

    UINT chan_list[MAX_5G_CHANNELS] = {'\0'};
#ifdef WIFI_HAL_VERSION_3
    wifi_radio_capabilities_t *wifiCapPtr = NULL;
    wifiCapPtr = getRadioCapability(RADIO_5G);
    len = wifiCapPtr->channel_list[0].num_channels;

    for(iter = 0;iter < len; iter++) {
        chan_list[iter] = wifiCapPtr->channel_list[0].channels_list[iter];
    }
#else
    char *list_ptr = NULL;
    memset(tmp, 0, sizeof(tmp));
    list_ptr = tmp;

    if(wifi_getRadioPossibleChannels(RADIO_5G,list_ptr)!= ANSC_STATUS_SUCCESS) {
        CcspTraceError(("%s:wifi_getRadioPossibleChannels failed!\n", __FUNCTION__));
    }else {
        while((strValue = strtok_r(NULL,",",&list_ptr)) != NULL){
            chan_list[iter++] = atoi(strValue);
            len++;
        }
        strValue = NULL;
    }
#endif /* WIFI_HAL_VERSION_3 */
    if (PSM_Get_Record_Value2(bus_handle, g_Subsystem, DFS_checker, NULL, &strValue) != CCSP_SUCCESS)
    {
        CcspTraceError(("%s: fail to get PSM record for %s!\n", __FUNCTION__, DFS_checker));
    }
    else
    {
        isDFSenabled = atoi(strValue);
        ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
    }

    wifi_getRadioCountryCode(1, countryStr);
    if (isDFSenabled == 1)
    {
        if( !((strncmp(countryStr, "US", 2) == 0) || (strncmp(countryStr, "CA" , 2) == 0) ||
            (strncmp(countryStr, "GB", 2) == 0)) ) {

            /* skip the scan for any country code apart from US,CA and GB */
            CcspTraceError(("Getting country code %s; skipping the scan!\n", countryStr));
            new_off_channel_scan_period = update_Off_Channel_5g_Scan_Params();
            if ((g_monitor_module.curr_off_channel_scan_period != new_off_channel_scan_period) && (new_off_channel_scan_period != 0)) {
               scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.off_channel_scan_id, new_off_channel_scan_period*1000);
               g_monitor_module.curr_off_channel_scan_period = new_off_channel_scan_period;
            }
            return TIMER_TASK_COMPLETE;
        }
    }

    CcspTraceInfo(("Off_channel_scan isDFSenabled = %d and country code = %s\n", isDFSenabled, countryStr));

    int i = 0, retry = 5, ret = 0, j = 0;
    wifi_neighbor_ap2_t *neighbor_ap_array = NULL;
    UINT array_size = 0;
    char *ChannelNChannels = "Device.WiFi.Radio.2.Radio_X_RDK_OffChannelNchannel";
    ULONG cur_channel = 36;
    wifi_getRadioChannel(1, &cur_channel);

#if (defined(FEATURE_OFF_CHANNEL_SCAN_5G_XB6))
    UINT cur_chan_list[MAX_5G_CHANNELS] = {'\0'};
    int channel_num = 0;
    retry = 20;
    for (i = 0; i < len; i++)
    {
        if ((isDFSenabled == 0) && (chan_list[i] >= DFS_CH_START) && (chan_list[i] <= DFS_CH_END)) {
            continue;
        }

        if (chan_list[i] == cur_channel) {
            CcspTraceInfo(("Off_channel_scan off channel number is same as current channel, skipping the off chan scan for %d\n", chan_list[i]));
            continue;
        }

        cur_chan_list[channel_num] = chan_list[i];
        ++channel_num;
    }
    CcspTraceInfo(("Off_channel_scan start scan, number of channels %d \n", channel_num));
    for (j = 0; j < retry; j++)
    {
        wifi_startNeighborScan(1, 3, Radio_Off_Channel_current[1], channel_num, cur_chan_list);
        ret = wifi_getNeighboringWiFiStatus(1, &neighbor_ap_array, &array_size);
        if (ret == RETURN_OK)
        {
            CcspTraceDebug(("Off_channel_scan Return success, Total APs: %d \n", array_size));
            if (array_size > 0)
            {
                Off_Channel_5g_print_data(neighbor_ap_array, array_size, cur_chan_list, channel_num);
            } else {
                for (i = 0; i < channel_num; i++)
                {
                    CcspTraceInfo(("Off_channel_scan Total Scan Results:0 for channel %d \n", *(cur_chan_list + i)));
                }
            }

            if (neighbor_ap_array)
            {
                free(neighbor_ap_array);
                neighbor_ap_array = NULL;
            }

            break;

        } else {
            CcspTraceDebug(("Off_channel_scan neighbor scan data was not fetched. Sleeping for 250msec, retry number: %d, max retry limit: %d \n", j+1, retry));
            usleep(250000);
        }

        if ((ret != RETURN_OK) && ((j + 1) == retry)) {
            CcspTraceError(("Off_channel_scan Neighborscan failed even after 20 retries, no data of neighbors was fetched \n"));
        }
    }
#else /* (defined(FEATURE_OFF_CHANNEL_SCAN_5G_XB6)) */
    for (i = 0; i < len; i ++)
    {
        if((UINT)(cur_channel) == chan_list[i])
        {
            CcspTraceInfo(("Off_channel_scan  off channel number is same as current channel, skipping the off chan scan for %d\n", chan_list[i]));
            continue;
        } else if((isDFSenabled == 0) && (chan_list[i] >= DFS_CH_START) && (chan_list[i] <= DFS_CH_END)) {
            /* Skipping DFS channels when DFS is disabled */
            continue;
        } else {
            wifi_startNeighborScan(1, 3, Radio_Off_Channel_current[1], 1, &chan_list[i]);
            for (j = 0; j < retry; j++)
            {
                ret = wifi_getNeighboringWiFiStatus(1, &neighbor_ap_array, &array_size);

                if (ret == RETURN_OK)
                {
                    CcspTraceInfo(("Off_channel_scan Total Scan Results:%d for channel %d \n", array_size, chan_list[i]));

                    if(array_size > 0)
                    {
                        Off_Channel_5g_print_data(neighbor_ap_array, array_size);
                    }

                    if (neighbor_ap_array)
                    {
                        free(neighbor_ap_array);
                        neighbor_ap_array = NULL;
                    }

                    break;
                }
                else
                {
                    /* sleep for one more second and retry if we can get the scan results */
                    sleep(1);
                }
            }
        }
    }
#endif /* (defined(FEATURE_OFF_CHANNEL_SCAN_5G_XB6)) */


    wifi_channelMetrics_t * ptr,  channelMetrics_array_1[MAX_5G_CHANNELS];
    int num_channels = 0;
    ptr = channelMetrics_array_1;
    memset(channelMetrics_array_1, 0, sizeof(channelMetrics_array_1));
    for (i = 0; i < len; i++)
    {
        if((isDFSenabled == 0) && (chan_list[i] >= DFS_CH_START) && (chan_list[i] <= DFS_CH_END)) {
            /* Skipping DFS channels when DFS is disabled */
            continue;
        }
        ptr[num_channels].channel_in_pool = TRUE;
        ptr[num_channels].channel_number = (chan_list[i]);
        ++num_channels;
    }

    ret = wifi_getRadioDcsChannelMetrics(1, ptr, MAX_5G_CHANNELS);

    for (i = 0; i < num_channels; i++, ptr++)
    {
        CcspTraceInfo(("Off_channel_scan Channel number:%d Channel Utilization:%d \n",ptr->channel_number, ptr->channel_utilization));
    }

    memset(tempBuf, 0, sizeof(tempBuf));
    snprintf(tempBuf, sizeof(tempBuf), "%d", (len - 1));
    PSM_Set_Record_Value2(bus_handle,g_Subsystem, ChannelNChannels, ccsp_string, tempBuf);

    // this is to put an entry in psm with value of timestamp in which the last occurence of the function was called
    memset(tmp, 0, sizeof(tmp));
    memset(tempBuf, 0, sizeof(tempBuf));

    get_formatted_time(tempBuf);
    snprintf(tmp, 256, "Device.WiFi.Radio.2.Radio_X_RDK_OffChannel_LastUpdatedTime");
    PSM_Set_Record_Value2(bus_handle,g_Subsystem, tmp, ccsp_string, tempBuf);
    new_off_channel_scan_period = update_Off_Channel_5g_Scan_Params();
    if ((g_monitor_module.curr_off_channel_scan_period != new_off_channel_scan_period) && (new_off_channel_scan_period != 0)) {
        scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.off_channel_scan_id, new_off_channel_scan_period*1000);
        g_monitor_module.curr_off_channel_scan_period = new_off_channel_scan_period;
    }
    return TIMER_TASK_COMPLETE;
}
#endif // (FEATURE_OFF_CHANNEL_SCAN_5G)

int upload_channel_width_telemetry(void *arg)
{
    char buffer[64] = {0};
    char bandwidth[4] = {0};
    char tmp[128] = {0};
    char buff[1024] = {0};
#ifdef WIFI_HAL_VERSION_3
    CHAR eventName[32] = {0};
    BOOL radioEnabled = FALSE;
    UINT numRadios = getNumberRadios();
    for (UINT i = 0; i < numRadios; ++i)
    {
        if (wifi_getRadioEnable(i, &radioEnabled))
        {
            CcspTraceWarning(("%s : failed to wifi_getRadioEnable with radio index %u\n", __FUNCTION__, i));
            radioEnabled = FALSE;
        }
        if (radioEnabled)
        {
            wifi_getRadioOperatingChannelBandwidth(i, buffer);
            get_sub_string(buffer, bandwidth);
            get_formatted_time(tmp);
            snprintf(buff, 1024, "%s WiFi_config_%dG_chan_width_split:%s\n", tmp, convertRadioIndexToFrequencyNum(i), bandwidth);
            write_to_file(wifi_health_log, buff);

            snprintf(eventName, sizeof(eventName), "WIFI_CWconfig_%d_split", i + 1 );
            t2_event_s(eventName, bandwidth);


            memset(buffer, 0, sizeof(buffer));
            memset(bandwidth, 0, sizeof(bandwidth));
            memset(tmp, 0, sizeof(tmp));
        }
    }

#else
    BOOL radio_0_Enabled=FALSE;
    BOOL radio_1_Enabled=FALSE;
    wifi_getRadioEnable(1, &radio_1_Enabled);
    wifi_getRadioEnable(0, &radio_0_Enabled);
    if(radio_0_Enabled)
    {
        wifi_getRadioOperatingChannelBandwidth(0, buffer);
        get_sub_string(buffer, bandwidth);
        get_formatted_time(tmp);
        snprintf(buff, 1024, "%s WiFi_config_2G_chan_width_split:%s\n", tmp, bandwidth);
        write_to_file(wifi_health_log, buff);
        //"header": "WIFI_CWconfig_1_split", "content": "WiFi_config_2G_chan_width_split:", "type": "wifihealth.txt",
        t2_event_s("WIFI_CWconfig_1_split", bandwidth);

        memset(buffer, 0, sizeof(buffer));
        memset(bandwidth, 0, sizeof(bandwidth));
        memset(tmp, 0, sizeof(tmp));
        memset(buff, 0, sizeof(buff));
    }
    if(radio_1_Enabled)
    {
        wifi_getRadioOperatingChannelBandwidth(1, buffer);
        get_sub_string(buffer, bandwidth);
        get_formatted_time(tmp);
        snprintf(buff, 1024, "%s WiFi_config_5G_chan_width_split:%s\n", tmp, bandwidth);
        write_to_file(wifi_health_log, buff);

        // "header": "WIFI_CWconfig_2_split", "content": "WiFi_config_5G_chan_width_split:", "type": "wifihealth.txt",
        t2_event_s("WIFI_CWconfig_2_split", bandwidth);
    }
#endif
    return TIMER_TASK_COMPLETE;
}

int upload_ap_telemetry_pmf(void *arg)
{
	int i;
	bool bFeatureMFPConfig=false;
	char tmp[128]={0};
	char log_buf[1024]={0};
	char telemetry_buf[1024]={0};
	char pmf_config[16]={0};
	errno_t rc = -1;

	wifi_dbg_print(1, "Entering %s:%d \n", __FUNCTION__, __LINE__);

	// Telemetry:
	// "header":  "WIFI_INFO_PMF_ENABLE"
	// "content": "WiFi_INFO_PMF_enable:"
	// "type": "wifihealth.txt",
	CosaDmlWiFi_GetFeatureMFPConfigValue((BOOLEAN *)&bFeatureMFPConfig);
	rc = sprintf_s(telemetry_buf, sizeof(telemetry_buf), "%s", bFeatureMFPConfig?"true":"false");
	if(rc < EOK)
	{
		ERR_CHK(rc);
	}
	get_formatted_time(tmp);
	rc = sprintf_s(log_buf, sizeof(log_buf), "%s WIFI_INFO_PMF_ENABLE:%s\n", tmp, (bFeatureMFPConfig?"true":"false"));
	if(rc < EOK)
	{
		ERR_CHK(rc);
	}
	write_to_file(wifi_health_log, log_buf);
	wifi_dbg_print(1, "%s", log_buf);
	t2_event_s("WIFI_INFO_PMF_ENABLE", telemetry_buf);

	// Telemetry:
	// "header":  "WIFI_INFO_PMF_CONFIG_1"
	// "content": "WiFi_INFO_PMF_config_ath0:"
	// "type": "wifihealth.txt",
#ifdef WIFI_HAL_VERSION_3
    for(i = 0; i < (int)getTotalNumberVAPs(); i++) 
    {
        if (isVapPrivate(i))
        {
#else
	for(i=0;i<2;i++) // for(i=0;i<MAX_VAP;i++)
	{
#endif
		memset(pmf_config, 0, sizeof(pmf_config));
		wifi_getApSecurityMFPConfig(i, pmf_config);
		rc = strcpy_s(telemetry_buf, sizeof(telemetry_buf), pmf_config);
		ERR_CHK(rc);

		get_formatted_time(tmp);
		rc = sprintf_s(log_buf, sizeof(log_buf), "%s WIFI_INFO_PMF_CONFIG_%d:%s\n", tmp, i+1, telemetry_buf);
		if(rc < EOK)
		{
			ERR_CHK(rc);
		}
		write_to_file(wifi_health_log, log_buf);
		wifi_dbg_print(1, "%s", log_buf);

		rc = sprintf_s(tmp, sizeof(tmp), "WIFI_INFO_PMF_CONFIG_%d", i+1);
		if(rc < EOK)
		{
			ERR_CHK(rc);
		}
		t2_event_s(tmp, telemetry_buf);
#ifdef WIFI_HAL_VERSION_3
        }
#endif
    }
	wifi_dbg_print(1, "Exiting %s:%d \n", __FUNCTION__, __LINE__);
    return TIMER_TASK_COMPLETE;
}

/*
 * wifi_stats_flag_change()
 * ap_index vAP
 * enable   true/false
 * type     Device.WiFi.X_RDKCENTRAL-COM_vAPStatsEnable= 0,
            Device.WiFi.AccessPoint.<vAP>.X_RDKCENTRAL-COM_StatsEnable = 1
*/
int wifi_stats_flag_change(int ap_index, bool enable, int type)
{
    wifi_monitor_data_t *data;

    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    data->id = msg_id++;

    data->event_type = monitor_event_type_StatsFlagChange;

    data->ap_index = ap_index;

    data->u.flag.type = type;
    data->u.flag.enable = enable;


    wifi_dbg_print(1, "%s:%d: flag changed apIndex=%d enable=%d type=%d\n",
        __func__, __LINE__, ap_index, enable, type);

    pthread_mutex_lock(&g_monitor_module.lock);
    queue_push(g_monitor_module.queue, data);

    pthread_cond_signal(&g_monitor_module.cond);
    pthread_mutex_unlock(&g_monitor_module.lock);

    return 0;
}

/*
 * radio_stats_flag_change()
 * ap_index vAP
 * enable   true/false
 * type     Device.WiFi.Radio.<Index>.Enable = 1
*/
int radio_stats_flag_change(int radio_index, bool enable)
{
    wifi_monitor_data_t *data;

    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    data->id = msg_id++;

    data->event_type = monitor_event_type_RadioStatsFlagChange;

    data->ap_index = radio_index;	//Radio_Index = 0, 1

    data->u.flag.enable = enable;

    wifi_dbg_print(1, "%s:%d: flag changed radioIndex=%d enable=%d\n",
        __func__, __LINE__, radio_index, enable);

    pthread_mutex_lock(&g_monitor_module.lock);
    queue_push(g_monitor_module.queue, data);

    pthread_cond_signal(&g_monitor_module.cond);
    pthread_mutex_unlock(&g_monitor_module.lock);

    return 0;
}

/*
 * vap_stats_flag_change()
 * ap_index vAP
 * enable   true/false
 * type     Device.WiFi.SSID.<vAP>.Enable = 0
*/
int vap_stats_flag_change(int ap_index, bool enable)
{
    wifi_monitor_data_t *data;

    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    data->id = msg_id++;

    data->event_type = monitor_event_type_VapStatsFlagChange;

    data->ap_index = ap_index;	//vap_Index

    data->u.flag.enable = enable;

    wifi_dbg_print(1, "%s:%d: flag changed vapIndex=%d enable=%d \n",
        __func__, __LINE__, ap_index, enable);

    pthread_mutex_lock(&g_monitor_module.lock);

#if defined (FEATURE_CSI)
    if(enable == FALSE) {
        csi_vap_down_update(ap_index);
    }
#endif    
    queue_push(g_monitor_module.queue, data);

    pthread_cond_signal(&g_monitor_module.cond);
    pthread_mutex_unlock(&g_monitor_module.lock);

    return 0;
}

void process_diagnostics (unsigned int ap_index, wifi_associated_dev3_t *dev, unsigned int num_devs)
{
    hash_map_t     *sta_map = NULL;
    sta_data_t *sta = NULL, *tmp_sta = NULL;
    unsigned int i;
    wifi_associated_dev3_t *hal_sta;
    sta_key_t sta_key;
    char bssid[MIN_MAC_LEN+1];
    struct timeval tv_now;

    wifi_associated_dev_stats_t * cli_stats;
    BOOL ret;
    int alloc_count = 0;
    ULLONG handle;

    sta_map = g_monitor_module.bssid_data[ap_index].sta_map;
    gettimeofday(&tv_now, NULL);


    snprintf(bssid, MIN_MAC_LEN+1, "%02x%02x%02x%02x%02x%02x",
                g_monitor_module.bssid_data[ap_index].bssid[0], g_monitor_module.bssid_data[ap_index].bssid[1],
                g_monitor_module.bssid_data[ap_index].bssid[2], g_monitor_module.bssid_data[ap_index].bssid[3],
                g_monitor_module.bssid_data[ap_index].bssid[4], g_monitor_module.bssid_data[ap_index].bssid[5]);

    hal_sta = dev;
    memset(sta_key, 0, STA_KEY_LEN);
    // update all sta(s) that are in the record retrieved from hal
    if (NULL != hal_sta) {
        for (i = 0; i < num_devs; i++) {
            sta = (sta_data_t *)hash_map_get(sta_map, to_sta_key(hal_sta->cli_MACAddress, sta_key));
            if (sta == NULL) {
                sta = (sta_data_t *)malloc(sizeof(sta_data_t));
                if (sta == NULL) {
                    CcspTraceError(( "[%s:%d] number of [%d] sta data memroy allocation malloc failure\r\n", __func__, __LINE__, num_devs));
                    return;
                }
                memset(sta, 0, sizeof(sta_data_t));
                memcpy(sta->sta_mac, hal_sta->cli_MACAddress, sizeof(mac_address_t));
                hash_map_put(sta_map, strdup(to_sta_key(hal_sta->cli_MACAddress, sta_key)), sta);
            }

            //wifi_dbg_print(1, "Current Stored for:%s Packets Sent:%d Packets Recieved:%d Errors Sent:%d Retrans:%d Retry:%d Multiple:%d at index:%d on vap:%d\n",
            //    to_sta_key(sta->dev_stats.cli_MACAddress, sta_key),
            //    sta->dev_stats.cli_PacketsSent, sta->dev_stats.cli_PacketsReceived, sta->dev_stats.cli_ErrorsSent,
            //    sta->dev_stats.cli_RetransCount, sta->dev_stats.cli_RetryCount, sta->dev_stats.cli_MultipleRetryCount, i, ap_index);

            memcpy((unsigned char *)&sta->dev_stats, (unsigned char *)hal_sta, sizeof(wifi_associated_dev3_t));

            //wifi_dbg_print(1, "Current Polled for:%s Packets Sent:%d Packets Recieved:%d Errors Sent:%d Retrans:%d Retry:%d Multiple:%d\n",
            //    to_sta_key(sta->dev_stats.cli_MACAddress, sta_key),
            //    hal_sta->cli_PacketsSent, hal_sta->cli_PacketsReceived, hal_sta->cli_ErrorsSent,
            //    hal_sta->cli_RetransCount, hal_sta->cli_RetryCount, hal_sta->cli_MultipleRetryCount);
            //wifi_dbg_print(1, "Current Last for: %s Packets Sent:%d Packets Recieved:%d Errors Sent:%d Retrans:%d Retry:%d Multiple:%d\n",
            //    to_sta_key(sta->dev_stats.cli_MACAddress, sta_key),
            //    sta->dev_stats_last.cli_PacketsSent, sta->dev_stats_last.cli_PacketsReceived, sta->dev_stats_last.cli_ErrorsSent,
            //    sta->dev_stats_last.cli_RetransCount, sta->dev_stats_last.cli_RetryCount, sta->dev_stats_last.cli_MultipleRetryCount);
            cli_stats = (wifi_associated_dev_stats_t *) malloc(sizeof(wifi_associated_dev_stats_t));
            if(cli_stats == NULL) {
                wifi_dbg_print(1, "%s %d Failed to allocate memory. Retrying",__func__,__LINE__);
                while (cli_stats == NULL && alloc_count<5) {
                    cli_stats = (wifi_associated_dev_stats_t *) malloc(sizeof(wifi_associated_dev_stats_t));
                    alloc_count++;
                }
                if (alloc_count >= 5) {
                    wifi_dbg_print(1, "%s %d Failed to allocate memory as system resources are unavailable",__func__,__LINE__);
                }
            }
            ret = wifi_getApAssociatedDeviceStats(ap_index, &(hal_sta->cli_MACAddress) , cli_stats, &handle);

            if ((ret == RETURN_OK) && (cli_stats != NULL))
                sta->cli_rx_retries = cli_stats->cli_rx_retries;

            if (cli_stats) {
                free(cli_stats);
                cli_stats = NULL;
            }

            sta->updated = true;

            //RDKB-41125
            if ((UINT)(tv_now.tv_sec - sta->last_disconnected_time.tv_sec) <= g_monitor_module.bssid_data[i].ap_params.rapid_reconnect_threshold) {
                if (sta->dev_stats.cli_Active == false) {
                    wifi_dbg_print(1, "Func:%s Device:%s connected on ap:%d connected within rapid reconnect time\n",__FUNCTION__, to_sta_key(sta->sta_mac, sta_key), ap_index);
                    sta->rapid_reconnects++;
                }
            }

            sta->dev_stats.cli_Active = true;
            sta->dev_stats.cli_SignalStrength = hal_sta->cli_SignalStrength;  //zqiu: use cli_SignalStrength as normalized rssi
            if (sta->dev_stats.cli_SignalStrength >= g_monitor_module.sta_health_rssi_threshold) {
                sta->good_rssi_time += g_monitor_module.poll_period;
            } else {
                sta->bad_rssi_time += g_monitor_module.poll_period;
            }

            sta->connected_time += g_monitor_module.poll_period;
            wifi_dbg_print(1, "Polled station info for, vap:%d bssid:%s ClientMac:%s Uplink rate:%d Downlink rate:%d Packets Sent:%d Packets Recieved:%d Errors Sent:%d Retrans:%d ErrorsRx:%llu ErrorsRxPrev:%llu\n",
                ap_index+1, bssid, to_sta_key(sta->dev_stats.cli_MACAddress, sta_key), sta->dev_stats.cli_LastDataUplinkRate, sta->dev_stats.cli_LastDataDownlinkRate,
                sta->dev_stats.cli_PacketsSent, sta->dev_stats.cli_PacketsReceived, sta->dev_stats.cli_ErrorsSent, sta->dev_stats.cli_RetransCount, sta->cli_rx_retries, sta->prev_cli_rx_retries);
#ifdef WIFI_HAL_VERSION_3
            wifi_dbg_print(1, "Polled radio NF %d \n",g_monitor_module.radio_data[getRadioIndexFromAp(ap_index)].NoiseFloor);
            wifi_dbg_print(1, "Polled channel info for radio %d : channel util:%d, channel interference:%d \n",
                getRadioIndexFromAp(ap_index), g_monitor_module.radio_data[getRadioIndexFromAp(ap_index)].channelUtil, g_monitor_module.radio_data[getRadioIndexFromAp(ap_index)].channelInterference);
#else
            wifi_dbg_print(1, "Polled radio NF %d \n",g_monitor_module.radio_data[ap_index % 2].NoiseFloor);
            wifi_dbg_print(1, "Polled channel info for radio 2.4 : channel util:%d, channel interference:%d \n", g_monitor_module.radio_data[0].channelUtil, g_monitor_module.radio_data[0].channelInterference);
            wifi_dbg_print(1, "Polled channel info for radio 5 : channel util:%d, channel interference:%d \n", g_monitor_module.radio_data[1].channelUtil, g_monitor_module.radio_data[1].channelInterference);
#endif
            hal_sta++;
        }
    } else {
        /* Log Error message only when the incoming dev structure is NULL due to some error conditions */
        if (num_devs > 0) {
            CcspTraceError(( "[%s:%d]Wi-Fi associated device map is NULL for vap_index:%d number of device:%d\r\n", __func__, __LINE__, ap_index, num_devs));
        }
    }
    // now update all sta(s) in cache that were not updated
    sta = hash_map_get_first(sta_map);
    while (sta != NULL) {

        if (sta->updated == true) {
            sta->updated = false;
        } else {
            // this was not present in hal record
            sta->disconnected_time += g_monitor_module.poll_period;
            sta->dev_stats.cli_Active = false;
            wifi_dbg_print(1, "Device:%s is disassociated from ap:%d, for %d amount of time, assoc status:%d\n", to_sta_key(sta->sta_mac, sta_key), ap_index, sta->disconnected_time, sta->dev_stats.cli_Active);
            if ((sta->disconnected_time > 4*g_monitor_module.poll_period) && (sta->dev_stats.cli_Active == false)) {
                tmp_sta = sta;
            }
        }

        sta = hash_map_get_next(sta_map, sta);

        if (tmp_sta != NULL) {
            wifi_dbg_print(1, "Device:%s being removed from map of ap:%d, and being deleted\n", to_sta_key(tmp_sta->sta_mac, sta_key), ap_index);
            hash_map_remove(sta_map, to_sta_key(tmp_sta->sta_mac, sta_key));
            free(tmp_sta);
            tmp_sta = NULL;
        }
    }
}

void process_deauthenticate	(unsigned int ap_index, auth_deauth_dev_t *dev)
{
        char buff[2048];
        char tmp[128];
   	sta_key_t sta_key;
 
        wifi_dbg_print(1, "%s:%d Device:%s deauthenticated on ap:%d with reason : %d\n", __func__, __LINE__, to_sta_key(dev->sta_mac, sta_key), ap_index, dev->reason);

        get_formatted_time(tmp);
        /* Counting the association rejection because AP is unable to handle additional associated stations*/
        if ( dev->reason == 17 )
        {
            g_monitor_module.ap_params[ap_index].ap_rejected_sta_count++;
            strncpy(g_monitor_module.ap_params[ap_index].last_time_ap_rejected_sta, tmp, sizeof(g_monitor_module.ap_params[ap_index].last_time_ap_rejected_sta));
	}

        /*Wrong password on private, Xfinity Home and LNF SSIDs*/
#ifdef WIFI_HAL_VERSION_3
        //event_type: 0=Reserved; 1=unspecified reason; 2=wrong password;
        if ((dev->reason == 2) && ( isVapPrivate(ap_index) || isVapXhs(ap_index) || isVapLnfPsk(ap_index) ) )
#else
        if ((dev->reason == 2) && ( ap_index == 0 || ap_index == 1 || ap_index == 2 || ap_index == 3 || ap_index == 6 || ap_index == 7 )) 
#endif
        {
       	 
   	       snprintf(buff, 2048, "%s WIFI_PASSWORD_FAIL:%d,%s\n", tmp, ap_index + 1, to_sta_key(dev->sta_mac, sta_key));
	       /* send telemetry of password failure */
	       write_to_file(wifi_health_log, buff);
        }
        /*ARRISXB6-11979 Possible Wrong WPS key on private SSIDs*/
#ifdef WIFI_HAL_VERSION_3
        if ((dev->reason == 2 || dev->reason == 14 || dev->reason == 19) && ( isVapPrivate(ap_index) )) 
#else
        if ((dev->reason == 2 || dev->reason == 14 || dev->reason == 19) && ( ap_index == 0 || ap_index == 1 )) 
#endif
        {
       	 
   	       snprintf(buff, 2048, "%s WIFI_POSSIBLE_WPS_PSK_FAIL:%d,%s,%d\n", tmp, ap_index + 1, to_sta_key(dev->sta_mac, sta_key), dev->reason);
	       /* send telemetry of WPS failure */
	       write_to_file(wifi_health_log, buff);
        }
        /*Calling process_disconnect as station is disconncetd from vAP*/
        process_disconnect(ap_index, dev);
}

void process_connect	(unsigned int ap_index, auth_deauth_dev_t *dev)
{
    sta_key_t sta_key;
    sta_data_t *sta;
    hash_map_t     *sta_map;
    struct timeval tv_now;
    unsigned int i = 0;
    char vap_status[16];

    sta_map = g_monitor_module.bssid_data[ap_index].sta_map;
    
    wifi_dbg_print(1, "sta map: %p Device:%s connected on ap:%d\n", sta_map, to_sta_key(dev->sta_mac, sta_key), ap_index);
    sta = (sta_data_t *)hash_map_get(sta_map, to_sta_key(dev->sta_mac, sta_key));
    if (sta == NULL) { /* new client */
        sta = (sta_data_t *)malloc(sizeof(sta_data_t));
        memset(sta, 0, sizeof(sta_data_t));
        memcpy(sta->sta_mac, dev->sta_mac, sizeof(mac_addr_t));
        hash_map_put(sta_map, strdup(to_sta_key(sta->sta_mac, sta_key)), sta);
        wifi_dbg_print(1,"Func:%s Line:%d Device:%s created a new entry \n",__FUNCTION__,__LINE__,to_sta_key(dev->sta_mac, sta_key));
    }
    
    sta->total_disconnected_time += sta->disconnected_time;
    sta->disconnected_time = 0;
    
    gettimeofday(&tv_now, NULL);
    if(!sta->assoc_monitor_start_time)
        sta->assoc_monitor_start_time = tv_now.tv_sec;

    if ((UINT)(tv_now.tv_sec - sta->last_disconnected_time.tv_sec) <= g_monitor_module.bssid_data[i].ap_params.rapid_reconnect_threshold) {
        if (sta->dev_stats.cli_Active == false) {
             wifi_dbg_print(1, "Device:%s connected on ap:%d connected within rapid reconnect time\n", to_sta_key(dev->sta_mac, sta_key), ap_index);
             sta->rapid_reconnects++;
	} else {
             wifi_dbg_print(1, "Device:%s connected on ap:%d received another connection event\n", to_sta_key(dev->sta_mac, sta_key), ap_index);
	}
    }
    
    sta->last_connected_time.tv_sec = tv_now.tv_sec;
    sta->last_connected_time.tv_usec = tv_now.tv_usec;
    sta->cli_CapableNumSpatialStreams = dev->cli_CapableNumSpatialStreams;

        /* reset stats of client */
	memset((unsigned char *)&sta->dev_stats, 0, sizeof(wifi_associated_dev3_t));
	memset((unsigned char *)&sta->dev_stats_last, 0, sizeof(wifi_associated_dev3_t));
	sta->prev_cli_rx_retries = 0;
  
	sta->dev_stats.cli_Active = true;
	sta->dev_stats.cli_RSSI = dev->cli_RSSI;
        /*To avoid duplicate entries in hash map of different vAPs eg:RDKB-21582
          Also when clients moved away from a vAP and connect back to other vAP this will be usefull*/
#ifdef WIFI_HAL_VERSION_3
        for (i = 0; i < getTotalNumberVAPs(); i++) {
#else
        for (i = 0; i < MAX_VAP; i++) {
#endif
              if ( i == ap_index)
                    continue;
              memset(vap_status,0,16);
              wifi_getApStatus(i, vap_status);
              if(strncasecmp(vap_status,"Up",2)==0) {
                   sta_map = g_monitor_module.bssid_data[i].sta_map;
                   sta = (sta_data_t *)hash_map_get(sta_map, to_sta_key(dev->sta_mac, sta_key));
                   if ((sta != NULL) && (sta->dev_stats.cli_Active == true)) {
                        sta->dev_stats.cli_Active = false;
                   }
           
              }
        }
}

void process_disconnect	(unsigned int ap_index, auth_deauth_dev_t *dev)
{
    sta_key_t sta_key;
    sta_data_t *sta;
    hash_map_t     *sta_map;
    struct timeval tv_now;
	instant_msmt_t	msmt;

    //Lib hostap callback
#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
    if (isLibHostapDisAssocEvent)
    {
        if (wifi_disassoc_frame_rx_callback_register(ap_index, dev->sta_mac, dev->reason) != 0)
            wifi_dbg_print(1, "%s:%d: Device disassociation failed in lib hostap\n", __func__, __LINE__);

        isLibHostapDisAssocEvent = FALSE;
    }
#endif //FEATURE_HOSTAP_AUTHENTICATOR

    sta_map = g_monitor_module.bssid_data[ap_index].sta_map;
    wifi_dbg_print(1, "Device:%s disconnected on ap:%d\n", to_sta_key(dev->sta_mac, sta_key), ap_index);
    sta = (sta_data_t *)hash_map_get(sta_map, to_sta_key(dev->sta_mac, sta_key));
    if (sta == NULL) {
		wifi_dbg_print(1, "Device:%s could not be found on sta map of ap:%d\n", to_sta_key(dev->sta_mac, sta_key), ap_index);
		return;
    }
    
    sta->total_connected_time += sta->connected_time;
    sta->connected_time = 0;
	sta->dev_stats.cli_Active = false;
    gettimeofday(&tv_now, NULL);
    if(!sta->deauth_monitor_start_time)
        sta->deauth_monitor_start_time = tv_now.tv_sec;

    sta->last_disconnected_time.tv_sec = tv_now.tv_sec;
    sta->last_disconnected_time.tv_usec = tv_now.tv_usec;

	// stop instant measurements if its going on with this client device
	msmt.ap_index = ap_index;
	memcpy(msmt.sta_mac, dev->sta_mac, sizeof(mac_address_t));
        /* stop the instant measurement only if the client for which instant measuremnt
           is running got disconnected from AP
         */
        if (memcmp(g_monitor_module.inst_msmt.sta_mac, msmt.sta_mac, sizeof(mac_address_t)) == 0)
        {
	    process_instant_msmt_stop(ap_index, &msmt);
        }
}

void process_instant_msmt_start	(unsigned int ap_index, instant_msmt_t *msmt)
{
	memcpy(g_monitor_module.inst_msmt.sta_mac, msmt->sta_mac, sizeof(mac_address_t));
	g_monitor_module.inst_msmt.ap_index = ap_index;
	g_monitor_module.poll_period = g_monitor_module.instantPollPeriod;
	g_monitor_module.inst_msmt.active = g_monitor_module.instntMsmtenable;

        if((g_monitor_module.instantDefOverrideTTL == 0) || (g_monitor_module.instantPollPeriod == 0))
	    g_monitor_module.maxCount = 0;
        else
	    g_monitor_module.maxCount = g_monitor_module.instantDefOverrideTTL/g_monitor_module.instantPollPeriod;

	g_monitor_module.count = 0;
	wifi_dbg_print(0, "%s:%d: count:%d, maxCount:%d, TTL:%d, poll:%d\n",__func__, __LINE__, 
			g_monitor_module.count, g_monitor_module.maxCount, g_monitor_module.instantDefOverrideTTL, g_monitor_module.instantPollPeriod);

        //Enable Radio channel Stats
        wifi_setRadioStatsEnable(ap_index, 1);
    //Stopping telemetry while running instant measurement.
    scheduler_telemetry_tasks();
    if (g_monitor_module.instantPollPeriod != 0) {
        scheduler_add_timer_task(g_monitor_module.sched, TRUE, &g_monitor_module.inst_msmt_id,
                                    process_instant_msmt_monitor, NULL, (g_monitor_module.instantPollPeriod*1000), 0);
    }
}

/* This function process the active measurement step info
   from the active_msmt_monitor thread and calls wifiblaster. 
*/

void process_active_msmt_step()
{
    pthread_attr_t attr;
    pthread_attr_t *attrp = NULL;
    pthread_t id;

    attrp = &attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
    if (pthread_create(&id, attrp, WiFiBlastClient, NULL) != 0) {
       CcspTraceError(("%s:%d: Fail to spawn 'WiFiBlastClient' thread errno: %d - %s\n", __FUNCTION__, __LINE__, errno, strerror(errno)));
       if(attrp != NULL) {
           pthread_attr_destroy( attrp );
       }
    }
    else {
       CcspWifiTrace(("RDK_LOG_DEBUG, %s:%d: Sucessfully created thread for starting blast\n", __FUNCTION__, __LINE__));
    }

    if(attrp != NULL)
    {
        pthread_attr_destroy( attrp );
    }
    wifi_dbg_print(0, "%s:%d: exiting this function\n",__func__, __LINE__); 
    return;
}

void process_instant_msmt_stop	(unsigned int ap_index, instant_msmt_t *msmt)
{
	/*if ((g_monitor_module.inst_msmt.active == true) && (memcmp(g_monitor_module.inst_msmt.sta_mac, msmt->sta_mac, sizeof(mac_address_t)) == 0)) {		
		g_monitor_module.inst_msmt.active = false;
		g_monitor_module.poll_period = DEFAULT_INSTANT_POLL_TIME;
		g_monitor_module.maxCount = g_monitor_module.instantDefReportPeriod/DEFAULT_INSTANT_POLL_TIME;
		g_monitor_module.count = 0;
	}*/
        UNREFERENCED_PARAMETER(msmt);
        g_monitor_module.inst_msmt.active = false;
        g_monitor_module.poll_period = DEFAULT_INSTANT_POLL_TIME;
        g_monitor_module.maxCount = 0;
        g_monitor_module.count = 0;

        //Disable Radio channel Stats
        wifi_setRadioStatsEnable(ap_index, 0);
    //Restarting telemetry after stopping instant measurement.  
    scheduler_telemetry_tasks();
    if (g_monitor_module.inst_msmt_id != 0) {
        scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.inst_msmt_id);
        g_monitor_module.inst_msmt_id = 0;
    }
}

int process_instant_msmt_monitor(void *arg)
{
    if (g_monitor_module.count >= g_monitor_module.maxCount) {
        wifi_dbg_print(1, "%s:%d: instant polling freq reached threshold\n", __func__, __LINE__);
        g_monitor_module.instantDefOverrideTTL = DEFAULT_INSTANT_REPORT_TIME;
        g_monitor_module.instntMsmtenable = false;
        process_instant_msmt_stop(g_monitor_module.inst_msmt.ap_index, &g_monitor_module.inst_msmt);
    } else {
        g_monitor_module.count += 1;
        wifi_dbg_print(1, "%s:%d: client %s on ap %d\n", __func__, __LINE__, g_monitor_module.instantMac, g_monitor_module.inst_msmt.ap_index);
        associated_client_diagnostics(); //for single client
        stream_client_msmt_data(false);
    }

    return TIMER_TASK_COMPLETE;
}

void *monitor_function  (void *data)
{
	wifi_monitor_t *proc_data;
	struct timeval tv_now;
	struct timespec time_to_wait;
	wifi_monitor_data_t	*queue_data = NULL;
	int rc;
	struct timeval t_start;
	struct timeval interval;
	struct timeval timeout;
	timerclear(&t_start);
	//temp variable to pass the struct member's address to functions
	void* pQueueDataUValue   = NULL;
	void* pLastSignalledTime = NULL;

#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
        bool speedtest_started = 0;
#endif

	proc_data = (wifi_monitor_t *)data;

	pthread_mutex_lock(&proc_data->lock);
	while (proc_data->exit_monitor == false) {
	    gettimeofday(&tv_now, NULL);

#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
            /* if speedtest started, update the interval as speedtest timeout to pause the wifi stats */
            if (speedtest_started == 1) {
                interval.tv_sec = speedtest_timeout;
                interval.tv_usec = 0;
                wifi_dbg_print(0, "%s:%d: interval updated as [%ld] [%ld] start time updated as [%ld] [%ld]\n", 
                               __func__, __LINE__, interval.tv_sec,  interval.tv_usec, t_start.tv_sec, t_start.tv_usec);
            }
            else {
                interval.tv_sec = 0;
                interval.tv_usec = MONITOR_RUNNING_INTERVAL_IN_MILLISEC * 1000;
            }
#else
	    interval.tv_sec = 0;
            interval.tv_usec = MONITOR_RUNNING_INTERVAL_IN_MILLISEC * 1000;

#endif

            timeradd(&t_start, &interval, &timeout);

            time_to_wait.tv_sec = timeout.tv_sec;
            time_to_wait.tv_nsec = timeout.tv_usec*1000;
            rc = pthread_cond_timedwait(&proc_data->cond, &proc_data->lock, &time_to_wait);

		if (rc == 0 || queue_count(proc_data->queue) != 0) {
			// dequeue data
			while (queue_count(proc_data->queue)) {
				queue_data = queue_pop(proc_data->queue);
				if (queue_data == NULL) {
					continue;
				}

#if defined (FEATURE_CSI)
                //Send data to wifi_events library
                events_publish(*queue_data);
#endif
				switch (queue_data->event_type) {
					case monitor_event_type_diagnostics:
						//process_diagnostics(queue_data->ap_index, &queue_data->.devs);
						break;

					case monitor_event_type_connect:
						pQueueDataUValue = &queue_data->u.dev;
						process_connect(queue_data->ap_index, pQueueDataUValue);
						break;

					case monitor_event_type_disconnect:
						#if defined(FEATURE_HOSTAP_AUTHENTICATOR)
						isLibHostapDisAssocEvent = TRUE;
						#endif /* FEATURE_HOSTAP_AUTHENTICATOR */
						pQueueDataUValue = &queue_data->u.dev;
						process_disconnect(queue_data->ap_index, pQueueDataUValue);
						break;

					case monitor_event_type_deauthenticate:
						pQueueDataUValue = &queue_data->u.dev;
						process_deauthenticate(queue_data->ap_index, pQueueDataUValue);
						break;

					case monitor_event_type_stop_inst_msmt:
						pQueueDataUValue = &queue_data->u.imsmt;
						process_instant_msmt_stop(queue_data->ap_index, pQueueDataUValue);
						break;

					case monitor_event_type_start_inst_msmt:
						pQueueDataUValue = &queue_data->u.imsmt;
						process_instant_msmt_start(queue_data->ap_index, pQueueDataUValue);
						break;

					case monitor_event_type_StatsFlagChange:
						pQueueDataUValue = &queue_data->u.flag;
						process_stats_flag_changed(queue_data->ap_index, pQueueDataUValue);
						break;
					case monitor_event_type_RadioStatsFlagChange:
						pQueueDataUValue = &queue_data->u.flag;
						radio_stats_flag_changed(queue_data->ap_index, pQueueDataUValue);
						break;
					case monitor_event_type_VapStatsFlagChange:
						pQueueDataUValue = &queue_data->u.flag;
						vap_stats_flag_changed(queue_data->ap_index, pQueueDataUValue);
						break;
					case monitor_event_type_process_active_msmt:
						if (proc_data->blastReqInQueueCount == 1)
						{
							wifi_dbg_print(0, "%s:%d: calling process_active_msmt_step \n",__func__, __LINE__);
							CcspWifiTrace(("RDK_LOG_INFO, %s-%d calling process_active_msmt_step\n", __FUNCTION__, __LINE__));
							process_active_msmt_step();
						}
						else
						{
							wifi_dbg_print(0, "%s:%d: skipping old request as blastReqInQueueCount is %d \n",__func__, __LINE__,proc_data->blastReqInQueueCount);
							CcspWifiTrace(("RDK_LOG_INFO, %s-%d skipping old request as blastReqInQueueCount is %d\n", __FUNCTION__, __LINE__, proc_data->blastReqInQueueCount));
							proc_data->blastReqInQueueCount--;
						}
                                                break;
#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
                                        case monitor_event_type_speedtest_started:
                                            gettimeofday(&t_start, NULL);
                                            speedtest_started = 1;
                                            wifi_dbg_print(1, "%s:%d: Disabling wifi stats measurement\n", __func__, __LINE__);
                                            break;

                                        case monitor_event_type_speedtest_completed:
                                            if (speedtest_started != 0) {
                                                /* wifi stats enabled from this part only when speedtest completed before
                                                   Unpausetimeout expires */
                                                wifi_dbg_print(1, "%s:%d: Enabling wifi stats measurement\n", __func__, __LINE__);
                                                speedtest_started = 0;
                                            }
                                            break;

                                        case monitor_event_type_speedtest_timeout:
                                            speedtest_timeout = queue_data->u.flag.type;
                                            break;
#endif
#if defined (FEATURE_CSI)
                    case monitor_event_type_csi_update_config:
                        csi_sheduler_enable();
                        break;
                    case monitor_event_type_clientdiag_update_config:
                        clientdiag_sheduler_enable(queue_data->ap_index);
                        break;
#endif
                    default:
                        break;

				}

				free(queue_data);

                pLastSignalledTime = &proc_data->last_signalled_time;
                gettimeofday(pLastSignalledTime, NULL);
			}
		} else if (rc == ETIMEDOUT) {
#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
  			if (speedtest_started == 1) {
                          /* wifi stats enabled from here when unpausetimeout expires before speedtest completion */
                          wifi_dbg_print(1, "%s:%d: Enabling wifi stats measurement\n", __func__, __LINE__);
                          t2_event_d("IMP_WiFi_SubscriberUnPauseTimeOut", 1);
                          speedtest_started = 0;
                      }
#endif
                      gettimeofday(&t_start, NULL);
                      scheduler_execute(g_monitor_module.sched, t_start, interval.tv_usec/1000);
		} else {
			wifi_dbg_print(1,"%s:%d Monitor Thread exited with rc - %d",__func__,__LINE__,rc);
            pthread_mutex_unlock(&proc_data->lock);
			return NULL;
		}
    }
    pthread_mutex_unlock(&proc_data->lock);

    return NULL;
}

static int refresh_task_period(void *arg)
{
    unsigned int    new_upload_period;
    new_upload_period = get_upload_period(g_monitor_module.upload_period);

    if (new_upload_period != g_monitor_module.upload_period) {
        g_monitor_module.upload_period = new_upload_period;
        if (new_upload_period != 0) {
            if (g_monitor_module.client_telemetry_id == 0) {
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.client_telemetry_id,
                        upload_client_telemetry_data, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
            } else {
                scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.client_telemetry_id,
                        (g_monitor_module.upload_period * MIN_TO_MILLISEC));
            }
            if (g_monitor_module.client_debug_id == 0 ) {
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.client_debug_id,
                        upload_client_debug_stats, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
            } else {
                scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.client_debug_id,
                        (g_monitor_module.upload_period * MIN_TO_MILLISEC));
            }
            if (g_monitor_module.channel_width_telemetry_id == 0) {
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.channel_width_telemetry_id,
                        upload_channel_width_telemetry, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
            } else {
                scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.channel_width_telemetry_id,
                        (g_monitor_module.upload_period * MIN_TO_MILLISEC));
            }
            if (g_monitor_module.vap_max_client_id == 0) {
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.vap_max_client_id,
                        upload_vap_rejection_sta_count_telemetry, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
            } else {
                scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.vap_max_client_id,
                        (g_monitor_module.upload_period * MIN_TO_MILLISEC));
            }

            if (g_monitor_module.ap_telemetry_id == 0) {
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.ap_telemetry_id,
                        upload_ap_telemetry_data, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
            } else {
                scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.ap_telemetry_id,
                        (g_monitor_module.upload_period * MIN_TO_MILLISEC));
            }
        } else {
            if (g_monitor_module.client_telemetry_id != 0) {
                scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.client_telemetry_id);
                g_monitor_module.client_telemetry_id = 0;
            }
            if (g_monitor_module.client_debug_id != 0) {
                scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.client_debug_id);
                g_monitor_module.client_debug_id = 0;

            }
            if (g_monitor_module.channel_width_telemetry_id != 0 ) {
                scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.channel_width_telemetry_id);
                g_monitor_module.channel_width_telemetry_id = 0;
            }
            if (g_monitor_module.ap_telemetry_id != 0) {
                scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.ap_telemetry_id);
                g_monitor_module.ap_telemetry_id = 0;
            }
            if (g_monitor_module.vap_max_client_id != 0) {
                scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.vap_max_client_id);
                g_monitor_module.vap_max_client_id = 0;
            }
        }
    }
    return TIMER_TASK_COMPLETE;
}

bool is_device_associated(int ap_index, char *mac)
{
    mac_address_t bmac;
    sta_data_t *sta;
    hash_map_t     *sta_map;

    if (ap_index < 0)
    {
        wifi_dbg_print (1, "(%s) invalid apIndex=%d\n", __func__, ap_index);
        return false;
    }

    to_mac_bytes(mac, bmac);
    sta_map = g_monitor_module.bssid_data[ap_index].sta_map;
    if (sta_map == NULL)
        return false;
    sta = hash_map_get_first(sta_map);
    while (sta != NULL) {
        if ((memcmp(sta->sta_mac, bmac, sizeof(mac_address_t)) == 0) && (sta->dev_stats.cli_Active == true)) {
            return true;
        }

        sta = hash_map_get_next(sta_map, sta);
    }
    return false;
}

#if defined (FEATURE_CSI)

int
timeval_subtract (struct timeval *result, struct timeval *end, struct timeval *start)
{
    if(result == NULL || end == NULL || start == NULL) {
        return 1;
    }

  /* Refer to https://www.gnu.org/software/libc/manual/html_node/Calculating-Elapsed-Time.html" */  

    if (end->tv_usec < start->tv_usec) {
        int adjust_sec = (start->tv_usec - end->tv_usec) / 1000000 + 1;
        start->tv_usec -= 1000000 * adjust_sec;
        start->tv_sec += adjust_sec;
    }
    if (end->tv_usec - start->tv_usec > 1000000) {
        int adjust_sec = (end->tv_usec - start->tv_usec) / 1000000;
        start->tv_usec += 1000000 * adjust_sec;
        start->tv_sec -= adjust_sec;
    }


    result->tv_sec = end->tv_sec - start->tv_sec;
    result->tv_usec = end->tv_usec - start->tv_usec;

    return (end->tv_sec < start->tv_sec);
}

int  getApIndexfromClientMac(char *check_mac)
{
    char *cmp_tok=NULL;
    char tmpassoc[2048];
    unsigned int i=0;
    int ret;
    unsigned char tmpmac[18];
    char* rest = NULL;

    if(check_mac == NULL) {
        wifi_dbg_print(1, "%s: Null arguments %p \n",__func__, check_mac);
        return -1;
    }

    macbytes_to_string((unsigned char *)check_mac, tmpmac);
    for (i = 0; i < MAX_VAP; i++) {
        memset(tmpassoc, 0, sizeof(tmpassoc));
        ret = wifi_getApAssociatedDevice(i, tmpassoc, sizeof(tmpassoc));
        rest = tmpassoc;
        if(ret == RETURN_OK)
        {
            while((cmp_tok = strtok_r(rest, ",", &rest)))
            {
                if(!strcasecmp((const char *)tmpmac,cmp_tok))
                {
                    return i;
                }
            }
        }
    }
    return -1;
}

static void rtattr_parse(struct rtattr *table[], int max, struct rtattr *rta, int len)
{
    unsigned short type;
    if(table == NULL || rta == NULL) {
        wifi_dbg_print(1, "%s: Null arguments %p %p\n",__func__,table, rta);
        return;
    }
    memset(table, 0, sizeof(struct rtattr *) * (max + 1));
    while (RTA_OK(rta, len)) {
        type = rta->rta_type;
        if (type <= max)
            table[type] = rta;
        rta = RTA_NEXT(rta, len);
    }
}

int getlocalIPAddress(char *ifname, char *ip, bool af_family)
{
    struct {
        struct nlmsghdr n;
        struct ifaddrmsg r;
    } req;

    int status;
    char buf[16384];
    struct nlmsghdr *nlm;
    struct ifaddrmsg *rtmp;
    unsigned char family;

    struct rtattr * table[__IFA_MAX+1];
    int fd;
    char if_name[IFNAMSIZ] = {'\0'};

    if(ifname == NULL || ip == NULL) {
        wifi_dbg_print(1, "%s: Null arguments %p %p\n",__func__, ifname, ip);
        return -1;
    }

    fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (fd < 0 ) {
        wifi_dbg_print(1, "Socket error\n");
        return -1;
    }

    req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_REQUEST;
    req.n.nlmsg_type = RTM_GETADDR;
    if(af_family)
    {
        req.r.ifa_family = AF_INET;
        family = AF_INET;
    }
    else
    {
        req.r.ifa_family = AF_INET6;
        family = AF_INET6;
    }
    status = send(fd, &req, req.n.nlmsg_len, 0);
    if(status<0) {
        wifi_dbg_print(1, "Send error\n");
        close(fd);
        return -1;
    }

    status = recv(fd, buf, sizeof(buf), 0);
    if(status<0) {
        wifi_dbg_print(1, "receive error\n");
        close(fd);
        return -1;
    }

    for(nlm = (struct nlmsghdr *)buf; status > (int)sizeof(*nlm);){
        int len = nlm->nlmsg_len;
        int req_len = len - sizeof(*nlm);

        if (req_len<0 || len>status || !NLMSG_OK(nlm, status))
        {
            wifi_dbg_print(1, "length error\n");
            close(fd);
            return -1;
        }
        rtmp = (struct ifaddrmsg *)NLMSG_DATA(nlm);
        rtattr_parse(table, IFA_MAX, IFA_RTA(rtmp), nlm->nlmsg_len - NLMSG_LENGTH(sizeof(*rtmp)));
        if(rtmp->ifa_index)
        {
            if_indextoname(rtmp->ifa_index, if_name);
            if(!strcasecmp(ifname, if_name) && table[IFA_ADDRESS])
            {
                inet_ntop(family, RTA_DATA(table[IFA_ADDRESS]), ip, 64);
                close(fd);
                return 0;
            }
        }
        status -= NLMSG_ALIGN(len);
        nlm = (struct nlmsghdr*)((char*)nlm + NLMSG_ALIGN(len));
    }
    close(fd);
    return -1;
}

int csi_getInterfaceAddress(unsigned char *tmpmac, char *ip, char *interface, bool *af_family)
{
    int ret;
    unsigned char mac[18];

    if(tmpmac == NULL || ip == NULL || interface == NULL || af_family == NULL) {
        wifi_dbg_print(1, "%s: Null arguments %p %p %p %p\n",__func__, tmpmac, ip, interface, af_family);
        return -1;
    }
    macbytes_to_string((unsigned char*)tmpmac, mac);
    ret = csi_getClientIpAddress(mac, ip, interface, 1);
    if(ret < 0 )
    {
        wifi_dbg_print(1, "Not able to find v4 address\n");
    }
    else
    {
        *af_family = TRUE;
        return 0;
    }
    ret = csi_getClientIpAddress(mac, ip, interface, 0);
    if(ret < 0)
    {
        *af_family = FALSE;
        wifi_dbg_print(1, "Not able to find v4 or v6 addresses\n");
        return -1;
    }
    return 0;
}

int csi_getClientIpAddress(char *mac, char *ip, char *interface, int check)
{
    struct {
        struct nlmsghdr n;
        struct ndmsg r;
    } req;

    int status;
    char buf[16384];
    struct nlmsghdr *nlm;
    struct ndmsg *rtmp;
    struct rtattr * table[NDA_MAX+1];
    int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    char if_name[IFNAMSIZ] = {'\0'};
    unsigned char tmp_mac[17];
    unsigned char af_family;

    if(mac == NULL || ip == NULL || interface == NULL) {
        wifi_dbg_print(1, "%s: Null arguments %p %p %p\n",__func__, mac, ip, interface);
        return -1;
    }
    if (fd < 0 ) {
        wifi_dbg_print(1, "Socket error\n");
        return -1;
    }
    req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ndmsg));
    req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_REQUEST;
    req.n.nlmsg_type = RTM_GETNEIGH;
    if(check)
    {
        req.r.ndm_family = AF_INET;
        af_family =  AF_INET;
    }
    else
    {
        req.r.ndm_family = AF_INET6;
        af_family = AF_INET6;
    }

    status = send(fd, &req, req.n.nlmsg_len, 0);
    if (status < 0) {
        wifi_dbg_print(1, "Socket send error\n");
        close(fd);
        return -1;
    }

    status = recv(fd, buf, sizeof(buf), 0);
    if (status < 0) {
        wifi_dbg_print(1, "Socket receive error\n");
        close(fd);
        return -1;
    }

    for(nlm = (struct nlmsghdr *)buf; status > (int)sizeof(*nlm);){
        int len = nlm->nlmsg_len;
        int req_len = len - sizeof(*nlm);

        if (req_len<0 || len>status || !NLMSG_OK(nlm, status)) {
            wifi_dbg_print(1, "packet length error\n");
            close(fd);
            return -1;
        }

        rtmp = (struct ndmsg *)NLMSG_DATA(nlm);
        rtattr_parse(table, NDA_MAX, NDA_RTA(rtmp), nlm->nlmsg_len - NLMSG_LENGTH(sizeof(*rtmp)));

        if(rtmp->ndm_state & NUD_REACHABLE || rtmp->ndm_state & NUD_STALE)
        {
            if(table[NDA_LLADDR])
            {
                unsigned char *addr =  RTA_DATA(table[NDA_LLADDR]);
                macbytes_to_string(addr,tmp_mac);
                if(!strcasecmp((char *)tmp_mac, mac))
                {
                    if(table[NDA_DST] && rtmp->ndm_ifindex) {
                        inet_ntop(af_family, RTA_DATA(table[NDA_DST]), ip, 64);
                        if_indextoname(rtmp->ndm_ifindex, if_name);
                        strncpy(interface, if_name, IFNAMSIZ);
                        close(fd);
                        return 0;
                    }
                }
            }
        }
        status -= NLMSG_ALIGN(len);
        nlm = (struct nlmsghdr*)((char*)nlm + NLMSG_ALIGN(len));
   }
   close(fd);
   return -1;
}

unsigned short csum(unsigned short *ptr,int nbytes)
{
    register long sum;
    unsigned short oddbyte;
    register short answer;

    if(ptr == NULL) {
        wifi_dbg_print(1, "%s: Null arguments %p\n",__func__, ptr);
        return 0;
    }

    sum=0;
    while(nbytes>1) {
        sum+=*ptr++;
        nbytes-=2;
    }
    if(nbytes==1) {
        oddbyte=0;
        *((u_char*)&oddbyte)=*(u_char*)ptr;
        sum+=oddbyte;
    }

    sum = (sum>>16)+(sum & 0xffff);
    sum = sum + (sum>>16);
    answer=(short)~sum;

    return(answer);
}

int frame_icmpv4_ping(char *buffer, char *dest_ip, char *source_ip)
{
    char *data;
    int buffer_size;
    //ip header
    struct iphdr *ip = (struct iphdr *) buffer;
    static int pingCount = 1;
    //ICMP header
    struct icmphdr *icmp = (struct icmphdr *) (buffer + sizeof (struct iphdr));
    if(buffer == NULL || dest_ip == NULL || source_ip == NULL) {
        wifi_dbg_print(1, "%s: Null arguments %p %p %p\n",__func__, buffer, dest_ip, source_ip);
        return 0;
    }
    data = buffer + sizeof(struct iphdr) + sizeof(struct icmphdr);
    strcpy(data , "stats ping");
    buffer_size = sizeof (struct iphdr) + sizeof (struct icmphdr) + strlen(data);


    //ICMP_HEADER
    //
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = (unsigned short) getpid();
    icmp->un.echo.sequence = pingCount++;
    icmp->checksum = csum ((unsigned short *) (icmp), sizeof (struct icmphdr) + strlen(data));

    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->tot_len = htons (sizeof (struct iphdr) + sizeof (struct icmphdr) + strlen(data));
    ip->ttl = 8;
    ip->frag_off = 0;
    ip->protocol = IPPROTO_ICMP;
    ip->saddr = inet_addr (source_ip);
    ip->daddr = inet_addr (dest_ip);
    ip->check = 0;
    ip->id = htonl (54321);
    ip->check = csum ((unsigned short *) (ip), sizeof(struct iphdr));

    return buffer_size;
}

int frame_icmpv6_ping(char *buffer, char *dest_ip, char *source_ip)
{
    char *data;
    int buffer_size;
    struct ip6_hdr* ip  = (struct ip6_hdr*) buffer;
    struct icmp6_hdr* icmp = (struct icmp6_hdr*)(buffer + sizeof(struct ip6_hdr));

    //v6 pseudo header for icmp6 checksum 
    struct ip6_pseu
    {
        struct in6_addr ip6e_src;
        struct in6_addr ip6e_dst;
        uint16_t ip6e_len;
        uint8_t  pad;
        uint8_t  ip6e_nxt;
    };
    char sample[1024] = {0};
    struct ip6_pseu* pseu = (struct ip6_pseu*)sample;

    data = (char *)(buffer + sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr));
    strcpy(data, "stats ping");
    buffer_size = sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr) + strlen(data);
    icmp->icmp6_type = ICMP6_ECHO_REQUEST;
    icmp->icmp6_code = 0;

    pseu->pad = 0x00;
    pseu->ip6e_nxt = IPPROTO_ICMPV6;

    ip->ip6_flow = htonl ((6 << 28) | (0 << 20) | 0);
    ip->ip6_plen = htons(sizeof(struct icmp6_hdr)+strlen(data));
    ip->ip6_nxt = IPPROTO_ICMPV6;
    ip->ip6_hlim = 255;

    pseu->ip6e_len = htons(sizeof(struct icmp6_hdr)+strlen(data));

    inet_pton(AF_INET6, source_ip, &(ip->ip6_src));
    inet_pton(AF_INET6, dest_ip, &(ip->ip6_dst));
    pseu->ip6e_src = ip->ip6_src;
    pseu->ip6e_dst = ip->ip6_dst;

    memcpy(sample+sizeof(struct ip6_pseu), icmp, sizeof(struct icmp6_hdr)+strlen(data));
    icmp->icmp6_cksum = 0;
    icmp->icmp6_cksum = csum ((unsigned short* )sample, sizeof(struct ip6_pseu)+sizeof(struct icmp6_hdr)+strlen(data));

    return buffer_size;
}

static bool isValidIpAddress(char *ipAddress, bool af_family)
{
    struct sockaddr_in sa;
    unsigned char family;
    if(ipAddress==NULL)
    {
        return FALSE;
    }
    if(af_family)
    {
        family = AF_INET;
    }
    else
    {
        family = AF_INET6;
    }
    int result = inet_pton(family, ipAddress, &(sa.sin_addr));
    return (result == 1);
}


static void send_ping_data(int ap_idx, unsigned char *mac, char *client_ip, char *vap_ip, long *client_ip_age, bool refresh)
{

    char        cli_interface_str[16];
    char        buffer[1024] = {0};
    int         frame_len;
    int rc = 0;
    bool af_family = TRUE;
    char        src_ip_str[IP_STR_LEN];
    char        cli_ip_str[IP_STR_LEN];

    if(mac == NULL ) {
        wifi_dbg_print(1, "%s: Mac is NULL\n",__func__);
        return;
    }

    memset (buffer, 0, sizeof(buffer));
    frame_len = 0;
    if(ap_idx < 0 || mac == NULL) {
        return;
    }
    wifi_dbg_print(1, "%s: Got the csi client for index  %02x..%02x\n",__func__,mac[0], mac[5]);
    if(refresh) {
        //Find the interface through which this client was seen
        rc = csi_getInterfaceAddress(mac, cli_ip_str, cli_interface_str, &af_family); //pass mac_addr_t
        if(rc<0)
        {
            wifi_dbg_print(1, "%s Failed to get ipv4 client address\n",__func__);
            return;
        }
        else
        {
            if(isValidIpAddress(cli_ip_str, af_family)) {
                *client_ip_age = 0;
                strncpy(client_ip, cli_ip_str, IP_STR_LEN);
                wifi_dbg_print(1, "%s Returned ipv4 client address is %s interface %s \n",__func__,  cli_ip_str, cli_interface_str );
            }
            else
            {
                wifi_dbg_print(1, "%s Was not a valid client ip string\n", __func__);
                return;
            }
        }
        //Get the ip address of the interface
        if(*vap_ip == '\0') {
            rc = getlocalIPAddress(cli_interface_str, src_ip_str, af_family);
            if(rc<0)
            {
                wifi_dbg_print(1, "%s Failed to get ipv4 address\n",__func__);
                return;
            }
            else
            {
                if(isValidIpAddress(src_ip_str, af_family)) {
                    strncpy(vap_ip, src_ip_str, IP_STR_LEN);
                    wifi_dbg_print(1, "%s Returned interface ip addr is %s\n", __func__,src_ip_str);
                }
                else
                {
                    wifi_dbg_print(1, "%s Was not a valid client ip string\n", __func__);
                    return;
                }
            }
        }
    }
    else
    {
        strncpy(src_ip_str, vap_ip, IP_STR_LEN);
        strncpy(cli_ip_str, client_ip, IP_STR_LEN);
    }
    //build a layer 3 packet , tcp ping
    if(af_family)
    {
        frame_len = frame_icmpv4_ping(buffer, (char *)&cli_ip_str, (char *)&src_ip_str);
        //send buffer
        if(frame_len) {
           wifi_sendDataFrame(ap_idx,
                    (unsigned char*)mac,
                    (unsigned char*)buffer,
                    frame_len,
                    TRUE,
                    WIFI_ETH_TYPE_IP,
                    wifi_data_priority_be);
        }
    }
    else{
        frame_len = frame_icmpv6_ping(buffer, (char *)&cli_ip_str, (char *)&src_ip_str);
        //send buffer
        if(frame_len) {
            wifi_sendDataFrame(ap_idx,
                    (unsigned char*)mac,
                    (unsigned char*)buffer,
                    frame_len,
                    TRUE,
                    WIFI_ETH_TYPE_IP6,
                    wifi_data_priority_be);
        }
    }
    wifi_dbg_print(1,"%s: Exit from \n",__func__);
}


static csi_session_t* csi_get_session(bool create, int csi_session_number) {
    csi_session_t *csi = NULL;
    int count = 0, i = 0;
    void* pLastSnapshotTime = NULL;

    count = queue_count(g_events_monitor.csi_queue);
    for(i = 0; i<count; i++) {
        csi = queue_peek(g_events_monitor.csi_queue, i);
        if(csi == NULL){
            continue;
        }
        if(csi->csi_sess_number == csi_session_number) {
            return csi;
        } 
    }

    if(create == FALSE) {
        return NULL;
    }
    
    csi = (csi_session_t *) malloc(sizeof(csi_session_t));
    if(csi == NULL) {
        return NULL;
    }

    memset(csi, 0, sizeof(csi_session_t));
    csi->csi_time_interval = MIN_CSI_INTERVAL;
    csi->csi_sess_number = csi_session_number;
    csi->enable = FALSE;
    csi->subscribed = FALSE;
    memset(csi->client_ip, '\0', sizeof(csi->client_ip));
    pLastSnapshotTime = &csi->last_snapshot_time;
    gettimeofday(pLastSnapshotTime, NULL);
    
    queue_push(g_events_monitor.csi_queue, csi);
    return csi;
}


void csi_update_client_mac_status(mac_addr_t mac, bool connected, int ap_idx) {
    csi_session_t *csi = NULL;
    int count = 0;
    int i = 0, j = 0;
    bool client_csi_monitored = FALSE;
    pthread_mutex_lock(&g_events_monitor.lock);
    count = queue_count(g_events_monitor.csi_queue);
    wifi_dbg_print(1, "%s: Received Mac %d %d %02x %02x\n",__func__, connected, count, mac[0], mac[5]);
    for(i = 0; i<count; i++) {
        csi = queue_peek(g_events_monitor.csi_queue, i);
        if(csi == NULL){
            continue;
        }

        wifi_dbg_print(1, "%s: Received Mac  %d %d %d %02x %02x\n",__func__, connected, csi->subscribed, csi->enable, mac[0], mac[5]);
        for(j =0 ;j < csi->no_of_mac; j++) {
            wifi_dbg_print(1, "%s: checking with Mac  %d %d %d %02x %02x\n",__func__, connected, csi->subscribed, csi->enable, csi->mac_list[j][0], csi->mac_list[j][5]);
            if(memcmp(mac, csi->mac_list[j], sizeof(mac_addr_t)) == 0) {
                csi->mac_is_connected[j] = connected;
                if(csi->enable && csi->subscribed) {
                    client_csi_monitored = TRUE;
                }
                if(connected == FALSE) {
                    csi->ap_index[j] = -1;
                    memset(&csi->client_ip[j][0], '\0', IP_STR_LEN);
                    csi->client_ip_age[j] = 0;
                }
                else {
                    csi->ap_index[j] = ap_idx;
                }
                break;
            }
        }
    }
    if(client_csi_monitored) {
        wifi_dbg_print(1, "%s: Updating csi collection for Mac %02x %02x %d\n",__func__, mac[0], mac[5], connected);
        wifi_enableCSIEngine(ap_idx, (unsigned char*)mac, connected);
    }
    pthread_mutex_unlock(&g_events_monitor.lock);
}

void csi_set_client_mac(char *r_mac_list, int csi_session_number) {
    
    csi_session_t *csi = NULL;
    int ap_index = -1, mac_ctr = 0;
    int i = 0;
    char *mac_tok=NULL;
    char* rest = NULL;
    char mac_list[COSA_DML_CSI_CLIENTMACLIST] = {0};
    wifi_monitor_data_t *data;

    if(r_mac_list == NULL) {
        wifi_dbg_print(1, "%s: mac_list is NULL \n",__func__);
        return;
    }

    pthread_mutex_lock(&g_events_monitor.lock);
    csi = csi_get_session(FALSE, csi_session_number);
    if(!csi) {
        wifi_dbg_print(1, "%s: csi session not present \n",__func__);
        pthread_mutex_unlock(&g_events_monitor.lock);
        return;
    }
    if(csi->no_of_mac > 0) {
        wifi_dbg_print(1, "%s: Mac already configured %d\n",__func__, csi->no_of_mac);
        csi_disable_client(csi);
        for(i = 0; i<csi->no_of_mac; i++) {
            csi->mac_is_connected[i] = FALSE;
            csi->ap_index[mac_ctr] = -1;
            memset(&csi->mac_list[i], 0, sizeof(mac_addr_t));
        }
        csi->no_of_mac = 0;
    }

    strncpy(mac_list, r_mac_list, COSA_DML_CSI_CLIENTMACLIST);
    csi->no_of_mac = (strlen(mac_list)+1) / (MIN_MAC_LEN + 1);
    wifi_dbg_print(1, "%s: Total mac's present -  %d %s\n",__func__, csi->no_of_mac, mac_list);
    rest = mac_list;
    if(csi->no_of_mac > 0)  {
        while((mac_tok = strtok_r(rest, ",", &rest))) {
            
            wifi_dbg_print(1, "%s: Mac %s\n",__func__, mac_tok);
            to_mac_bytes(mac_tok,(unsigned char*)&csi->mac_list[mac_ctr]);    
            ap_index= getApIndexfromClientMac((char *)&csi->mac_list[mac_ctr]);
            if(ap_index >= 0) {
                csi->ap_index[mac_ctr] = ap_index;
                csi->mac_is_connected[mac_ctr] = TRUE;
                if(csi->enable && csi->subscribed) {
                    wifi_dbg_print(1, "%s: Enabling csi collection for Mac %s\n",__func__, mac_tok);
                    wifi_enableCSIEngine(ap_index, csi->mac_list[mac_ctr], TRUE);
                }
            }
            else {
                wifi_dbg_print(1, "%s: Not Enabling csi collection for Mac %s\n",__func__, mac_tok);
                csi->ap_index[mac_ctr] = -1;
                csi->mac_is_connected[mac_ctr] = FALSE;
            }
            mac_ctr++;
        }
    }
    pthread_mutex_unlock(&g_events_monitor.lock);

    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    if (data != NULL) {
        memset(data, 0, sizeof(wifi_monitor_data_t));
        data->id = msg_id++;
        data->event_type = monitor_event_type_csi_update_config;

        pthread_mutex_lock(&g_monitor_module.lock);
        queue_push(g_monitor_module.queue, data);

        pthread_cond_signal(&g_monitor_module.cond);
        pthread_mutex_unlock(&g_monitor_module.lock);
    }

}

static void csi_enable_client(csi_session_t *csi) {
    
    int i =0;
    int ap_index = -1; 
    if((csi == NULL) || !(csi->enable && csi->subscribed)) {
        return;
    }
    
    for(i =0; i<csi->no_of_mac; i++) {
        if((csi->ap_index[i] != -1) && (csi->mac_is_connected[i] == TRUE)) {
            wifi_dbg_print(1, "%s: Enabling csi collection for Mac %02x..%02x\n",__func__, csi->mac_list[i][0] , csi->mac_list[i][5]  );
            wifi_enableCSIEngine(csi->ap_index[i], csi->mac_list[i], TRUE);
        }
        //check if client is connected now
        else {
            ap_index= getApIndexfromClientMac((char *)&csi->mac_list[i]);
            if(ap_index >= 0) {
                csi->ap_index[i] = ap_index;
                csi->mac_is_connected[i] = TRUE;
                wifi_dbg_print(1, "%s: Enabling csi collection for Mac %02x..%02x\n",__func__, csi->mac_list[i][0] , csi->mac_list[i][5]  );
                wifi_enableCSIEngine(csi->ap_index[i], csi->mac_list[i], TRUE);
            } 
        }
    }
}

void csi_enable_session(bool enable, int csi_session_number) {
    csi_session_t *csi = NULL;
    wifi_monitor_data_t *data;

    pthread_mutex_lock(&g_events_monitor.lock);
    csi = csi_get_session(FALSE, csi_session_number);
    if(csi) {
        wifi_dbg_print(1, "%s: Enable session %d enable - %d\n",__func__, csi_session_number, enable);
        if(enable) {
            csi->enable = enable;
            csi_enable_client(csi);
        } 
        else {
            csi_disable_client(csi);
            csi->enable = enable;
        }
    }
    pthread_mutex_unlock(&g_events_monitor.lock);
    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    if (data != NULL) {
        memset(data, 0, sizeof(wifi_monitor_data_t));
        data->id = msg_id++;
        data->event_type = monitor_event_type_csi_update_config;

        pthread_mutex_lock(&g_monitor_module.lock);
        queue_push(g_monitor_module.queue, data);

        pthread_cond_signal(&g_monitor_module.cond);
        pthread_mutex_unlock(&g_monitor_module.lock);
    }
}

static void csi_vap_down_update(int ap_idx) {
    csi_session_t *csi = NULL;
    int count = 0, i = 0, j=0;

    pthread_mutex_lock(&g_events_monitor.lock);
    count = queue_count(g_events_monitor.csi_queue);
    for(i = 0; i<count; i++) {
        csi = queue_peek(g_events_monitor.csi_queue, i);
        if(csi == NULL){
            continue;
        }
        for(j =0; j<csi->no_of_mac; j++) {
           if(csi->ap_index[j] == ap_idx) {
                wifi_enableCSIEngine(csi->ap_index[j], csi->mac_list[j], FALSE);
                csi->ap_index[j] = -1;
                csi->mac_is_connected[j] = FALSE;
                memset(&csi->client_ip[j][0], '\0', IP_STR_LEN);
                csi->client_ip_age[j] = 0;
           }
        }
    }
    pthread_mutex_unlock(&g_events_monitor.lock);
}

void csi_enable_subscription(bool subscribe, int csi_session_number)
{
    csi_session_t *csi = NULL;
    wifi_monitor_data_t *data;

    pthread_mutex_lock(&g_events_monitor.lock);
    csi = csi_get_session(FALSE, csi_session_number);
    if(csi) {
        wifi_dbg_print(1, "%s: subscription for session %d\n",__func__, csi_session_number);
        if(subscribe) {
            csi->subscribed = subscribe;
            csi_enable_client(csi);
        }
        else {
            csi_disable_client(csi);
            csi->subscribed = subscribe;
        }
    }
    pthread_mutex_unlock(&g_events_monitor.lock);
    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    if (data != NULL) {
        memset(data, 0, sizeof(wifi_monitor_data_t));
        data->id = msg_id++;
        data->event_type = monitor_event_type_csi_update_config;

        pthread_mutex_lock(&g_monitor_module.lock);
        queue_push(g_monitor_module.queue, data);

        pthread_cond_signal(&g_monitor_module.cond);
        pthread_mutex_unlock(&g_monitor_module.lock);
    }
}

void csi_set_interval(int interval, int csi_session_number) {
    csi_session_t *csi = NULL;

    pthread_mutex_lock(&g_events_monitor.lock);
    csi = csi_get_session(FALSE, csi_session_number);
    if(csi) {
        csi->csi_time_interval = interval;
    }
    pthread_mutex_unlock(&g_events_monitor.lock);
}

void csi_create_session(int csi_session_number) {

    pthread_mutex_lock(&g_events_monitor.lock);
    csi_get_session(TRUE, csi_session_number);
    pthread_mutex_unlock(&g_events_monitor.lock);
}

static void csi_disable_client(csi_session_t *r_csi) {
    int count = 0;
    int i = 0, j = 0, k = 0;
    csi_session_t *csi = NULL;
    bool client_in_diff_subscriber = FALSE;

    if(r_csi == NULL) {
        wifi_dbg_print(1, "%s: r_csi is NULL\n",__func__);
        return;
    }

    count = queue_count(g_events_monitor.csi_queue);

    for(j =0 ; j< r_csi->no_of_mac; j++) {
        client_in_diff_subscriber = FALSE;
        for(i = 0; i<count; i++) {
            csi = queue_peek(g_events_monitor.csi_queue, i);

            if(csi == NULL || csi == r_csi){
                continue;
            }
            if(!(csi->enable && csi->subscribed)) {
                continue;
            }
            
            for(k = 0; k < csi->no_of_mac; k++) {
                if(memcmp(r_csi->mac_list[j], csi->mac_list[k], sizeof(mac_addr_t))== 0) {
                    //Client is also monitored by a different subscriber
                    wifi_dbg_print(1, "%s: Not Disabling csi for client mac %02x..%02x\n",__func__,r_csi->mac_list[j][0],r_csi->mac_list[j][5]);
                    client_in_diff_subscriber = TRUE;
                    break;
                }
            }
            if(client_in_diff_subscriber)
                break;
        }
        if((client_in_diff_subscriber == FALSE) && (r_csi->mac_is_connected[j] == TRUE)) {
            wifi_dbg_print(1, "%s: Disabling for client mac %02x..%02x\n",__func__,r_csi->mac_list[j][0],r_csi->mac_list[j][5]);
            wifi_enableCSIEngine(r_csi->ap_index[j], r_csi->mac_list[j], FALSE);
        }
    }
}

static int csi_timedout(struct timeval *time_diff, int *csi_time_interval) {

    if(time_diff == NULL || csi_time_interval == NULL) {
      return 0;
    }

    int time_compare = *csi_time_interval;
    if(time_compare >= 1000) {
        if(time_diff->tv_sec >= (time_compare / 1000)) {
            return 1;
        }else {
            return 0;
        }
    } else {
        if(((time_diff->tv_usec) >=  (time_compare - 5 ) * MILLISEC_TO_MICROSEC) || time_diff->tv_sec > 0) {
            return 1;
        } else {
            return 0;
        }
    }
    return 0;
}

void csi_del_session(int csi_sess_number) {
    int count = 0;
    int i = 0;
    csi_session_t *csi = NULL;

    pthread_mutex_lock(&g_events_monitor.lock);
    count = queue_count(g_events_monitor.csi_queue);
    wifi_dbg_print(1, "%s: Deleting Element %d\n",__func__, csi_sess_number);
    for(i = 0; i<count; i++) {
        csi = queue_peek(g_events_monitor.csi_queue, i);

        if(csi == NULL){
            continue;
        }

        if(csi->csi_sess_number == csi_sess_number) {
            wifi_dbg_print(1, "%s: Found Element\n",__func__);
            queue_remove(g_events_monitor.csi_queue, i);

            csi_disable_client(csi);
            free(csi);
            break;
        }
    }
    pthread_mutex_unlock(&g_events_monitor.lock);
}

static void csi_refresh_session () {
    int i = 0;
    int count = 0;
    struct timeval time_diff;
    csi_session_t *csi = NULL;
    void* pLastSnapshotTime = NULL;
    void* pCsiTimeInterval  = NULL;

    pthread_mutex_lock(&g_events_monitor.lock);

    count = queue_count(g_events_monitor.csi_queue);

    while(i < count) {
        csi = queue_peek(g_events_monitor.csi_queue, i);
        i++;
        if(csi == NULL || !(csi->enable && csi->subscribed)) {
            continue;
        }
        pLastSnapshotTime = &csi->last_snapshot_time;
        if(!timeval_subtract(&time_diff, &csi_prune_timer, pLastSnapshotTime)) {
            pCsiTimeInterval = &csi->csi_time_interval;
            if(csi_timedout(&time_diff, pCsiTimeInterval)) {
                    csi->last_snapshot_time = csi_prune_timer;
            }
        }
    }
    pthread_mutex_unlock(&g_events_monitor.lock);
}

static int csi_sheduler_enable(void)
{
    unsigned int *interval_list;
    int i, found = 0, count;
    unsigned int csi_time_interval = MAX_CSI_INTERVAL;
    bool enable = FALSE;
    csi_session_t *csi = NULL;

    count = queue_count(g_events_monitor.csi_queue);
    if (count > 0) {
        interval_list = (unsigned int *) malloc(sizeof(unsigned int)*count);
        if (interval_list == NULL) {
            return -1;
        }
        pthread_mutex_lock(&g_events_monitor.lock);
        for (i=0; i < count; i++) {
            interval_list[i] = MAX_CSI_INTERVAL;
            csi = queue_peek(g_events_monitor.csi_queue, i);
            if (csi != NULL && csi->enable && csi->subscribed) {
                enable = TRUE;
                //make sure it is multiple of 100ms
                interval_list[i] = (csi->csi_time_interval/100)*100;
                //find shorter time interval
                if(csi_time_interval > interval_list[i]) {
                    csi_time_interval = interval_list[i];
                }
            }
        }
        pthread_mutex_unlock(&g_events_monitor.lock);
        if (enable == TRUE) {
            while (found == 0) {
                int i;
                found = 1;
                for (i=0; i < count; i++) {
                    if ((interval_list[i] % csi_time_interval) != 0 ) {
                        csi_time_interval = csi_time_interval - 100;
                        found = 0;
                        break;
                    }
                }
            }
        }
        free(interval_list);
    }
#if !defined(FEATURE_CSI_CALLBACK)
    if (enable == TRUE) {
        if (g_monitor_module.csi_sched_id == 0) {
            scheduler_add_timer_task(g_monitor_module.sched, TRUE,
                        &(g_monitor_module.csi_sched_id), csi_getCSIData,
                        NULL, csi_time_interval, 0);
        } else {
            if (g_monitor_module.csi_sched_interval != csi_time_interval) {
                g_monitor_module.csi_sched_interval = csi_time_interval;
                scheduler_update_timer_task_interval(g_monitor_module.sched,
                                        g_monitor_module.csi_sched_id, csi_time_interval);
            }
        }
    } else {
        if (g_monitor_module.csi_sched_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched,
                                        g_monitor_module.csi_sched_id);
            g_monitor_module.csi_sched_id = 0;
        }
    }
#else
//Enabling Pinger only on CMXB7
#if (defined (_XB7_PRODUCT_REQ_) && !defined (_COSA_BCM_ARM_))
    if (enable == TRUE) {
        if (g_monitor_module.csi_sched_id == 0) {
            scheduler_add_timer_task(g_monitor_module.sched, TRUE,
                        &(g_monitor_module.csi_sched_id), csi_sendPingData,
                        NULL, csi_time_interval, 0);
        } else {
            if (g_monitor_module.csi_sched_interval != csi_time_interval) {
                g_monitor_module.csi_sched_interval = csi_time_interval;
                scheduler_update_timer_task_interval(g_monitor_module.sched,
                                        g_monitor_module.csi_sched_id, csi_time_interval);
            }
        }
    } else {
        if (g_monitor_module.csi_sched_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched,
                                        g_monitor_module.csi_sched_id);
            g_monitor_module.csi_sched_id = 0;
        }
    }
#endif
#endif
    return 0;
}

static void csi_publish(wifi_monitor_data_t *evtData) {
    int i = 0;
    int j = 0;
    int count = 0;
    struct timeval time_diff;
    csi_session_t *csi = NULL;
    void* pLastSnapshotTime = NULL;
    void* pCsiTimeInterval  = NULL;

    if(evtData == NULL) {
        return;
    }
    pthread_mutex_lock(&g_events_monitor.lock);
    count = queue_count(g_events_monitor.csi_queue);

    for(i=0; i<count; i++) {
        csi = queue_peek(g_events_monitor.csi_queue, i);

        if(csi == NULL || !(csi->enable && csi->subscribed)){
            continue;
        }
        /*this code is hit every MONITOR_RUNNING_INTERVAL_IN_MILLISEC, 
          Rounding off by -5 to make sure  we do not miss an interval, as hit this path
          1 or 2 msec earlier at times*/
        pLastSnapshotTime = &csi->last_snapshot_time;
        if(!timeval_subtract(&time_diff, &csi_prune_timer, pLastSnapshotTime)) {
            pCsiTimeInterval = &csi->csi_time_interval;
            if(csi_timedout(&time_diff, pCsiTimeInterval)) {
                for(j = 0; j < csi->no_of_mac; j++) {
                    if(memcmp(evtData->u.csi.sta_mac, csi->mac_list[j],  sizeof(mac_address_t)) == 0) { 
                        wifi_dbg_print(1, "%s: Publish CSI Event - MAC  %02x:%02x:%02x:%02x:%02x:%02x\n",__func__, evtData->u.csi.sta_mac[0], evtData->u.csi.sta_mac[1], evtData->u.csi.sta_mac[2], evtData->u.csi.sta_mac[3], evtData->u.csi.sta_mac[4], evtData->u.csi.sta_mac[5]);
                        evtData->csi_session = csi->csi_sess_number;
                        events_publish(*evtData);
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&g_events_monitor.lock);
}

#if defined (FEATURE_CSI_CALLBACK)
bool csi_check_timeout(csi_session_t *csi, struct timeval* t_now)
{
    struct timeval interval;
    int  interval_ms_margin;
    struct timeval timeout;
    
    if (csi == NULL || t_now == NULL) {
        wifi_dbg_print(1, "%s: Invalid arguments. csi %p, t_now %p\n",__func__, csi, t_now);
        return FALSE;
    }
    //Need to support the fluctuation of csi interval coming from the driver
    interval_ms_margin = csi->csi_time_interval - (MIN_CSI_INTERVAL/2);

    interval.tv_sec = (interval_ms_margin / 1000);
    interval.tv_usec = (interval_ms_margin % 1000) * 1000;
    timeradd(&(csi->last_snapshot_time), &interval, &timeout);
    if (timercmp(t_now, &timeout, >)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

INT process_csi(mac_address_t mac_addr, wifi_csi_data_t  *csi_data)
{
    struct timeval t_now;
    int i, j;
    int csi_subscribers_count = 0;
    bool mac_found = FALSE;
    csi_session_t *csi = NULL;
    wifi_monitor_data_t evtData;

    wifi_dbg_print(1, "%s: CSI data received - MAC  %02x:%02x:%02x:%02x:%02x:%02x\n",__func__, mac_addr[0], mac_addr[1],
                                                        mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    gettimeofday(&t_now, NULL);

    evtData.event_type = monitor_event_type_csi;
    memcpy(evtData.u.csi.sta_mac, mac_addr, sizeof(mac_addr_t));
    memcpy(&evtData.u.csi.csi, csi_data, sizeof(wifi_csi_data_t));

    pthread_mutex_lock(&g_events_monitor.lock);
    csi_subscribers_count = queue_count(g_events_monitor.csi_queue);

    for (i =0; i < csi_subscribers_count; i++) {
        mac_found = FALSE;
        csi = queue_peek(g_events_monitor.csi_queue, i);
        if (csi == NULL || !(csi->enable && csi->subscribed)) {
            continue;
        }
        for (j = 0; j < csi->no_of_mac; j++) {
            if (csi->mac_is_connected[j] == FALSE) {
                continue;
            }
            if (memcmp(mac_addr, csi->mac_list[j], sizeof(mac_addr_t)) == 0) {
                mac_found = TRUE;
                break;
            }
        }
        if (mac_found == TRUE) {
            evtData.csi_session = csi->csi_sess_number;
            //check interval
            if (csi->csi_time_interval == MIN_CSI_INTERVAL || csi_check_timeout(csi, &t_now)) {
                evtData.csi_session = csi->csi_sess_number;
                wifi_dbg_print(1, "%s: Publish CSI Event - MAC  %02x:%02x:%02x:%02x:%02x:%02x Session %d\n",__func__, mac_addr[0], mac_addr[1],
                                                        mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5], csi->csi_sess_number);
                events_publish(evtData);
                csi->last_snapshot_time = t_now;
            }
        }
    }
    pthread_mutex_unlock(&g_events_monitor.lock);
    return 0;
}

int csi_sendPingData(void *arg)
{
    mac_addr_t  tmp_csiClientMac[MAX_CSI_CLIENTS_PER_SESSION];
    int count=0, i=0, j =0, k=0, m=0;
    int csi_subscribers_count = 0;
    csi_session_t *csi = NULL;
    bool mac_found = FALSE;
    bool refresh = FALSE;
    void* pCsiClientIpAge   = NULL;
    //Iterating through each VAP and collecting data
    for (i = 0; i < MAX_VAP; i++) {
        count=0;
        memset(tmp_csiClientMac, 0, sizeof(tmp_csiClientMac));
        pthread_mutex_lock(&g_events_monitor.lock);
        csi_subscribers_count = queue_count(g_events_monitor.csi_queue);
 
        for(k =0; k < csi_subscribers_count; k++) {
            mac_found = FALSE;
            csi = queue_peek(g_events_monitor.csi_queue, k);
            if(csi == NULL || !(csi->enable && csi->subscribed)) {
                continue;
            }

            for(j = 0; j < csi->no_of_mac; j++) {
                if((csi->mac_is_connected[j] == FALSE) || (csi->ap_index[j] != i)) {
                  continue;
                }
                for(m=0; m<count; m++) {
                  if(memcmp(tmp_csiClientMac[m], csi->mac_list[j], sizeof(mac_addr_t)) == 0) {
                    mac_found = TRUE;
                    break;
                  }
                }
                if(mac_found == TRUE) {		
                  wifi_dbg_print(1, "%s: Mac already present in CSI list %02x..%02x\n",__func__, csi->mac_list[j][0], csi->mac_list[j][5]);
                  continue;
                }
                wifi_dbg_print(1, "%s: Adding Mac for csi collection %02x..%02x ap_idx %d\n",__func__, csi->mac_list[j][0], csi->mac_list[j][5], i);
                memcpy(&tmp_csiClientMac[count], &csi->mac_list[j], sizeof(mac_addr_t));
                if((csi->client_ip[j][0] != '\0') && ((csi->client_ip_age[j]*csi->csi_time_interval)  <= IPREFRESH_PERIOD_IN_MILLISECONDS) && (g_events_monitor.vap_ip[j][0] != '\0')) {
                  refresh = FALSE;
                }
                else {
                  refresh = TRUE;
                }
                pCsiClientIpAge = &csi->client_ip_age[j];
                send_ping_data(csi->ap_index[j], (unsigned char *)&csi->mac_list[j][0],
                               &csi->client_ip[j][0], &g_events_monitor.vap_ip[j][0], pCsiClientIpAge,refresh);
                csi->client_ip_age[j]++;
                count++;
            }
        }
        pthread_mutex_unlock(&g_events_monitor.lock);
    }
    //csi_refresh_session();
    return TIMER_TASK_COMPLETE;
}  
#endif

int csi_getCSIData(void *arg)
{
    mac_addr_t  tmp_csiClientMac[MAX_CSI_CLIENTS_PER_SESSION];
    int count=0, i=0, itrcsi=0, itrc=0, ret=RETURN_ERR, j =0, k=0, m=0;
    wifi_monitor_data_t evtData;
    struct timeval time_diff;
    int csi_subscribers_count = 0;
    csi_session_t *csi = NULL;
    bool mac_found = FALSE;
    bool refresh = FALSE;
    int total_events = 0;
    int re_itr = 0;
    void* pLastSnapshotTime = NULL;
    void* pCsiTimeInterval  = NULL;
    void* pCsiClientIpAge   = NULL;

    gettimeofday(&csi_prune_timer, NULL);
    memset((char *)&evtData,0,sizeof(wifi_monitor_data_t));
    wifi_associated_dev3_t *dev_array = NULL;
    //Iterating through each VAP and collecting data

   for (i = 0; i < MAX_VAP; i++) {
        count=0;
        memset(tmp_csiClientMac, 0, sizeof(tmp_csiClientMac));

        pthread_mutex_lock(&g_events_monitor.lock);
        csi_subscribers_count = queue_count(g_events_monitor.csi_queue);
 
        for(k =0; k < csi_subscribers_count; k++) {
            mac_found = FALSE;
            csi = queue_peek(g_events_monitor.csi_queue, k);
            if(csi == NULL || !(csi->enable && csi->subscribed)) {
                continue;
            }
            /*this code is hit every MONITOR_RUNNING_INTERVAL_IN_MILLISEC, 
              Rounding off by -5 to make sure  we do not miss an interval, as hit this path
              1 or 2 msec earlier at times*/
            pLastSnapshotTime = &csi->last_snapshot_time;
            if(!timeval_subtract(&time_diff, &csi_prune_timer, pLastSnapshotTime)) {
                pCsiTimeInterval = &csi->csi_time_interval;
                if(csi_timedout(&time_diff, pCsiTimeInterval)) {
                    for(j = 0; j < csi->no_of_mac; j++) {
                        if((csi->mac_is_connected[j] == FALSE) || (csi->ap_index[j] != i)) {
                            continue;
                        }
                        for(m=0; m<count; m++) {
                            if(memcmp(tmp_csiClientMac[m], csi->mac_list[j], sizeof(mac_addr_t)) == 0) {
                                mac_found = TRUE;
                                break;
                            }
                        }
                        if(mac_found == TRUE) {		
                            wifi_dbg_print(1, "%s: Mac already present in CSI list %02x..%02x\n",__func__, csi->mac_list[j][0], csi->mac_list[j][5]);
                            continue;
                        }
                        wifi_dbg_print(1, "%s: Adding Mac for csi collection %02x..%02x ap_idx %d\n",__func__, csi->mac_list[j][0], csi->mac_list[j][5], i);
                        memcpy(&tmp_csiClientMac[count], &csi->mac_list[j], sizeof(mac_addr_t));
                        if((csi->client_ip[j][0] != '\0') && ((csi->client_ip_age[j]*csi->csi_time_interval)  <= IPREFRESH_PERIOD_IN_MILLISECONDS) && (g_events_monitor.vap_ip[j][0] != '\0')) {
                            refresh = FALSE;
                        }
                        else {
                            refresh = TRUE;
                        }
                        pCsiClientIpAge = &csi->client_ip_age[j];
                        send_ping_data(csi->ap_index[j], (unsigned char *)&csi->mac_list[j][0],
                                          &csi->client_ip[j][0], &g_events_monitor.vap_ip[j][0], pCsiClientIpAge,refresh);
                        csi->client_ip_age[j]++;
                        count++;
                    }
                }
            }
        }	
        pthread_mutex_unlock(&g_events_monitor.lock);
        if (count>0) {
            dev_array = (wifi_associated_dev3_t *)malloc(sizeof(wifi_associated_dev3_t)*count);
            if (dev_array != NULL) {
                memset(dev_array, 0, (sizeof(wifi_associated_dev3_t)*count));
                for (itrc=0; itrc<count; itrc++) {
                    memcpy(dev_array[itrc].cli_MACAddress, tmp_csiClientMac[itrc], sizeof(mac_addr_t));
                }
                total_events = 0;
                for (re_itr = 0; re_itr < 4; re_itr++) {
                    ret = wifi_getApAssociatedDeviceDiagnosticResult3(i, &dev_array, (unsigned int *)&count);
                    if (ret == RETURN_OK) {
                        for (itrcsi=0; itrcsi < count; itrcsi++) {
                            if (dev_array[itrcsi].cli_CsiData != NULL) {
                                evtData.event_type = monitor_event_type_csi;
                                memcpy(evtData.u.csi.sta_mac, dev_array[itrcsi].cli_MACAddress, sizeof(mac_addr_t));
                                memcpy(&evtData.u.csi.csi, dev_array[itrcsi].cli_CsiData, sizeof(wifi_csi_data_t));
                                csi_publish(&evtData);
                                wifi_dbg_print(1, "%s Free CSI data for %02x..%02x\n",__func__,dev_array[itrcsi].cli_MACAddress[0],
                                        dev_array[itrcsi].cli_MACAddress[5]);
                                if (dev_array[itrcsi].cli_CsiData != NULL) {
                                    free(dev_array[itrcsi].cli_CsiData);
                                    dev_array[itrcsi].cli_CsiData = NULL;
                                }
                                total_events++;
                            } else {
                                wifi_dbg_print(1, "%s: CSI data is NULL for %02x..%02x\n", __func__, dev_array[itrcsi].cli_MACAddress[0],
                                        dev_array[itrcsi].cli_MACAddress[5]);
                            }

                        }
                    } else {
                        wifi_dbg_print(1, "%s: wifi_getApAssociatedDeviceDiagnosticResult3 api returned error\n", __func__);
                    }
                    if (total_events == count) {
                        break;
                    }
                }
                free(dev_array);
            } else {
                wifi_dbg_print(1, "%s: Failed to allocate mem to dev_array\n",__func__);
            }
        }
    }
    csi_refresh_session();
    return TIMER_TASK_COMPLETE;
}

static int clientdiag_sheduler_enable(int ap_index)
{
    unsigned int clientdiag_interval;

    pthread_mutex_lock(&g_events_monitor.lock);
    clientdiag_interval = g_events_monitor.diag_session[ap_index].interval;
    pthread_mutex_unlock(&g_events_monitor.lock);

    if (clientdiag_interval != 0) {
        if (g_monitor_module.clientdiag_id[ap_index] == 0) {
            scheduler_add_timer_task(g_monitor_module.sched, FALSE,
                        &(g_monitor_module.clientdiag_id[ap_index]), associated_device_diagnostics_send_event,
                        (void *)&(g_monitor_module.clientdiag_sched_arg[ap_index]), clientdiag_interval, 0);
        } else {
            if (g_monitor_module.clientdiag_sched_interval[ap_index] != clientdiag_interval) {
                g_monitor_module.clientdiag_sched_interval[ap_index] = clientdiag_interval;
                scheduler_update_timer_task_interval(g_monitor_module.sched,
                                        g_monitor_module.clientdiag_id[ap_index], clientdiag_interval);
            }
        }
    } else {
        if (g_monitor_module.clientdiag_id[ap_index] != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched,
                                        g_monitor_module.clientdiag_id[ap_index]);
            g_monitor_module.clientdiag_id[ap_index] = 0;
        }
    }
    return 0;
}

void diagdata_set_interval(int interval, unsigned int ap_idx)
{
    wifi_monitor_data_t *data;

    if(ap_idx >= MAX_VAP) {
        wifi_dbg_print(1, "%s: ap_idx %d not valid\n",__func__, ap_idx);
    }
    pthread_mutex_lock(&g_events_monitor.lock);
    g_events_monitor.diag_session[ap_idx].interval = interval;
    wifi_dbg_print(1, "%s: ap_idx %d configuring inteval %d\n", __func__, ap_idx, interval);
    pthread_mutex_unlock(&g_events_monitor.lock);
    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    if (data != NULL) {
        memset(data, 0, sizeof(wifi_monitor_data_t));
        data->id = msg_id++;
        data->event_type = monitor_event_type_clientdiag_update_config;
        data->ap_index = ap_idx;

        pthread_mutex_lock(&g_monitor_module.lock);
        queue_push(g_monitor_module.queue, data);

        pthread_cond_signal(&g_monitor_module.cond);
        pthread_mutex_unlock(&g_monitor_module.lock);
    }
}

int associated_device_diagnostics_send_event(void* arg)
{
    int *ap_index;
    wifi_monitor_data_t data = {0};

    if (arg == NULL) {
        wifi_dbg_print(1, "%s(): Error arg NULL\n",__func__);
        return TIMER_TASK_ERROR;
    }
    ap_index = (int *) arg;
    data.ap_index = *ap_index;
    data.event_type = monitor_event_type_diagnostics;
    events_publish(data);
    return TIMER_TASK_COMPLETE;
}
#endif //FEATURE_CSI

#if defined (DUAL_CORE_XB3)
static BOOL erouterGetIpAddress()
{
    FILE *f;
    char ptr[32];

    memset (ptr, 0, sizeof(ptr));

    if ((f = v_secure_popen("r", "deviceinfo.sh -eip")) == NULL) {
        return false;
    } else {
        *ptr = 0;
        fgets(ptr,32,f);
        v_secure_pclose(f);
    }
    
    if ((ptr[0] >= '1') && (ptr[0] <= '9')) {
        memset(erouterIpAddrStr, 0, sizeof(erouterIpAddrStr));
        /*CID: 159695 BUFFER_SIZE_WARNING*/
        strncpy((char*)erouterIpAddrStr, ptr, sizeof(erouterIpAddrStr)-1);
        erouterIpAddrStr[sizeof(erouterIpAddrStr)-1] = '\0';
        return true;
    } else {
        return false;
    }
}
#endif

static unsigned char updateNasIpStatus (int apIndex)
{
#if defined (DUAL_CORE_XB3)
    
    static unsigned char erouterIpInitialized = 0;
#ifdef WIFI_HAL_VERSION_3
    if(isVapHotspotSecure(apIndex)) {
#else
    if ((apIndex == 8) || (apIndex == 9)) {

#endif
        if (!erouterIpInitialized) {
            if (FALSE == erouterGetIpAddress()) {
                return 0;
            } else {
                erouterIpInitialized = 1;
                return wifi_pushSecureHotSpotNASIP(apIndex, erouterIpAddrStr);
            }
        } else {
            return wifi_pushSecureHotSpotNASIP(apIndex, erouterIpAddrStr);
        }
    } else {
        return 1;
    }       
#else
    UNREFERENCED_PARAMETER(apIndex);
    return 1;
#endif
}

/* Log VAP status on percentage basis */
static void logVAPUpStatus()
{
    int i=0;
    int vapup_percentage=0;
    char log_buf[1024]={0};
    char telemetry_buf[1024]={0};
    char vap_buf[16]={0};
    char tmp[128]={0};
    errno_t rc = -1;
    wifi_dbg_print(1, "Entering %s:%d \n",__FUNCTION__,__LINE__);
	get_formatted_time(tmp);
    rc = sprintf_s(log_buf, sizeof(log_buf), "%s WIFI_VAP_PERCENT_UP:",tmp);
    if(rc < EOK)
    {
        ERR_CHK(rc);
    }
#ifdef WIFI_HAL_VERSION_3
    for(i = 0; i < (int)getTotalNumberVAPs(); i++)
#else
    for(i=0;i<MAX_VAP;i++)
#endif
    {
        if (vap_iteration > 0) {
            vapup_percentage=((int)(vap_up_arr[i])*100)/(vap_iteration);
        }
#ifdef WIFI_HAL_VERSION_3
        char delimiter = (i+1) < ((int)getTotalNumberVAPs()+1) ?';':' ';
#else
        char delimiter = (i+1) < (MAX_VAP+1) ?';':' ';
#endif
        rc = sprintf_s(vap_buf, sizeof(vap_buf), "%d,%d%c",(i+1),vapup_percentage, delimiter);
        if(rc < EOK)
        {
            ERR_CHK(rc);
        }
        rc = strcat_s(log_buf, sizeof(log_buf), vap_buf);
        ERR_CHK(rc);
        rc = strcat_s(telemetry_buf, sizeof(telemetry_buf), vap_buf);
        ERR_CHK(rc);
    }
    rc = strcat_s(log_buf, sizeof(log_buf), "\n");
    ERR_CHK(rc);
    write_to_file(wifi_health_log,log_buf);
    wifi_dbg_print(1, "%s", log_buf);
    t2_event_s("WIFI_VAPPERC_split", telemetry_buf);
    vap_iteration=0;
    memset(vap_up_arr, 0,sizeof(vap_up_arr));
    wifi_dbg_print(1, "Exiting %s:%d \n",__FUNCTION__,__LINE__);
}

/* Capture the VAP status periodically */
int captureVAPUpStatus(void *arg)
{
    static unsigned int i = 0;
    char vap_status[16];
  
    wifi_dbg_print(1, "Entering %s:%d for VAP %d\n",__FUNCTION__,__LINE__, i);
    memset(vap_status,0,16);
    wifi_getApStatus(i,vap_status);
    if(strncasecmp(vap_status,"Up",2)==0)
    {
        vap_up_arr[i]=vap_up_arr[i]+1;
        if (!vap_nas_status[i]) {
            vap_nas_status[i] = updateNasIpStatus(i);
            CosaDmlWiFi_RestoreAPInterworking(i);
        }
    } else {
        vap_nas_status[i] = 0;
    }
    i++;
#ifdef WIFI_HAL_VERSION_3
    if(i >= getTotalNumberVAPs()) {
#else
    if(i >= MAX_VAP) {
#endif
        i = 0;
        vap_iteration++;
        wifi_dbg_print(1, "Exiting %s:%d \n",__FUNCTION__,__LINE__);
        return TIMER_TASK_COMPLETE;
    }
    wifi_dbg_print(1, "Exiting %s:%d \n",__FUNCTION__,__LINE__);
    return TIMER_TASK_CONTINUE;
}

int get_chan_util_upload_period()
{

    int logInterval = DEFAULT_CHANUTIL_LOG_INTERVAL;//Default Value 15mins.
    int retPsmGet = CCSP_SUCCESS;
    char *strValue = NULL;

    retPsmGet = PSM_Get_Record_Value2(bus_handle, g_Subsystem,"dmsb.device.deviceinfo.X_RDKCENTRAL-COM_WHIX.ChUtilityLogInterval",NULL,&strValue);

    if (retPsmGet == CCSP_SUCCESS)
    {
        if (strValue && strlen(strValue))
        {
            logInterval=atoi(strValue);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
    }
    else
    {
            wifi_dbg_print(1, "%s:%d The PSM_Get_Record_Value2  is failed with %d retval  \n",__FUNCTION__,__LINE__,retPsmGet);
    }
    return logInterval;
}

static int readLogInterval()
{

    int logInterval=60;//Default Value 60mins.
    int retPsmGet = CCSP_SUCCESS;
    char *strValue=NULL;

    wifi_dbg_print(1, "Entering %s:%d \n",__FUNCTION__,__LINE__);
    retPsmGet = PSM_Get_Record_Value2(bus_handle, g_Subsystem,"dmsb.device.deviceinfo.X_RDKCENTRAL-COM_WHIX.LogInterval",NULL,&strValue);
    
    if (retPsmGet == CCSP_SUCCESS)
    {
        if (strValue && strlen(strValue))
        {
            logInterval=atoi(strValue);
            wifi_dbg_print(1, "The LogInterval is %dsec or %dmin \n",logInterval,(int)logInterval/60);
            logInterval=(int)(logInterval/60);
            ((CCSP_MESSAGE_BUS_INFO *)bus_handle)->freefunc(strValue);
        }
    }
    else
    {
            wifi_dbg_print(1, "%s:%d The PSM_Get_Record_Value2  is failed with %d retval  \n",__FUNCTION__,__LINE__,retPsmGet);
    }
    wifi_dbg_print(1, "Exiting %s:%d \n",__FUNCTION__,__LINE__);
    return logInterval;
}

void associated_client_diagnostics ()
{
    wifi_associated_dev3_t dev_conn ;
    int radioIndex;
    int chan_util = 0;
  
    char s_mac[MIN_MAC_LEN+1];
    int index = g_monitor_module.inst_msmt.ap_index;
   
    memset(&dev_conn, 0, sizeof(wifi_associated_dev3_t));
    snprintf(s_mac, MIN_MAC_LEN+1, "%02x%02x%02x%02x%02x%02x", g_monitor_module.inst_msmt.sta_mac[0],
       g_monitor_module.inst_msmt.sta_mac[1],g_monitor_module.inst_msmt.sta_mac[2], g_monitor_module.inst_msmt.sta_mac[3],
                g_monitor_module.inst_msmt.sta_mac[4], g_monitor_module.inst_msmt.sta_mac[5]);
#ifdef WIFI_HAL_VERSION_3
    radioIndex = getRadioIndexFromAp(index);
#else
     radioIndex = ((index % 2) == 0)? 0:1;
#endif

    wifi_dbg_print(1, "%s:%d: get radio NF %d\n", __func__, __LINE__, g_monitor_module.radio_data[radioIndex].NoiseFloor);

    /* ToDo: We can get channel_util percentage now, channel_ineterference percentage is 0 */
    if (wifi_getRadioBandUtilization(index, &chan_util) == RETURN_OK) {
            wifi_dbg_print(1, "%s:%d: get channel stats for radio %d\n", __func__, __LINE__, radioIndex);
            g_monitor_module.radio_data[radioIndex].channelUtil = chan_util;
            g_monitor_module.radio_data[radioIndex].channelInterference = 0;
#ifdef WIFI_HAL_VERSION_3 
            g_monitor_module.radio_data[getRadioIndexFromAp(radioIndex + 1)].channelUtil = 0;
            g_monitor_module.radio_data[getRadioIndexFromAp(radioIndex + 1)].channelInterference = 0;
#else
            g_monitor_module.radio_data[((radioIndex + 1) % 2)].channelUtil = 0;
            g_monitor_module.radio_data[((radioIndex + 1) % 2)].channelInterference = 0;
#endif
    }
 
    wifi_dbg_print(1, "%s:%d: get single connected client %s stats\n", __func__, __LINE__, s_mac);
#if !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_)
    wifi_dbg_print(1, "WIFI_HAL enabled, calling wifi_getApAssociatedClientDiagnosticResult\n");
    if (wifi_getApAssociatedClientDiagnosticResult(index, s_mac, &dev_conn) == RETURN_OK) {
           process_diagnostics(index, &dev_conn, 1);
    }
#else
    wifi_dbg_print(1, "WIFI_HAL Not enabled. Using wifi default values\n");
    process_diagnostics(index, &dev_conn, 1);
#endif
}

int radio_diagnostics(void *arg)
{
    wifi_radioTrafficStats2_t radioTrafficStats;
    BOOL radio_Enabled=FALSE;
    char            RadioFreqBand[64] = {0};
    char            RadioChanBand[64] = {0};
    ULONG           Channel = 0;
    static unsigned int radiocnt = 0;

    wifi_dbg_print(1, "%s : %d getting radio Traffic stats for Radio %d\n",__func__,__LINE__, radiocnt);
    memset(&radioTrafficStats, 0, sizeof(wifi_radioTrafficStats2_t));
    memset(&g_monitor_module.radio_data[radiocnt], 0, sizeof(radio_data_t));
    if (wifi_getRadioEnable(radiocnt, &radio_Enabled) == RETURN_OK)
    {
        if(radio_Enabled)
        {
            if (wifi_getRadioTrafficStats2(radiocnt, &radioTrafficStats) == RETURN_OK)
            {
                /* update the g_active_msmt with the radio data */
                g_monitor_module.radio_data[radiocnt].NoiseFloor = radioTrafficStats.radio_NoiseFloor;
                g_monitor_module.radio_data[radiocnt].RadioActivityFactor = radioTrafficStats.radio_ActivityFactor;
                g_monitor_module.radio_data[radiocnt].CarrierSenseThreshold_Exceeded = radioTrafficStats.radio_CarrierSenseThreshold_Exceeded;
                g_monitor_module.radio_data[radiocnt].channelUtil = radioTrafficStats.radio_ChannelUtilization;
                wifi_getRadioChannel(radiocnt, &Channel);
                g_monitor_module.radio_data[radiocnt].primary_radio_channel = Channel;

                wifi_getRadioOperatingFrequencyBand(radiocnt,RadioFreqBand);
                strncpy((char *)&g_monitor_module.radio_data[radiocnt].frequency_band, RadioFreqBand,sizeof(RadioFreqBand));

                wifi_getRadioOperatingChannelBandwidth(radiocnt,RadioChanBand);
                strncpy((char *)&g_monitor_module.radio_data[radiocnt].channel_bandwidth, RadioChanBand,sizeof(RadioChanBand));
            }
            else
            {
                wifi_dbg_print(1, "%s : %d wifi_getRadioTrafficStats2 failed for rdx : %d\n",__func__,__LINE__,radiocnt);
            }
        }
        else
        {
            wifi_dbg_print(1, "%s : %d Radio : %d is not enabled\n",__func__,__LINE__,radiocnt);
        }
    }
    else
    {
        wifi_dbg_print(1, "%s : %d wifi_getRadioEnable failed for rdx : %d\n",__func__,__LINE__,radiocnt);
    }
    radiocnt++;
#if WIFI_HAL_VERSION_3
    if (radiocnt >= getNumberRadios()) {
#else
    if (radiocnt >= MAX_RADIOS) {
#endif
        radiocnt = 0;
        return TIMER_TASK_COMPLETE;
    }
    return TIMER_TASK_CONTINUE;
}

int associated_devices_diagnostics(void *arg)
{
    static unsigned int phase = 0;
    static unsigned int vap_index = 0;
    static wifi_associated_dev3_t *dev_array = NULL;
    static unsigned int num_devs = 0;

#if !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(DUAL_CORE_XB3) && !(defined (_XB7_PRODUCT_REQ_) && defined (_COSA_BCM_ARM_))
    unsigned int i = 0;
    static unsigned int current_dev = 0;
    static int last_valid_dev = 0;
    char s_mac[MIN_MAC_LEN+1]={0};
    int itr = 0;
    int jtr = 0;
    static char assoc_list[(MAX_ASSOCIATED_WIFI_DEVS*18)+1];

    if (phase == 0) { /* phase 0: collect diag data */
        if (dev_array == NULL) {
            current_dev = 0;
            last_valid_dev = 0;
            memset(assoc_list, 0, sizeof(assoc_list));
            if (wifi_getApAssociatedDevice(vap_index, assoc_list, sizeof(assoc_list)) == RETURN_OK) {
                num_devs = (strlen(assoc_list)+1)/18;
                if (num_devs > 0) {
                    dev_array = (wifi_associated_dev3_t *) malloc(sizeof(wifi_associated_dev3_t) * num_devs);
                    if (dev_array == NULL) {
                        wifi_dbg_print(1,"%s,malloc failed ! vap_index=%u\n",__func__,vap_index);
                        goto exit_task;
                    }
                    for(i=1;i<num_devs;i++) {
                        //remove ','
                        assoc_list[(i*18)-1] = '\0';
                    }
                } else {
                    /* No devices are associated with the current vap_index */
                    phase = 1;
                    return TIMER_TASK_CONTINUE;
                }
            } else {
                /* wifi_getApAssociatedDevice dint return RETURN_OK for the current vap_index.
                 * Continue the task in the next cycle  */
                wifi_dbg_print(1,"%s,wifi_getApAssociatedDevice returned error ! vap_index=%u\n",__func__,vap_index);
                goto exit_task;
            }
            return TIMER_TASK_CONTINUE;
        }

        if(current_dev < num_devs) {
            for(itr=0; assoc_list[(current_dev*18) + itr] != '\0'; itr++) {
                if((assoc_list[(current_dev*18) + itr] != ':') && (jtr < MIN_MAC_LEN)) {
                    s_mac[jtr] = assoc_list[(current_dev*18) + itr];
                    jtr++;
                }
            }
            s_mac[jtr] = '\0';
            if (wifi_getApAssociatedClientDiagnosticResult(vap_index, s_mac, 
                                                        &dev_array[last_valid_dev]) == RETURN_OK){
                last_valid_dev++;
            }
            current_dev++;
            return TIMER_TASK_CONTINUE;
        }
        num_devs = last_valid_dev;
        phase = 1;
    }
#else //!defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined(DUAL_CORE_XB3)
    if (phase == 0) { /* phase 0: collect diag data */
        if (dev_array == NULL) {
            num_devs = 0;
            if (wifi_getApAssociatedDeviceDiagnosticResult3(vap_index, &dev_array, &num_devs) != RETURN_OK) {
                CcspTraceError(( "[%s:%d]Wi-Fi hal get Associated Device failure dev_array:[%p] for vap_index:%d number of device:%d\r\n", __func__, __LINE__, dev_array,vap_index, num_devs));
                if (dev_array) {
                    free(dev_array);
                    dev_array = NULL;
                }
                goto exit_task;
            }
            phase = 1;
            return TIMER_TASK_CONTINUE;
        }
    }
#endif
    if (phase == 1) { /* phase 1: process diag data */
#if defined (FEATURE_CSI)
        events_update_clientdiagdata(num_devs, vap_index, dev_array);
#endif //FEATURE_CSI
        process_diagnostics(vap_index, dev_array, num_devs);

        if (dev_array) {
            free(dev_array);
            dev_array = NULL;
        }
    }

exit_task:
    vap_index++;
    phase = 0;
#ifdef WIFI_HAL_VERSION_3
    if (vap_index >= getTotalNumberVAPs()) {
#else
    if (vap_index >= MAX_VAP) {
#endif
        vap_index = 0;
        return TIMER_TASK_COMPLETE;
    }
    return TIMER_TASK_CONTINUE;
}

int device_disassociated(int ap_index, char *mac, int reason)
{
    wifi_monitor_data_t *data;
	unsigned int mac_addr[MAC_ADDR_LEN];

    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    if(data == NULL) {
       return 0; 
    }
    data->id = msg_id++;

	data->event_type = monitor_event_type_disconnect;

    data->ap_index = ap_index;
	sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
           &mac_addr[0], &mac_addr[1], &mac_addr[2],
           &mac_addr[3], &mac_addr[4], &mac_addr[5]);
    data->u.dev.sta_mac[0] = mac_addr[0]; data->u.dev.sta_mac[1] = mac_addr[1]; data->u.dev.sta_mac[2] = mac_addr[2];
    data->u.dev.sta_mac[3] = mac_addr[3]; data->u.dev.sta_mac[4] = mac_addr[4]; data->u.dev.sta_mac[5] = mac_addr[5];
    data->u.dev.reason = reason;
	   
	wifi_dbg_print(1, "%s:%d:Device diaassociated on interface:%d mac:%02x:%02x:%02x:%02x:%02x:%02x\n",
        __func__, __LINE__, ap_index,
        data->u.dev.sta_mac[0], data->u.dev.sta_mac[1], data->u.dev.sta_mac[2], 
		data->u.dev.sta_mac[3], data->u.dev.sta_mac[4], data->u.dev.sta_mac[5]);
#if defined (FEATURE_CSI)
    csi_update_client_mac_status(data->u.dev.sta_mac, FALSE, ap_index); 
#endif

    pthread_mutex_lock(&g_monitor_module.lock);
    queue_push(g_monitor_module.queue, data);

    pthread_cond_signal(&g_monitor_module.cond);
    pthread_mutex_unlock(&g_monitor_module.lock);

	return 0;
}

int device_deauthenticated(int ap_index, char *mac, int reason)
{
    wifi_monitor_data_t *data;
    unsigned int mac_addr[MAC_ADDR_LEN];

    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    if (data == NULL) {
          return -1;      
    }
   
    data->id = msg_id++;

	data->event_type = monitor_event_type_deauthenticate;
    
    data->ap_index = ap_index;
	sscanf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
           &mac_addr[0], &mac_addr[1], &mac_addr[2],
           &mac_addr[3], &mac_addr[4], &mac_addr[5]);
    data->u.dev.sta_mac[0] = mac_addr[0]; data->u.dev.sta_mac[1] = mac_addr[1]; data->u.dev.sta_mac[2] = mac_addr[2];
    data->u.dev.sta_mac[3] = mac_addr[3]; data->u.dev.sta_mac[4] = mac_addr[4]; data->u.dev.sta_mac[5] = mac_addr[5];
	data->u.dev.reason = reason;
  
    wifi_dbg_print(1, "%s:%d   Device deauthenticated on interface:%d mac:%02x:%02x:%02x:%02x:%02x:%02x with reason %d\n",
        __func__, __LINE__, ap_index,
        data->u.dev.sta_mac[0], data->u.dev.sta_mac[1], data->u.dev.sta_mac[2],
                data->u.dev.sta_mac[3], data->u.dev.sta_mac[4], data->u.dev.sta_mac[5], reason);
#if defined (FEATURE_CSI)
    csi_update_client_mac_status(data->u.dev.sta_mac, FALSE, ap_index); 
#endif

    pthread_mutex_lock(&g_monitor_module.lock);
    queue_push(g_monitor_module.queue, data);

    pthread_cond_signal(&g_monitor_module.cond);
    pthread_mutex_unlock(&g_monitor_module.lock);

	return 0;
}

int device_associated(int ap_index, wifi_associated_dev_t *associated_dev)
{
    wifi_monitor_data_t *data;

    data = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
    data->id = msg_id++;

    data->event_type = monitor_event_type_connect;

    data->ap_index = ap_index;
    //data->u.dev.reason = reason;

	data->u.dev.sta_mac[0] = associated_dev->cli_MACAddress[0]; data->u.dev.sta_mac[1] = associated_dev->cli_MACAddress[1];
	data->u.dev.sta_mac[2] = associated_dev->cli_MACAddress[2]; data->u.dev.sta_mac[3] = associated_dev->cli_MACAddress[3];
	data->u.dev.sta_mac[4] = associated_dev->cli_MACAddress[4]; data->u.dev.sta_mac[5] = associated_dev->cli_MACAddress[5];

	wifi_dbg_print(1, "%s:%d:Device associated on interface:%d mac:%02x:%02x:%02x:%02x:%02x:%02x\n",
        __func__, __LINE__, ap_index,
        data->u.dev.sta_mac[0], data->u.dev.sta_mac[1], data->u.dev.sta_mac[2], 
		data->u.dev.sta_mac[3], data->u.dev.sta_mac[4], data->u.dev.sta_mac[5]);

	data->u.dev.cli_CapableNumSpatialStreams = associated_dev->cli_CapableNumSpatialStreams;
	data->u.dev.cli_RSSI = associated_dev->cli_RSSI;

#if defined (FEATURE_CSI)
    csi_update_client_mac_status(data->u.dev.sta_mac, TRUE, ap_index); 
#endif

    pthread_mutex_lock(&g_monitor_module.lock);
    queue_push(g_monitor_module.queue, data);
    pthread_cond_signal(&g_monitor_module.cond);
    pthread_mutex_unlock(&g_monitor_module.lock);

	return 0;
}

static void scheduler_telemetry_tasks(void)
{
    if (!g_monitor_module.instntMsmtenable) {
        g_monitor_module.curr_chan_util_period = get_chan_util_upload_period();
#if defined (FEATURE_OFF_CHANNEL_SCAN_5G)
        Off_Channel_5g_upload_period  = update_Off_Channel_5g_Scan_Params();
        g_monitor_module.curr_off_channel_scan_period = Off_Channel_5g_upload_period;
#endif //FEATURE_OFF_CHANNEL_SCAN_5G

        //5 minutes
        if (g_monitor_module.refresh_task_id == 0) {
            scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.refresh_task_id, refresh_task_period,
                    NULL, REFRESH_TASK_INTERVAL_MS, 0);
        }
        if (g_monitor_module.associated_devices_id == 0) {
            scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.associated_devices_id, associated_devices_diagnostics,
                    NULL, ASSOCIATED_DEVICE_DIAG_INTERVAL_MS, 0);
        }
        if (g_monitor_module.vap_status_id == 0) {
            scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.vap_status_id, captureVAPUpStatus,
                    NULL, CAPTURE_VAP_STATUS_INTERVAL_MS, 0);
        }
        if (g_monitor_module.radio_diagnostics_id == 0) {
            //RADIO_STATS_INTERVAL - 30 seconds
            scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.radio_diagnostics_id, radio_diagnostics, NULL,
                    RADIO_STATS_INTERVAL_MS, 0);
        }
        if (g_monitor_module.chutil_id == 0) {
            //get_chan_util_upload_period - configurable on PSM
            scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.chutil_id, upload_radio_chan_util_telemetry,
                    NULL, get_chan_util_upload_period()*SEC_TO_MILLISEC, 0);
        }
#if defined (FEATURE_OFF_CHANNEL_SCAN_5G)
        if (g_monitor_module.off_channel_scan_id == 0) {
            CcspTraceInfo(("Off_channel_scan offchannel scan starting...\n"));
            scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.off_channel_scan_id, Off_Channel_5g_scanner,
                    NULL, (Off_Channel_5g_upload_period + Radio_Off_Channel_current[3])*SEC_TO_MILLISEC, 0);
        }
#endif //FEATURE_OFF_CHANNEL_SCAN_5G

        //upload_period - configurable from file
        if (g_monitor_module.upload_period != 0) {
            if (g_monitor_module.client_telemetry_id == 0) {
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.client_telemetry_id,
                        upload_client_telemetry_data, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
            }
            if (g_monitor_module.client_debug_id == 0) {
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.client_debug_id,
                        upload_client_debug_stats, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
            }
            if (g_monitor_module.channel_width_telemetry_id == 0) {
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.channel_width_telemetry_id,
                        upload_channel_width_telemetry, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
            }
            if (g_monitor_module.vap_max_client_id == 0) {
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.vap_max_client_id,
                        upload_vap_rejection_sta_count_telemetry, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
            }
            if (g_monitor_module.ap_telemetry_id == 0)
                scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.ap_telemetry_id,
                        upload_ap_telemetry_data, NULL, (g_monitor_module.upload_period * MIN_TO_MILLISEC), 0);
        }

        if (g_monitor_module.radio_health_telemetry_logger_id == 0) {
            scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.radio_health_telemetry_logger_id, radio_health_telemetry_logger, NULL,
                    RADIO_HEALTH_TELEMETRY_INTERVAL_MS, 0);
        }
        if (g_monitor_module.upload_ap_telemetry_pmf_id == 0) {
            //24h
            scheduler_add_timer_task(g_monitor_module.sched, FALSE, &g_monitor_module.upload_ap_telemetry_pmf_id, upload_ap_telemetry_pmf, NULL,
                    UPLOAD_AP_TELEMETRY_INTERVAL_MS, 0);
        }
    } else {
        if (g_monitor_module.refresh_task_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.refresh_task_id);
            g_monitor_module.refresh_task_id = 0;
        }
        if (g_monitor_module.associated_devices_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.associated_devices_id);
            g_monitor_module.associated_devices_id = 0;
        }
        if (g_monitor_module.vap_status_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.vap_status_id);
            g_monitor_module.vap_status_id = 0;
        }
        if (g_monitor_module.radio_diagnostics_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.radio_diagnostics_id);
            g_monitor_module.radio_diagnostics_id = 0;
        }
        if (g_monitor_module.chutil_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.chutil_id);
            g_monitor_module.chutil_id = 0;
        }
#if defined (FEATURE_OFF_CHANNEL_SCAN_5G)
        if (g_monitor_module.off_channel_scan_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.off_channel_scan_id);
            g_monitor_module.off_channel_scan_id = 0;
        }
#endif //FEATURE_OFF_CHANNEL_SCAN_5G

        if (g_monitor_module.client_telemetry_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.client_telemetry_id);
            g_monitor_module.client_telemetry_id = 0;
        }
        if (g_monitor_module.client_debug_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.client_debug_id);
            g_monitor_module.client_debug_id = 0;
        }
        if (g_monitor_module.channel_width_telemetry_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.channel_width_telemetry_id);
            g_monitor_module.channel_width_telemetry_id = 0;
        }
        if (g_monitor_module.ap_telemetry_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.ap_telemetry_id);
            g_monitor_module.ap_telemetry_id = 0;
        }
        if (g_monitor_module.radio_health_telemetry_logger_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.radio_health_telemetry_logger_id);
            g_monitor_module.radio_health_telemetry_logger_id = 0;
        }
        if (g_monitor_module.upload_ap_telemetry_pmf_id != 0) {
            scheduler_cancel_timer_task(g_monitor_module.sched, g_monitor_module.upload_ap_telemetry_pmf_id);
            g_monitor_module.upload_ap_telemetry_pmf_id = 0;
        }
    }
}

#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : update_speedtest_tout_value                                   */
/*                                                                               */
/* DESCRIPTION   : Collect speedtest Unpausetimeout value at init time and       */
/*                 update the value in global variable                           */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void update_speedtest_tout_value()
{
    char const* name = SPEEDTEST_SUBSCRIBE;
    rbusValue_t value;
    char *st_val = NULL;
    int rc = RBUS_ERROR_SUCCESS;
    rc = rbus_get(st_handle, name, &value);

    if(rc != RBUS_ERROR_SUCCESS)
    {
        wifi_dbg_print(1, "%s: %d rbus_get failed for %s with error %d\n", __func__, __LINE__,  name, rc);
        return;
    }

    st_val = rbusValue_ToString(value, NULL, 0);
    if (st_val == NULL) {
        wifi_dbg_print(1, "%s: %d Unable to get value for event %s\n", __func__, __LINE__, name);
        return;
    }

    speedtest_timeout = atoi(st_val);
    wifi_dbg_print(1, "%s: %d Init time speedtest timeout  : %d\n", __func__, __LINE__, speedtest_timeout);
    free(st_val);
    st_val = NULL;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : st_event_receive_subscription_handler                         */
/*                                                                               */
/* DESCRIPTION   : Once after rbus subscription success/failure, this handler    */
/*                 is getting called from rbus to update the status              */
/*                                                                               */
/* INPUT         : Arg 1 : handler                                               */
/*                 Arg 2 : The structure which contains the info about the       */
/*                 event which we sent for subscription                          */
/*                 Arg 3 : Success or failure response from rbus for             */
/*                 subscription request                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/*********************************************************************************/
void st_event_receive_subscription_handler(rbusHandle_t handle, rbusEventSubscription_t* subscription, rbusError_t error)
{
   (void)handle;
   if (subscription) {
       wifi_dbg_print(1, "%s: %d event name (%s) subscribe %s\n", __func__, __LINE__, subscription->eventName,
                      error == RBUS_ERROR_SUCCESS ? "success" : "failed");
       if ((error == RBUS_ERROR_SUCCESS) && ((strcmp(subscription->eventName, SPEEDTEST_SUBSCRIBE)) == 0)) {
           update_speedtest_tout_value();
       }
   }
}
#endif

int init_wifi_monitor ()
{
	unsigned int i;
	int	rssi, rapid_reconn_max;
        unsigned int uptimeval = 0;
	char mac_str[32];
#if defined (FEATURE_CSI)
    unsigned char def_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
#endif
#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
    int rc = RBUS_ERROR_SUCCESS;

    memset(g_component_name, '\0', RBUS_MAX_NAME_LENGTH);
    snprintf(g_component_name, RBUS_MAX_NAME_LENGTH, "%s%d", "WifiMon", getpid());
    wifi_dbg_print(1, "%s: %d g_component_name updated as %s\n", __func__, __LINE__, g_component_name);

    rc = rbus_open(&st_handle, g_component_name);
    if (rc != RBUS_ERROR_SUCCESS)
    {
         wifi_dbg_print(1, "%s: %d Rbus open failed :%d\n", __func__, __LINE__, rc);
    }

    rc = rbusEvent_SubscribeExAsync(st_handle, st_events, ARRAY_SZ(st_events), st_event_receive_subscription_handler, 0);
    if (rc != RBUS_ERROR_SUCCESS)
    {
       wifi_dbg_print(1, "%s: %d Rbus Event subscription failed :%d\n", __func__, __LINE__, rc);
    }
#endif

#ifdef WIFI_HAL_VERSION_3
	for (i = 0; i < getTotalNumberVAPs(); i++) {
#else
	for (i = 0; i < MAX_VAP; i++) {
#endif
               /*TODO CID: 110946 Out-of-bounds access - Fix in QTN code*/
	       if (wifi_getSSIDMACAddress(i, mac_str) == RETURN_OK) {
			to_mac_bytes(mac_str, g_monitor_module.bssid_data[i].bssid);
	       }
  /* This is not part of Single reporting Schema, handle this when default reporting will be enabled */
             //  wifi_getRadioOperatingFrequencyBand(i%2, g_monitor_module.bssid_data[i].frequency_band);
             //  wifi_getSSIDName(i, g_monitor_module.bssid_data[i].ssid);
             //  wifi_getRadioChannel(i, (ULONG *)&g_monitor_module.bssid_data[i].primary_radio_channel);
             //  wifi_getRadioOperatingChannelBandwidth(i%2, g_monitor_module.bssid_data[i].channel_bandwidth);

#if defined (FEATURE_CSI)
        //Cleanup all CSI clients configured in driver
        wifi_enableCSIEngine(i, def_mac, FALSE);
#endif
	}

	memset(g_monitor_module.cliStatsList, 0, MAX_VAP);
    g_monitor_module.poll_period = 5;
    g_monitor_module.upload_period = get_upload_period(60);//Default value 60
    uptimeval=get_sys_uptime();
    chan_util_upload_period = get_chan_util_upload_period();

    wifi_dbg_print(1, "%s:%d system uptime val is %ld and upload period is %d in secs\n",
        __FUNCTION__,__LINE__,uptimeval,(g_monitor_module.upload_period*60));
    /* If uptime is less than the upload period then we should calculate the current
        VAP iteration for measuring correct VAP UP percentatage. Becaues we should show
        the uptime value as VAP down percentatage.
    */
    if(uptimeval<(g_monitor_module.upload_period*60))
    {
        vap_iteration=(int)uptimeval/60;
        g_monitor_module.current_poll_iter = vap_iteration;
        wifi_dbg_print(1, "%s:%d Current VAP UP check iteration  %d \n",__FUNCTION__,__LINE__,vap_iteration);
    }
    else
    {
        vap_iteration=0;
        g_monitor_module.current_poll_iter = 0;
        wifi_dbg_print(1, "%s:%d Upload period is already crossed \n",__FUNCTION__,__LINE__);
    }

	if (CosaDmlWiFi_GetGoodRssiThresholdValue(&rssi) != ANSC_STATUS_SUCCESS) {
		g_monitor_module.sta_health_rssi_threshold = -65;
	} else {
		g_monitor_module.sta_health_rssi_threshold = rssi;
	}
#ifdef WIFI_HAL_VERSION_3
    for (i = 0; i < getTotalNumberVAPs(); i++) {
#else
    for (i = 0; i < MAX_VAP; i++) {
#endif
	memset(&g_monitor_module.ap_params[i], 0 ,sizeof(g_monitor_module.ap_params[i]));
        // update rapid reconnect time limit if changed
        if (CosaDmlWiFi_GetRapidReconnectThresholdValue(i, &rapid_reconn_max) != ANSC_STATUS_SUCCESS) {
            g_monitor_module.bssid_data[i].ap_params.rapid_reconnect_threshold = 180;
        } else {
            g_monitor_module.bssid_data[i].ap_params.rapid_reconnect_threshold = rapid_reconn_max;
        }
	}

    gettimeofday(&g_monitor_module.last_signalled_time, NULL);
    gettimeofday(&g_monitor_module.last_polled_time, NULL);
	pthread_cond_init(&g_monitor_module.cond, NULL);
    pthread_mutex_init(&g_monitor_module.lock, NULL);

#ifdef WIFI_HAL_VERSION_3
    for (i = 0; i < getTotalNumberVAPs(); i++) {
#else
    for (i = 0; i < MAX_VAP; i++) {
#endif
		g_monitor_module.bssid_data[i].sta_map = hash_map_create();
		if (g_monitor_module.bssid_data[i].sta_map == NULL) {
			deinit_wifi_monitor();
			//wifi_dbg_print(1, "sta map create error\n");
			return -1;
		}
	}

	g_monitor_module.queue = queue_create();
	if (g_monitor_module.queue == NULL) {
		deinit_wifi_monitor();
		wifi_dbg_print(1, "monitor queue create error\n");
		return -1;
	}

    g_monitor_module.sched = scheduler_init();
    if (g_monitor_module.sched == NULL) {
        deinit_wifi_monitor();
        wifi_dbg_print(1, "monitor scheduler init error\n");
        return -1;
    }
    g_monitor_module.chutil_id = 0;
    g_monitor_module.client_telemetry_id = 0;
    g_monitor_module.client_debug_id = 0;
    g_monitor_module.channel_width_telemetry_id = 0;
    g_monitor_module.ap_telemetry_id = 0;
    g_monitor_module.vap_max_client_id = 0;
    g_monitor_module.refresh_task_id = 0;
    g_monitor_module.associated_devices_id = 0;
    g_monitor_module.vap_status_id = 0;
    g_monitor_module.radio_diagnostics_id = 0;
    g_monitor_module.radio_health_telemetry_logger_id = 0;
    g_monitor_module.upload_ap_telemetry_pmf_id = 0;
#if defined (FEATURE_OFF_CHANNEL_SCAN_5G)
    g_monitor_module.off_channel_scan_id = 0;
#endif //FEATURE_OFF_CHANNEL_SCAN_5G
#if defined (FEATURE_CSI)
    g_monitor_module.csi_sched_id = 0;
    g_monitor_module.csi_sched_interval = 0;
#ifdef WIFI_HAL_VERSION_3
    for (i = 0; i < getTotalNumberVAPs(); i++) {
#else
    for (i = 0; i < MAX_VAP; i++) {
#endif
        g_monitor_module.clientdiag_id[i] = 0;
        g_monitor_module.clientdiag_sched_arg[i] = i;
        g_monitor_module.clientdiag_sched_interval[i] = 0;
    }
    memset(g_events_monitor.vap_ip, '\0', sizeof(g_events_monitor.vap_ip));
#endif //FEATURE_CSI
    
    scheduler_telemetry_tasks();

#if defined (FEATURE_CSI)
    pthread_mutex_init(&g_events_monitor.lock, NULL);

    g_events_monitor.csi_queue = queue_create();
    if (g_events_monitor.csi_queue == NULL) {
        deinit_wifi_monitor();
        wifi_dbg_print(1, "monitor csi queue create error\n");
        return -1;
    }
#endif
	g_monitor_module.exit_monitor = false;
	g_monitor_module.blastReqInQueueCount = 0;
        pthread_attr_t attr;
        pthread_attr_t *attrp = NULL;

        attrp = &attr;
        pthread_attr_init(&attr);

        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	if (pthread_create(&g_monitor_module.id, attrp, monitor_function, &g_monitor_module) != 0) {
                if(attrp != NULL)
                    pthread_attr_destroy( attrp );
		deinit_wifi_monitor();
		wifi_dbg_print(1, "monitor thread create error\n");
		return -1;
	}

        if(attrp != NULL)
            pthread_attr_destroy( attrp );

#if defined (FEATURE_CSI)
    if(events_init() < 0)
    {
        wifi_dbg_print(1,"%s:%d: Failed to open socket for wifi event send\n", __func__, __LINE__);
    }
    else
    {
        wifi_dbg_print(1, "%s:%d: Opened socket for wif event\n", __func__, __LINE__);
    }
#endif
    g_monitor_module.sysevent_fd = sysevent_open("127.0.0.1", SE_SERVER_WELL_KNOWN_PORT, SE_VERSION, "wifiMonitor", &g_monitor_module.sysevent_token);
    if (g_monitor_module.sysevent_fd < 0) {
        wifi_dbg_print(1, "%s:%d: Failed to opne sysevent\n", __func__, __LINE__);
    } else {
        wifi_dbg_print(1, "%s:%d: Opened sysevent\n", __func__, __LINE__);
    }

    /* Initializing the lock for active measurement g_active_msmt.lock */
       pthread_mutex_init(&g_active_msmt.lock, NULL);

	if (initparodusTask() == -1) {
        //wifi_dbg_print(1, "%s:%d: Failed to initialize paroduc task\n", __func__, __LINE__);

	}
   
    pthread_mutex_lock(&g_apRegister_lock);
    wifi_newApAssociatedDevice_callback_register(device_associated);
#if !defined(_PLATFORM_RASPBERRYPI_) && !defined(_PLATFORM_TURRIS_)
    wifi_apDeAuthEvent_callback_register(device_deauthenticated);
    wifi_apDisassociatedDevice_callback_register(device_disassociated);
#endif
#if defined(FEATURE_CSI) && defined(FEATURE_CSI_CALLBACK)
    wifi_csi_callback_register(process_csi);
#endif
    pthread_mutex_unlock(&g_apRegister_lock);

    return 0;
}

void deinit_wifi_monitor()
{
    unsigned int i;
#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
    int rc = RBUS_ERROR_SUCCESS;
#endif
#if defined (FEATURE_CSI)
    events_deinit();
#endif

    sysevent_close(g_monitor_module.sysevent_fd, g_monitor_module.sysevent_token);
    if(g_monitor_module.queue != NULL)
        queue_destroy(g_monitor_module.queue);

    scheduler_deinit(&(g_monitor_module.sched));

#if defined (FEATURE_CSI)
    pthread_mutex_destroy(&g_events_monitor.lock);
    if(g_events_monitor.csi_queue != NULL) {
        queue_destroy(g_events_monitor.csi_queue);
    }
#endif

#if defined (WIFI_STATS_DISABLE_SPEEDTEST_RUNNING)
    rc = rbus_close(st_handle);
    if (rc != RBUS_ERROR_SUCCESS)
    {
        wifi_dbg_print(1, "%s: %d Rbus close failed :%d\n", __func__, __LINE__, rc);
    }
#endif

#ifdef WIFI_HAL_VERSION_3
    for (i = 0; i < getTotalNumberVAPs(); i++) {
#else
    for (i = 0; i < MAX_VAP; i++) {
#endif
        if(g_monitor_module.bssid_data[i].sta_map != NULL)
        {
            hash_map_destroy(g_monitor_module.bssid_data[i].sta_map);
            g_monitor_module.bssid_data[i].sta_map = NULL;
        }
    }
    pthread_mutex_destroy(&g_monitor_module.lock);
    pthread_cond_destroy(&g_monitor_module.cond);

    /* destory the active measurement g_active_msmt.lock */
    pthread_mutex_destroy(&g_active_msmt.lock);
    /* reset the blast request in monitor queue count */
    g_monitor_module.blastReqInQueueCount = 0;
}

unsigned int get_poll_period 	()
{
	return g_monitor_module.poll_period;
}

unsigned int GetINSTOverrideTTL()
{
    return g_monitor_module.instantDefOverrideTTL;
}

void SetINSTOverrideTTL(int defTTL)
{
    g_monitor_module.instantDefOverrideTTL = defTTL;
}

unsigned int GetINSTDefReportingPeriod()
{
    return g_monitor_module.instantDefReportPeriod;
}

void SetINSTDefReportingPeriod(int defPeriod)
{
    g_monitor_module.instantDefReportPeriod = defPeriod;
}

void SetINSTReportingPeriod(unsigned long pollPeriod)
{
    g_monitor_module.instantPollPeriod = pollPeriod;
}

unsigned int GetINSTPollingPeriod()
{
    return g_monitor_module.instantPollPeriod;
}

void SetINSTMacAddress(char *mac_addr)
{
    strncpy(g_monitor_module.instantMac, mac_addr, MIN_MAC_LEN);
}

char* GetInstAssocDevSchemaIdBuffer()
{
     return instSchemaIdBuffer;
}

int GetInstAssocDevSchemaIdBufferSize()
{
    int len = 0;
    if(instSchemaIdBuffer)
        len = strlen(instSchemaIdBuffer);

    return len;
}

void instant_msmt_reporting_period(int pollPeriod)
{
    int timeSpent = 0;
    int timeLeft = 0;

    wifi_dbg_print(1, "%s:%d: reporting period changed\n", __func__, __LINE__);
    pthread_mutex_lock(&g_monitor_module.lock);

    if(pollPeriod == 0){
        g_monitor_module.maxCount = 0;
        g_monitor_module.count = 0;
    }else{
    timeSpent = g_monitor_module.count * g_monitor_module.instantPollPeriod ;
	timeLeft = g_monitor_module.instantDefOverrideTTL - timeSpent;
	g_monitor_module.maxCount = timeLeft/pollPeriod;
	g_monitor_module.poll_period = pollPeriod;

	if(g_monitor_module.count > g_monitor_module.maxCount)
            g_monitor_module.count = 0;
    }
    g_monitor_module.instantPollPeriod = pollPeriod;
    if(g_monitor_module.instntMsmtenable == true)
    {
	pthread_cond_signal(&g_monitor_module.cond);
    }
    if (g_monitor_module.inst_msmt_id != 0) {
        scheduler_update_timer_task_interval(g_monitor_module.sched, g_monitor_module.inst_msmt_id,
                                                (g_monitor_module.instantPollPeriod*1000));
    }
    pthread_mutex_unlock(&g_monitor_module.lock);
}

void instant_msmt_def_period(int defPeriod)
{
    int curCount = 0;
    int newCount = 0;

    wifi_dbg_print(1, "%s:%d: def period changed\n", __func__, __LINE__);
    g_monitor_module.instantDefReportPeriod = defPeriod;

    if(g_monitor_module.instntMsmtenable == false)
    { 
         pthread_mutex_lock(&g_monitor_module.lock);

	 curCount = g_monitor_module.count;
	 newCount = g_monitor_module.instantDefReportPeriod / DEFAULT_INSTANT_POLL_TIME;

	 if(newCount > curCount){
              g_monitor_module.maxCount = newCount - curCount;
	      g_monitor_module.count = 0;
	 }else{
              wifi_dbg_print(1, "%s:%d:created max non instant report, stop polling now\n", __func__, __LINE__);
	      g_monitor_module.maxCount = 0;
	 }
	 pthread_cond_signal(&g_monitor_module.cond);
	 pthread_mutex_unlock(&g_monitor_module.lock);
    }
}

void instant_msmt_ttl(int overrideTTL)
{
    int curCount = 0;
    int newCount = 0;

    wifi_dbg_print(1, "%s:%d: TTL changed\n", __func__, __LINE__);
    g_monitor_module.instantDefOverrideTTL = overrideTTL;

    if(g_monitor_module.instantPollPeriod == 0)
        return;

    pthread_mutex_lock(&g_monitor_module.lock);

    if(overrideTTL == 0){
        g_monitor_module.maxCount = 0;
        g_monitor_module.count = 0;
    }else{   
        curCount = g_monitor_module.count;
	newCount = g_monitor_module.instantDefOverrideTTL/g_monitor_module.instantPollPeriod;
	if(newCount > curCount){
	    g_monitor_module.maxCount = newCount - curCount;
	    g_monitor_module.count = 0;
	}else{
            wifi_dbg_print(1, "%s:%d:already created maxCount report, stop polling now\n", __func__, __LINE__);
	    g_monitor_module.maxCount = 0;
	}
    }
    if(g_monitor_module.instntMsmtenable == true)
    {
	pthread_cond_signal(&g_monitor_module.cond);
    }
    pthread_mutex_unlock(&g_monitor_module.lock);
}

void instant_msmt_macAddr(char *mac_addr)
{
    mac_address_t bmac;
    int i;

    wifi_dbg_print(1, "%s:%d: get new client %s stats\n", __func__, __LINE__, mac_addr);
    strncpy(g_monitor_module.instantMac, mac_addr, MIN_MAC_LEN);
  
    to_mac_bytes(mac_addr, bmac);
#ifdef WIFI_HAL_VERSION_3
    for (i = 0; i < (int)getTotalNumberVAPs(); i++) {
#else
        for (i = 0; i < MAX_VAP; i++) {
#endif	  
if( is_device_associated(i, mac_addr)  == true)
          {
               wifi_dbg_print(1, "%s:%d: found client %s on ap %d\n", __func__, __LINE__, mac_addr,i);
               pthread_mutex_lock(&g_monitor_module.lock);
               g_monitor_module.inst_msmt.ap_index = i; 
	       memcpy(g_monitor_module.inst_msmt.sta_mac, bmac, sizeof(mac_address_t));

	       pthread_cond_signal(&g_monitor_module.cond);
	       pthread_mutex_unlock(&g_monitor_module.lock);
          
               break;
          }
    }
}

void monitor_enable_instant_msmt(mac_address_t sta_mac, bool enable)
{
	mac_addr_str_t sta;
	unsigned int i;
	wifi_monitor_data_t *event;

	to_sta_key(sta_mac, sta);
	wifi_dbg_print(1, "%s:%d: instant measurements %s for sta:%s\n", __func__, __LINE__, (enable == true)?"start":"stop", sta);

        g_monitor_module.instntMsmtenable = enable;  
	pthread_mutex_lock(&g_monitor_module.lock);
	
	if (g_monitor_module.inst_msmt.active == true) {
		if (enable == false) {
			if (memcmp(g_monitor_module.inst_msmt.sta_mac, sta_mac, sizeof(mac_address_t)) == 0) {
				wifi_dbg_print(1, "%s:%d: instant measurements active for sta:%s, should stop\n", __func__, __LINE__, sta);
				g_monitor_module.instantDefOverrideTTL = DEFAULT_INSTANT_REPORT_TIME;

				event = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));
				event->event_type = monitor_event_type_stop_inst_msmt;
				memcpy(event->u.imsmt.sta_mac, sta_mac, sizeof(mac_address_t));

				event->u.imsmt.ap_index = g_monitor_module.inst_msmt.ap_index;
				event->ap_index = g_monitor_module.inst_msmt.ap_index;

				queue_push(g_monitor_module.queue, event);

				pthread_cond_signal(&g_monitor_module.cond);
			}

		} else {
			// must return 
			wifi_dbg_print(1, "%s:%d: instant measurements active for sta:%s, should just return\n", __func__, __LINE__, sta);
		}
        	
		pthread_mutex_unlock(&g_monitor_module.lock);

		return;

	}
			
	wifi_dbg_print(1, "%s:%d: instant measurements not active should look for sta:%s\n", __func__, __LINE__, sta);

#ifdef WIFI_HAL_VERSION_3
        for (i = 0; i < getTotalNumberVAPs(); i++) {
#else
        for (i = 0; i < MAX_VAP; i++) {
#endif
		if ( is_device_associated(i, sta) == true ) {		
			wifi_dbg_print(1, "%s:%d: found sta:%s on ap index:%d starting instant measurements\n", __func__, __LINE__, sta, i);
			event = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));

			event->event_type = monitor_event_type_start_inst_msmt;	

			memcpy(event->u.imsmt.sta_mac, sta_mac, sizeof(mac_address_t));

			event->u.imsmt.ap_index = i;
			event->ap_index = i;

			queue_push(g_monitor_module.queue, event);
			pthread_cond_signal(&g_monitor_module.cond);

			break;
		} 
	}

	pthread_mutex_unlock(&g_monitor_module.lock);
}

bool monitor_is_instant_msmt_enabled()
{
        return g_monitor_module.instntMsmtenable;
}


/* Active Measurement GET Calls */

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : monitor_is_active_msmt_enabled                                */
/*                                                                               */
/* DESCRIPTION   : This function returns the status of the Active Measurement    */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : TRUE / FALSE                                                  */
/*                                                                               */
/*********************************************************************************/

bool monitor_is_active_msmt_enabled()
{
    return g_active_msmt.active_msmt.ActiveMsmtEnable;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : GetActiveMsmtPktSize                                          */
/*                                                                               */
/* DESCRIPTION   : This function returns the size of the packet configured       */
/*                 for Active Measurement                                        */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : size of the packet                                            */
/*                                                                               */
/*********************************************************************************/

unsigned int GetActiveMsmtPktSize()
{
    return g_active_msmt.active_msmt.ActiveMsmtPktSize;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : GetActiveMsmtSampleDuration                                   */
/*                                                                               */
/* DESCRIPTION   : This function returns the duration between the samples        */
/*                 configured for Active Measurement                             */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : duration between samples                                      */
/*                                                                               */
/*********************************************************************************/

unsigned int GetActiveMsmtSampleDuration()
{
    return g_active_msmt.active_msmt.ActiveMsmtSampleDuration;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : GetActiveMsmtNumberOfSamples                                  */
/*                                                                               */
/* DESCRIPTION   : This function returns the count of samples configured         */
/*                 for Active Measurement                                        */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : Sample count                                                  */
/*                                                                               */
/*********************************************************************************/

unsigned int GetActiveMsmtNumberOfSamples()
{
    return g_active_msmt.active_msmt.ActiveMsmtNumberOfSamples;
}
/* Active Measurement Step & Plan GET calls */

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : GetActiveMsmtStepID                                           */
/*                                                                               */
/* DESCRIPTION   : This function returns the Step Identifier configured          */
/*                 for Active Measurement                                        */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : Step Identifier                                               */
/*                                                                               */
/*********************************************************************************/
unsigned int GetActiveMsmtStepID()
{
    return g_active_msmt.curStepData.StepId;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : GetActiveMsmtPlanID                                           */
/*                                                                               */
/* DESCRIPTION   : This function returns the Plan Id configured for              */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : pPlanId                                                       */
/*                                                                               */
/* OUTPUT        : Plan ID                                                       */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/
void GetActiveMsmtPlanID(unsigned int *pPlanID)
{
    if (pPlanID != NULL)
    {
        memcpy(pPlanID, g_active_msmt.active_msmt.PlanId, PLAN_ID_LENGTH);
    }
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : GetActiveMsmtStepSrcMac                                       */
/*                                                                               */
/* DESCRIPTION   : This function returns the Step Source Mac configured for      */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : pStepSrcMac                                                   */
/*                                                                               */
/* OUTPUT        : Step Source Mac                                               */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/
void GetActiveMsmtStepSrcMac(mac_address_t pStepSrcMac)
{
    if (pStepSrcMac != NULL)
    {
        memcpy(pStepSrcMac, g_active_msmt.curStepData.SrcMac, sizeof(mac_address_t));
    }
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : GetActiveMsmtStepDestMac                                      */
/*                                                                               */
/* DESCRIPTION   : This function returns the Step Destination Mac configured for */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : pStepDstMac                                                   */
/*                                                                               */
/* OUTPUT        : Step Destination Mac                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/
void GetActiveMsmtStepDestMac(mac_address_t pStepDstMac)
{
    if (pStepDstMac != NULL)
    {
        memcpy(pStepDstMac, g_active_msmt.curStepData.DestMac, sizeof(mac_address_t));
    }
}


/* Active Measurement SET Calls */

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : SetActiveMsmtEnable                                           */
/*                                                                               */
/* DESCRIPTION   : This function set the status of Active Measurement            */
/*                                                                               */
/* INPUT         : enable - flag to enable/ disable Active Measurement           */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void SetActiveMsmtEnable(bool enable)
{
    wifi_monitor_data_t *event;
    wifi_dbg_print(1, "%s:%d: changing the Active Measurement Flag to %s\n", __func__, __LINE__,(enable ? "true" : "false"));
    CcspWifiTrace(("RDK_LOG_INFO, %s-%d changing the Active Measurement Flag to %s\n", __FUNCTION__, __LINE__, (enable ? "true" : "false")));

    /* return if enable is false and there is no more step to process */
    if (!enable)
    {
        g_active_msmt.active_msmt.ActiveMsmtEnable = enable;
        wifi_dbg_print(1, "%s:%d: changed the Active Measurement Flag to false\n", __func__, __LINE__);
        if (g_monitor_module.blastReqInQueueCount)
            CcspWifiTrace(("RDK_LOG_INFO,  %s-%d Blaster stopped, pending queue value %d\n!!", __FUNCTION__, __LINE__, g_monitor_module.blastReqInQueueCount));

        return;
    }
    wifi_dbg_print(1, "%s:%d: allocating memory for event data\n", __func__, __LINE__);
    event = (wifi_monitor_data_t *)malloc(sizeof(wifi_monitor_data_t));

    if ( event == NULL)
    {
        wifi_dbg_print(1, "%s:%d: memory allocation for event failed.\n", __func__, __LINE__);
        return;
    }

    memset(event, 0, sizeof(wifi_monitor_data_t));
    /* update the event data */
    event->event_type = monitor_event_type_process_active_msmt;

    /* push the event to the monitor queue */
    wifi_dbg_print(1, "%s:%d: Acquiring lock\n", __func__, __LINE__);
    pthread_mutex_lock(&g_monitor_module.lock);
    queue_push(g_monitor_module.queue, event);
    g_monitor_module.blastReqInQueueCount++;
    wifi_dbg_print(1, "%s:%d: pushed the step info into monitor queue with queucount : %d \n", __func__, __LINE__,g_monitor_module.blastReqInQueueCount);
    wifi_dbg_print(1, "%s:%d: released the mutex lock for monitor queue\n", __func__, __LINE__);

    pthread_cond_signal(&g_monitor_module.cond);
    pthread_mutex_unlock(&g_monitor_module.lock);
    wifi_dbg_print(1, "%s:%d: signalled the monitor thread for active measurement\n", __func__, __LINE__);

    g_active_msmt.active_msmt.ActiveMsmtEnable = enable;
    wifi_dbg_print(1, "%s:%d: Active Measurement Flag changed to %s\n", __func__, __LINE__,(enable ? "true" : "false"));
    return;
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : SetActiveMsmtPktSize                                          */
/*                                                                               */
/* DESCRIPTION   : This function set the size of packet configured for           */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : PktSize - size of packet                                      */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void SetActiveMsmtPktSize(unsigned int PktSize)
{
    wifi_dbg_print(1, "%s:%d: Active Measurement Packet Size Changed to %d \n", __func__, __LINE__,PktSize);
    pthread_mutex_lock(&g_active_msmt.lock);
    g_active_msmt.active_msmt.ActiveMsmtPktSize = PktSize;
    pthread_mutex_unlock(&g_active_msmt.lock);
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : SetActiveMsmtSampleDuration                                   */
/*                                                                               */
/* DESCRIPTION   : This function set the sample duration configured for          */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : Duration - duration between samples                           */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void SetActiveMsmtSampleDuration(unsigned int Duration)
{
    wifi_dbg_print(1, "%s:%d: Active Measurement Sample Duration Changed to %d \n", __func__, __LINE__,Duration);
    pthread_mutex_lock(&g_active_msmt.lock);
    g_active_msmt.active_msmt.ActiveMsmtSampleDuration = Duration;
    pthread_mutex_unlock(&g_active_msmt.lock);
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : SetActiveMsmtNumberOfSamples                                  */
/*                                                                               */
/* DESCRIPTION   : This function set the count of sample configured for          */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : NoOfSamples - count of samples                                */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void SetActiveMsmtNumberOfSamples(unsigned int NoOfSamples)
{
    wifi_dbg_print(1, "%s:%d: Active Measurement Number of Samples Changed %d \n", __func__, __LINE__,NoOfSamples);
    pthread_mutex_lock(&g_active_msmt.lock);
    g_active_msmt.active_msmt.ActiveMsmtNumberOfSamples = NoOfSamples;
    pthread_mutex_unlock(&g_active_msmt.lock);
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : SetActiveMsmtPlanID                                           */
/*                                                                               */
/* DESCRIPTION   : This function set the Plan Identifier configured for          */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : pPlanID - Plan Idetifier                                      */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void SetActiveMsmtPlanID(char *pPlanID)
{
    wifi_dbg_print(1, "%s:%d: changing the plan ID to %s \n", __func__, __LINE__,pPlanID);
    unsigned char       PlanId[PLAN_ID_LENGTH];
    int                 StepCount = 0;

    pthread_mutex_lock(&g_active_msmt.lock);
    to_plan_id((UCHAR*)pPlanID, PlanId);
    if (strncasecmp((char*)PlanId, (char*)g_active_msmt.active_msmt.PlanId, PLAN_ID_LENGTH) != 0)
    {
        /* reset all the step information under the existing plan */
        for (StepCount = 0; StepCount < MAX_STEP_COUNT; StepCount++)
        {
           g_active_msmt.active_msmt.StepInstance[StepCount] = 0;
        }
        to_plan_id((UCHAR*)pPlanID, g_active_msmt.active_msmt.PlanId);
    }
    pthread_mutex_unlock(&g_active_msmt.lock);
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : SetActiveMsmtStepID                                           */
/*                                                                               */
/* DESCRIPTION   : This function set the Step Identifier configured for          */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : StepId - Step Identifier                                      */
/*                 StepIns - Step Instance                                       */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void SetActiveMsmtStepID(unsigned int StepId, ULONG StepIns)
{
    wifi_dbg_print(1, "%s:%d: Active Measurement Step Id Changed to %d for ins : %d\n", __func__, __LINE__,StepId,StepIns);
    pthread_mutex_lock(&g_active_msmt.lock);
    g_active_msmt.active_msmt.Step[StepIns].StepId = StepId;
    pthread_mutex_unlock(&g_active_msmt.lock);
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : SetActiveMsmtStepSrcMac                                       */
/*                                                                               */
/* DESCRIPTION   : This function set the Step Source Mac configured for          */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : SrcMac - Step Source Mac                                      */
/*                 StepIns - Step Instance                                       */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void SetActiveMsmtStepSrcMac(char *SrcMac, ULONG StepIns)
{
    mac_address_t bmac;
    wifi_dbg_print(1, "%s:%d: Active Measurement Step Src Mac changed to %s for ins : %d\n", __func__, __LINE__,SrcMac,StepIns);
    pthread_mutex_lock(&g_active_msmt.lock);
    to_mac_bytes(SrcMac, bmac);
    memset(g_active_msmt.active_msmt.Step[StepIns].SrcMac, 0, sizeof(mac_address_t));
    memcpy(g_active_msmt.active_msmt.Step[StepIns].SrcMac, bmac, sizeof(mac_address_t));
    pthread_mutex_unlock(&g_active_msmt.lock);
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : SetActiveMsmtStepDstMac                                       */
/*                                                                               */
/* DESCRIPTION   : This function set the Step Destination Mac configured for     */
/*                 Active Measurement                                            */
/*                                                                               */
/* INPUT         : DstMac - Step Destination Mac                                 */
/*                 StepIns - Step Instance                                       */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void SetActiveMsmtStepDstMac(char *DstMac, ULONG StepIns)
{
    mac_address_t bmac;
    int i;
    bool is_found = false; 

    wifi_dbg_print(1, "%s:%d: Active Measurement Step Destination Mac changed to %s for step ins : %d\n", __func__, __LINE__,DstMac,StepIns);

    memset(g_active_msmt.active_msmt.Step[StepIns].DestMac, 0, sizeof(mac_address_t));
    to_mac_bytes(DstMac, bmac);

#ifdef WIFI_HAL_VERSION_3
    for (i = 0; i < (int)getTotalNumberVAPs(); i++) 
#else
    for (i = 0; i < MAX_VAP; i++) 
#endif    
    {
          if ( is_device_associated(i, DstMac)  == true)
          {
               wifi_dbg_print(1, "%s:%d: found client %s on ap %d\n", __func__, __LINE__, DstMac,i);
               is_found = true;
               pthread_mutex_lock(&g_active_msmt.lock);
               g_active_msmt.active_msmt.Step[StepIns].ApIndex = i;
               memcpy(g_active_msmt.active_msmt.Step[StepIns].DestMac, bmac, sizeof(mac_address_t));
               /* update the step instance number */
               g_active_msmt.active_msmt.StepInstance[StepIns] = 1;
               wifi_dbg_print(1, "%s:%d: updated stepIns to 1 for step : %d\n", __func__, __LINE__,StepIns);
               pthread_mutex_unlock(&g_active_msmt.lock);

               break;
          }
    }
    if (!is_found)
    {
        wifi_dbg_print(1, "%s:%d: client %s not found \n", __func__, __LINE__, DstMac);
        pthread_mutex_lock(&g_active_msmt.lock);
        g_active_msmt.active_msmt.Step[StepIns].ApIndex = -1;
        memcpy(g_active_msmt.active_msmt.Step[StepIns].DestMac, bmac, sizeof(mac_address_t));
        g_active_msmt.active_msmt.StepInstance[StepIns] = 1;
        wifi_dbg_print(1, "%s:%d: updated stepIns to 1 for step : %d\n", __func__, __LINE__,StepIns);
        pthread_mutex_unlock(&g_active_msmt.lock);
    }
}

// This function sends CapableStream param value from monitor to apis
int monitor_apis_param_send(int index, PCOSA_DML_WIFI_AP_ASSOC_DEVICE pWifiApDev)
{
    sta_key_t sta_key;
    sta_data_t *sta = NULL;
    hash_map_t *sta_map = NULL;

    #ifdef WIFI_HAL_VERSION_3
    if (pWifiApDev == NULL || (index < 0 || index >= (int)getTotalNumberVAPs())) {
    #else
    if (pWifiApDev == NULL || (index < 0 || index >= MAX_VAP)) {
    #endif //WIFI_HAL_VERSION_3

        CcspWifiTrace(("RDK_LOG_ERROR, %s pWifiApDev NULL or index out of range \n", __FUNCTION__));
        return RETURN_ERR;
    }

    pWifiApDev->CapableNumSpatialStreams = 0;
    sta_map = g_monitor_module.bssid_data[index].sta_map;

    if (sta_map == NULL) {
        CcspWifiTrace(("RDK_LOG_ERROR, %s sta_map NULL\n", __FUNCTION__));
        return RETURN_ERR;
    }

    sta = (sta_data_t *)hash_map_get(sta_map, to_sta_key(pWifiApDev->MacAddress, sta_key));

    if (sta == NULL) {
        CcspWifiTrace(("RDK_LOG_ERROR, %s sta NULL, hash_map_get returned NULL\n", __FUNCTION__));
        return RETURN_ERR;
    }

    pWifiApDev->CapableNumSpatialStreams = sta->cli_CapableNumSpatialStreams;

    return RETURN_OK;
}

/* This function returns the system uptime at the time of init */
long get_sys_uptime()
{
    struct sysinfo s_info;
    int error = sysinfo(&s_info);
    if(error != 0)
    {
        wifi_dbg_print(1, "%s:%d: Error reading sysinfo %d \n", __func__, __LINE__,error);
    }
    return s_info.uptime;
}

/*
The get_upload_period takes two arguments iteration and oldInterval.
Because, it will return old interval value if check is less than 5mins.
*/
unsigned int get_upload_period  (int oldInterval)
{
    FILE *fp;
    char buff[64];
    char *ptr;
    int logInterval=oldInterval;
    struct timeval polling_time = {0};
    time_t  time_gap = 0;
    gettimeofday(&polling_time, NULL);

    if ((fp = fopen("/tmp/upload", "r")) == NULL) {
    /* Minimum LOG Interval we can set is 300 sec, just verify every 5 mins any change in the LogInterval
       if any change in log_interval do the calculation and dump the VAP status */
         time_gap = polling_time.tv_sec - lastpolledtime;
         if ( time_gap >= 300 )
         {
              logInterval=readLogInterval();
              lastpolledtime = polling_time.tv_sec;
         }
         return logInterval;
    }

    fgets(buff, 64, fp);
    if ((ptr = strchr(buff, '\n')) != NULL) {
        *ptr = 0;
    }
    fclose(fp);

    return atoi(buff);
}

wifi_monitor_t *get_wifi_monitor	()
{
	return &g_monitor_module;
}

wifi_actvie_msmt_t *get_active_msmt_data()
{
	return &g_active_msmt;
}

void wifi_dbg_print(int level, char *format, ...)
{
    char buff[4096] = {0};
    va_list list;
    static FILE *fpg = NULL;
    errno_t rc = -1;
    UNREFERENCED_PARAMETER(level);
   
    if ((access("/nvram/wifiMonDbg", R_OK)) != 0) {
        return;
    }

    get_formatted_time(buff);
    rc = strcat_s(buff, sizeof(buff), " ");
    ERR_CHK(rc);

    va_start(list, format);
    vsprintf(&buff[strlen(buff)], format, list);
    va_end(list);
    
    if (fpg == NULL) {
        fpg = fopen("/tmp/wifiMon", "a+");
        if (fpg == NULL) {
            return;
        } else {
            fputs(buff, fpg);
        }
    } else {
        fputs(buff, fpg);
    }
    
    fflush(fpg);
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : startWifiBlast                                                */
/*                                                                               */
/* DESCRIPTION   : This function start the pktgen to blast the packets with      */
/*                 the configured parameters                                     */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/
void *startWifiBlast(void *vargp)
{
        char command[BUFF_LEN_MAX];
        char result[BUFF_LEN_MAX];
        int     oldcanceltype;
        UNREFERENCED_PARAMETER(vargp);

        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldcanceltype);

        snprintf(command,BUFF_LEN_MAX,"echo \"start\" >> %s",PKTGEN_CNTRL_FILE);
        executeCommand(command,result);
        return NULL;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : StopWifiBlast                                                 */
/*                                                                               */
/* DESCRIPTION   : This function stops the pktgen and reset the pktgen conf      */
/*                                                                               */
/* INPUT         : vargp - pointer to variable arguments                         */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : TRUE / FALSE                                                  */
/*                                                                               */
/*********************************************************************************/

int StopWifiBlast(void)
{
        char command[BUFF_LEN_MAX];
        char result[BUFF_LEN_MAX];

        executeCommand( "echo \"stop\" >> /proc/net/pktgen/pgctrl",result);

        snprintf(command,BUFF_LEN_MAX,"echo \"stop\" >> %s",PKTGEN_CNTRL_FILE);
        executeCommand(command,result);

        snprintf(command,BUFF_LEN_MAX,"echo \"reset\" >> %s",PKTGEN_CNTRL_FILE);
        executeCommand(command,result);
        return 1;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : executeCommand                                                */
/*                                                                               */
/* DESCRIPTION   : This is a wrapper function to execute the command             */
/*                                                                               */
/* INPUT         : command - command to execute                                  */
/*                 result  - result of the execution                             */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : TRUE / FALSE                                                  */
/*                                                                               */
/*********************************************************************************/

int executeCommand(char* command,char* result)
{
        UNREFERENCED_PARAMETER(result);
        wifi_dbg_print(1,"CMD: %s START\n", command);

        system (command);
        return 0;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : configurePktgen                                               */
/*                                                                               */
/* DESCRIPTION   : This function configure the mandatory parameters required     */
/*                 for pktgen utility                                            */
/*                                                                               */
/* INPUT         : config - pointer to the pktgen parameters                     */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : TRUE / FALSE                                                  */
/*                                                                               */
/*********************************************************************************/

static int configurePktgen(pktGenConfig* config)
{
        char command[BUFF_LEN_MAX];
        char result[BUFF_LEN_MAX];

        memset(command,0,BUFF_LEN_MAX);
        memset(result,0,BUFF_LEN_MAX);

        // Reset pktgen
        snprintf(command,BUFF_LEN_MAX,"echo \"reset\" >> %s",PKTGEN_CNTRL_FILE);
        executeCommand(command,result);

        //Add device interface
        memset(command,0,BUFF_LEN_MAX);
        snprintf(command,BUFF_LEN_MAX,"echo \"add_device %s\" >> %s",config->wlanInterface,PKTGEN_THREAD_FILE_0);
        executeCommand(command,result);

        // Set q_map_min
        memset(command,0,BUFF_LEN_MAX);
        snprintf(command,BUFF_LEN_MAX,"echo \"queue_map_min 2\" >> %s%s",PKTGEN_DEVICE_FILE, config->wlanInterface );
        executeCommand(command,result);

        // Set q_map_max
        memset(command,0,BUFF_LEN_MAX);
        snprintf(command,BUFF_LEN_MAX,"echo \"queue_map_max 2\" >> %s%s",PKTGEN_DEVICE_FILE, config->wlanInterface);
        executeCommand(command,result);

        // Set count 0
        memset(command,0,BUFF_LEN_MAX);
        snprintf(command,BUFF_LEN_MAX,"echo \"count 0\" >> %s%s",PKTGEN_DEVICE_FILE, config->wlanInterface );
        executeCommand(command,result);

        // Set pkt_size
        memset(command,0,BUFF_LEN_MAX);
        snprintf(command,BUFF_LEN_MAX,"echo \"pkt_size %d \" >> %s%s",config->packetSize, PKTGEN_DEVICE_FILE, config->wlanInterface);
        executeCommand(command,result);

        CcspWifiTrace(("RDK_LOG_DEBUG, Pkt gen control file %s Pkt gen device file %s\n", PKTGEN_CNTRL_FILE, PKTGEN_DEVICE_FILE));
        CcspWifiTrace(("RDK_LOG_DEBUG, %s:%d Configured pktgen with configs {Interface:%s,\t queue_map_min:2,\t queue_map_max:2,\t count:0,\t pkt_size:%d}\n",
           __FUNCTION__, __LINE__, config->wlanInterface, config->packetSize));
        return 1;
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : getCurrentTimeInMicroSeconds                                  */
/*                                                                               */
/* DESCRIPTION   : This function returns the current time in micro seconds       */
/*                                                                               */
/* INPUT         : NONE                                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : timestamp in micro seconds                                    */
/*                                                                               */
/*********************************************************************************/

unsigned long getCurrentTimeInMicroSeconds()
{
        struct timeval timer_usec;
        long long int timestamp_usec; /* timestamp in microsecond */

        if (!gettimeofday(&timer_usec, NULL)) {
                timestamp_usec = ((long long int) timer_usec.tv_sec) * 1000000ll +
                        (long long int) timer_usec.tv_usec;
        }
        else {
                timestamp_usec = -1;
        }
        return timestamp_usec;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : isVapEnabled                                                  */
/*                                                                               */
/* DESCRIPTION   : This function checks whether AP is enabled or not             */
/*                                                                               */
/* INPUT         : wlanIndex - AP index                                          */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : TRUE / FALSE                                                  */
/*                                                                               */
/*********************************************************************************/

int isVapEnabled (int wlanIndex)
{
        BOOL enabled=FALSE;

        DEBUG_PRINT (("Call wifi_getApEnable\n"));
        wifi_getApEnable(wlanIndex, &enabled);

        if (enabled == FALSE)
        {
                wifi_dbg_print (1, "ERROR> Wifi AP Not enabled for Index: %d\n", wlanIndex );
                return -1;
        }

        return 0;
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : WaitForDuration                                               */
/*                                                                               */
/* DESCRIPTION   : This function makes the calling thread to wait for particular */
/*                 time interval                                                 */
/*                                                                               */
/* INPUT         : timeInMs - time to wait                                       */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : TRUE / FALSE                                                  */
/*                                                                               */
/*********************************************************************************/

int WaitForDuration (int timeInMs)
{
        struct timespec   ts;
        struct timeval    tp;
        pthread_cond_t      cond  = PTHREAD_COND_INITIALIZER;
        pthread_mutex_t     mutex = PTHREAD_MUTEX_INITIALIZER;
        int     ret;

        gettimeofday(&tp, NULL);

        /* Convert from timeval to timespec */
        ts.tv_sec  = tp.tv_sec;
        ts.tv_nsec = tp.tv_usec * 1000;

        /* Add wait duration*/
        if ( timeInMs > 1000 )
        {
                ts.tv_sec += (timeInMs/1000);
        }
        else
        {
                ts.tv_nsec = ts.tv_nsec + (timeInMs*CONVERT_MILLI_TO_NANO);
                ts.tv_sec = ts.tv_sec + ts.tv_nsec / 1000000000L;
                ts.tv_nsec = ts.tv_nsec % 1000000000L;
        }
        pthread_mutex_lock(&mutex);
        ret = pthread_cond_timedwait(&cond, &mutex, &ts);
        pthread_mutex_unlock(&mutex);

        return ret;
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : pktGen_BlastClient                                            */
/*                                                                               */
/* DESCRIPTION   : This function uses the pktgen utility and calculates the      */
/*                 throughput                                                    */
/*                                                                               */
/* INPUT         : vargp - ptr to variable arguments                             */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void pktGen_BlastClient ()
{
        unsigned int SampleCount = 0;
        unsigned long DiffsamplesAck = 0, Diffsamples = 0, TotalAckSamples = 0, TotalSamples = 0, totalduration = 0;
        wifi_associated_dev3_t dev_conn;
        double  tp, AckRate, AckSum = 0, Rate, Sum = 0, AvgAckThroughput, AvgThroughput;
        char    s_mac[MIN_MAC_LEN+1];
        int index = g_active_msmt.curStepData.ApIndex;
        pthread_attr_t  Attr;

        CcspWifiTrace(("RDK_LOG_DEBUG, %s:%d Start pktGen utility and analyse received samples for active clients [%02x%02x%02x%02x%02x%02x]\n",
            __FUNCTION__, __LINE__,  g_active_msmt.curStepData.DestMac[0], g_active_msmt.curStepData.DestMac[1],
            g_active_msmt.curStepData.DestMac[2], g_active_msmt.curStepData.DestMac[3],
            g_active_msmt.curStepData.DestMac[4], g_active_msmt.curStepData.DestMac[5]));

        snprintf(s_mac, MIN_MAC_LEN+1, "%02x%02x%02x%02x%02x%02x", g_active_msmt.curStepData.DestMac[0],
          g_active_msmt.curStepData.DestMac[1],g_active_msmt.curStepData.DestMac[2], g_active_msmt.curStepData.DestMac[3],
            g_active_msmt.curStepData.DestMac[4], g_active_msmt.curStepData.DestMac[5]);

        if ( index >= 0)
        {
#if defined (DUAL_CORE_XB3)
#ifdef WIFI_HAL_VERSION_3
            wifi_setClientDetailedStatisticsEnable(getRadioIndexFromAp(index), TRUE);
#else
            wifi_setClientDetailedStatisticsEnable((index % 2), TRUE);
#endif

#endif
            pthread_attr_init(&Attr);
            pthread_attr_setdetachstate(&Attr, PTHREAD_CREATE_DETACHED);
            /* spawn a thread to start the packetgen as this will trigger multiple threads which will hang the calling thread*/
            wifi_dbg_print (1, "%s : %d spawn a thread to start the packetgen\n",__func__,__LINE__);
            if (pthread_create(&startpkt_thread_id, &Attr, startWifiBlast, NULL) != 0) {
                CcspTraceError(("%s:%d: Failed to spawn thread to start the packet gen\n", __FUNCTION__, __LINE__));
            } else {
                CcspWifiTrace(("RDK_LOG_INFO %s:%d Created thread to start packet gen\n", __FUNCTION__, __LINE__));
            }
            pthread_attr_destroy(&Attr);
        }
        else
        {
            CcspWifiTrace(("RDK_LOG_DEBUG, %s : %d no need to start pktgen for offline client %s\n" ,__FUNCTION__, __LINE__, s_mac));
            wifi_dbg_print (1, "%s : %d no need to start pktgen for offline client %s\n",__func__,__LINE__,s_mac);
        }

#if (!defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_))
        int waittime = config.sendDuration;
#endif

        /* allocate memory for the dynamic variables */
        g_active_msmt.active_msmt_data = (active_msmt_data_t *) calloc ((config.packetCount+1), sizeof(active_msmt_data_t));

        if (g_active_msmt.active_msmt_data == NULL)
        {
            wifi_dbg_print (1, "%s : %d  ERROR> Memory allocation failed for active_msmt_data\n",__func__,__LINE__);
            if (index >= 0)
            {
#if defined (DUAL_CORE_XB3)
#ifdef WIFI_HAL_VERSION_3
                wifi_setClientDetailedStatisticsEnable(getRadioIndexFromAp(index), FALSE);
#else
                wifi_setClientDetailedStatisticsEnable((index % 2), FALSE);
#endif
#endif
            }
            CcspTraceError(("%s:%d ERROR: Failed to allocate memory for active_msmt_data\n", __FUNCTION__, __LINE__));
            return;
        }

        /* sampling */
        while ( SampleCount < (config.packetCount + 1))
        {
            memset(&dev_conn, 0, sizeof(wifi_associated_dev3_t));

#if (!defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_))
            wifi_dbg_print(1,"%s : %d WIFI_HAL enabled, calling wifi_getApAssociatedClientDiagnosticResult with mac : %s\n",__func__,__LINE__,s_mac);
            CcspWifiTrace(("RDK_LOG_DEBUG, %s-%d WIFI_HAL enabled, calling wifi_getApAssociatedClientDiagnosticResult with mac : %s for sampling process",
                   __FUNCTION__, __LINE__, s_mac));

            unsigned long start = getCurrentTimeInMicroSeconds ();
            WaitForDuration ( waittime );

            if (index >= 0)
	    {
		    if (wifi_getApAssociatedClientDiagnosticResult(index, s_mac, &dev_conn) == RETURN_OK)
		    {

			    frameCountSample[SampleCount].WaitAndLatencyInMs = ((getCurrentTimeInMicroSeconds () - start) / 1000);
			    wifi_dbg_print(1, "PKTGEN_WAIT_IN_MS duration : %lu\n", ((getCurrentTimeInMicroSeconds () - start)/1000));
                            CcspWifiTrace(("RDK_LOG_DEBUG,  PKTGEN_WAIT_IN_MS duration : %lu\n", ((getCurrentTimeInMicroSeconds () - start)/1000)));

			    g_active_msmt.active_msmt_data[SampleCount].rssi = dev_conn.cli_RSSI;
			    g_active_msmt.active_msmt_data[SampleCount].TxPhyRate = dev_conn.cli_LastDataDownlinkRate;
			    g_active_msmt.active_msmt_data[SampleCount].RxPhyRate = dev_conn.cli_LastDataUplinkRate;
			    g_active_msmt.active_msmt_data[SampleCount].SNR = dev_conn.cli_SNR;
			    g_active_msmt.active_msmt_data[SampleCount].ReTransmission = dev_conn.cli_Retransmissions;
			    g_active_msmt.active_msmt_data[SampleCount].MaxTxRate = dev_conn.cli_MaxDownlinkRate;
			    g_active_msmt.active_msmt_data[SampleCount].MaxRxRate = dev_conn.cli_MaxUplinkRate;
			    strncpy(g_active_msmt.active_msmt_data[SampleCount].Operating_channelwidth, dev_conn.cli_OperatingChannelBandwidth,OPER_BUFFER_LEN);

			    frameCountSample[SampleCount].PacketsSentAck = dev_conn.cli_DataFramesSentAck;
			    frameCountSample[SampleCount].PacketsSentTotal = dev_conn.cli_PacketsSent + dev_conn.cli_DataFramesSentNoAck;
                            if (strstr(dev_conn.cli_OperatingStandard, "802.11") != NULL)
                            {
                                sscanf(dev_conn.cli_OperatingStandard, "802.11%s", g_active_msmt.active_msmt_data[SampleCount].Operating_standard);
                            }
                            else
                            {
			        strncpy(g_active_msmt.active_msmt_data[SampleCount].Operating_standard, dev_conn.cli_OperatingStandard,OPER_BUFFER_LEN);
                            }
			    wifi_dbg_print(1,"samplecount[%d] : PacketsSentAck[%lu] PacketsSentTotal[%lu]"
					    " WaitAndLatencyInMs[%d ms] RSSI[%d] TxRate[%lu Mbps] RxRate[%lu Mbps] SNR[%d]"
					    "chanbw [%s] standard [%s] MaxTxRate[%d] MaxRxRate[%d]\n",
					    SampleCount, dev_conn.cli_DataFramesSentAck, (dev_conn.cli_PacketsSent + dev_conn.cli_DataFramesSentNoAck),
					    frameCountSample[SampleCount].WaitAndLatencyInMs, dev_conn.cli_RSSI, dev_conn.cli_LastDataDownlinkRate, dev_conn.cli_LastDataUplinkRate, dev_conn.cli_SNR,g_active_msmt.active_msmt_data[SampleCount].Operating_channelwidth ,g_active_msmt.active_msmt_data[SampleCount].Operating_standard,g_active_msmt.active_msmt_data[SampleCount].MaxTxRate, g_active_msmt.active_msmt_data[SampleCount].MaxRxRate);
		    }
		    else
		    {
			    wifi_dbg_print(1,"%s : %d wifi_getApAssociatedClientDiagnosticResult failed for mac : %s\n",__func__,__LINE__,s_mac);
                            CcspTraceError(("%s:%d wifi_getApAssociatedClientDiagnosticResult failed for mac : %s\n", __FUNCTION__, __LINE__, s_mac));
			    frameCountSample[SampleCount].PacketsSentAck = 0;
			    frameCountSample[SampleCount].PacketsSentTotal = 0;
			    frameCountSample[SampleCount].WaitAndLatencyInMs = 0;
		    }
	    }
            else
            {
                wifi_dbg_print(1,"%s : %d client is offline so setting the default values.\n",__func__,__LINE__);
                CcspWifiTrace(("RDK_LOG_DEBUG, client is offline so setting the default values.\n"));
                frameCountSample[SampleCount].PacketsSentAck = 0;
                frameCountSample[SampleCount].PacketsSentTotal = 0;
                frameCountSample[SampleCount].WaitAndLatencyInMs = 0;
                strncpy(g_active_msmt.active_msmt_data[SampleCount].Operating_standard, "NULL",OPER_BUFFER_LEN);
                strncpy(g_active_msmt.active_msmt_data[SampleCount].Operating_channelwidth, "NULL",OPER_BUFFER_LEN);
            }
#endif
                SampleCount++;
        }

#if defined (DUAL_CORE_XB3)
        if (index >= 0)
        {
#ifdef WIFI_HAL_VERSION_3
            wifi_setClientDetailedStatisticsEnable(getRadioIndexFromAp(index), FALSE);
#else
            wifi_setClientDetailedStatisticsEnable((index % 2), FALSE);
#endif
        }
#endif
        // Analyze samples and get Throughput
        for (SampleCount=0; SampleCount < config.packetCount; SampleCount++)
        {
                DiffsamplesAck = frameCountSample[SampleCount+1].PacketsSentAck - frameCountSample[SampleCount].PacketsSentAck;
                Diffsamples = frameCountSample[SampleCount+1].PacketsSentTotal - frameCountSample[SampleCount].PacketsSentTotal;

                tp = (double)(DiffsamplesAck*8*config.packetSize);              //number of bits
                wifi_dbg_print(1,"tp = [%f bits]\n", tp );
                tp = tp/1000000;                //convert to Mbits
                wifi_dbg_print(1,"tp = [%f Mb]\n", tp );
                AckRate = (tp/frameCountSample[SampleCount+1].WaitAndLatencyInMs) * 1000;                        //calculate bitrate in the unit of Mbpms

                tp = (double)(Diffsamples*8*config.packetSize);         //number of bits
                wifi_dbg_print(1,"tp = [%f bits]\n", tp );
                tp = tp/1000000;                //convert to Mbits
                wifi_dbg_print(1,"tp = [%f Mb]\n", tp );
                Rate = (tp/frameCountSample[SampleCount+1].WaitAndLatencyInMs) * 1000;                   //calculate bitrate in the unit of Mbpms

                /* updating the throughput in the global variable */
                g_active_msmt.active_msmt_data[SampleCount].throughput = AckRate;

                wifi_dbg_print(1,"Sample[%d]   DiffsamplesAck[%lu]   Diffsamples[%lu]   BitrateAckPackets[%.5f Mbps]   BitrateTotalPackets[%.5f Mbps]\n", SampleCount, DiffsamplesAck, Diffsamples, AckRate, Rate );
                AckSum += AckRate;
                Sum += Rate;
                TotalAckSamples += DiffsamplesAck;
                TotalSamples += Diffsamples;

                totalduration += frameCountSample[SampleCount+1].WaitAndLatencyInMs;
        }
        AvgAckThroughput = AckSum/(config.packetCount);
        AvgThroughput = Sum/(config.packetCount);
        wifi_dbg_print(1,"\nTotal number of ACK Packets = %lu   Total number of Packets = %lu   Total Duration = %lu ms\n", TotalAckSamples, TotalSamples, totalduration );
        wifi_dbg_print(1,"Calculated Average : ACK Packets Throughput[%.2lf Mbps]  Total Packets Throughput[%.2lf Mbps]\n\n", AvgAckThroughput, AvgThroughput );
        CcspWifiTrace(("RDK_LOG_DEBUG, Total number of ACK Packets = %lu   Total number of Packets = %lu   Total Duration = %lu ms\n", TotalAckSamples, TotalSamples, totalduration));
        CcspWifiTrace(("RDK_LOG_DEBUG, Calculated Average : ACK Packets Throughput[%.2lf Mbps]  Total Packets Throughput[%.2lf Mbps]\n", AvgAckThroughput, AvgThroughput));

        return;
}

/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : WiFiBlastClient                                               */
/*                                                                               */
/* DESCRIPTION   : This function starts the active measurement process to        */
/*                 start the pktgen and to calculate the throughput for a        */
/*                 particular client                                             */
/*                                                                               */
/* INPUT         : ClientMac - MAC address of the client                         */
/*                 apIndex - AP index                                            */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/

void *WiFiBlastClient(void* data)
{
    char macStr[18] = {'\0'};
    char ChannelsInUse[256] = {'\0'};
    int radiocnt = 0;
    int total_radiocnt = 0;
    int ret = 0;
    unsigned int StepCount = 0;
    int apIndex = 0;
    unsigned int NoOfSamples = 0;
    char command[BUFF_LEN_MAX];
    char result[BUFF_LEN_MAX];
    int     oldcanceltype;

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldcanceltype);

    UNREFERENCED_PARAMETER(data);
    NoOfSamples = GetActiveMsmtNumberOfSamples();
    /* allocate memory for frameCountSample */
    frameCountSample = (pktGenFrameCountSamples *) calloc ((NoOfSamples + 1), sizeof(pktGenFrameCountSamples));

    if (frameCountSample == NULL)
    {
        wifi_dbg_print (1,"Memory allocation failed for frameCountSample\n");
    }
    /* fill the packetgen config with the incoming parameter */
    memset(&config,0,sizeof(pktGenConfig));
    config.packetSize = GetActiveMsmtPktSize();
    config.sendDuration = GetActiveMsmtSampleDuration();
    config.packetCount = NoOfSamples;

    /* Get the radio ChannelsInUse param value for all the radios */
#ifdef WIFI_HAL_VERSION_3
    total_radiocnt = (int)getNumberRadios();
#else
    if (wifi_getRadioNumberOfEntries((ULONG*)&total_radiocnt) != RETURN_OK)
    {
        wifi_dbg_print(1, "%s : %d Failed to get radio count\n",__func__,__LINE__);
        return NULL;
    }
#endif
    for (radiocnt = 0; radiocnt < (int)total_radiocnt; radiocnt++)
    {
        memset(ChannelsInUse, '\0', sizeof(ChannelsInUse));
        //wifi_getRadioChannelsInUse (radiocnt, ChannelsInUse);
        strncpy((char *)&g_monitor_module.radio_data[radiocnt].ChannelsInUse, ChannelsInUse,sizeof(ChannelsInUse));
    }
    for (StepCount = 0; StepCount < MAX_STEP_COUNT; StepCount++)
    {
        if(g_active_msmt.active_msmt.StepInstance[StepCount] != 0)
        {
            wifi_dbg_print (1,"%s : %d processing StepCount : %d \n",__func__,__LINE__,StepCount);
            apIndex = g_active_msmt.active_msmt.Step[StepCount].ApIndex;

            if (apIndex >= 0)
            {
                if ( isVapEnabled (apIndex) != 0 )
                {
                    wifi_dbg_print (1, "ERROR running wifiblaster: Init Failed\n" );
                    continue;
                }

                memset(config.wlanInterface, '\0', sizeof(config.wlanInterface));
                /*CID: 160057 Out-of-bounds access- updated BUFF_LEN_MIN 64*/
                wifi_getApName(apIndex, config.wlanInterface);
            }
            else
            {
                wifi_dbg_print (1, "(%s) invalid apIndex=%d\n", __func__, apIndex);
                CcspWifiTrace(("RDK_LOG_ERROR, Client is not there on this VAP, abort the blast for this client and move on to the next client\n"));
                continue;
            }

            /*TODO RDKB-34680 CID: 154402,154401  Data race condition*/
            g_active_msmt.curStepData.ApIndex = apIndex;
            g_active_msmt.curStepData.StepId = g_active_msmt.active_msmt.Step[StepCount].StepId;
            memcpy(g_active_msmt.curStepData.DestMac, g_active_msmt.active_msmt.Step[StepCount].DestMac, sizeof(mac_address_t));

            wifi_dbg_print (1,"%s : %d copied mac address %02x:%02x:%02x:%02x:%02x:%02x to current step info\n",__func__,__LINE__,g_active_msmt.curStepData.DestMac[0],g_active_msmt.curStepData.DestMac[1],g_active_msmt.curStepData.DestMac[2],g_active_msmt.curStepData.DestMac[3],g_active_msmt.curStepData.DestMac[4],g_active_msmt.curStepData.DestMac[5]);

            snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                     g_active_msmt.curStepData.DestMac[0], g_active_msmt.curStepData.DestMac[1],
                     g_active_msmt.curStepData.DestMac[2], g_active_msmt.curStepData.DestMac[3],
                     g_active_msmt.curStepData.DestMac[4], g_active_msmt.curStepData.DestMac[5]);

            //check if the client is still associated to that vap index before blast
			char *macstr1 = macStr;
			CcspWifiTrace(("RDK_LOG_INFO, sleeping for 10 secs, client mac  %s\n", macstr1));
			if(access("/tmp/blaster_induce_sleep", R_OK) == 0)
			{
				sleep(10);
				/* this sleep is only for QA to validate, in field scenarios, we will not sleep
				we will automatically detect if we are getting a notification from
				driver on steering / roaming / offline client */
			}

			if ( is_device_associated (apIndex, macstr1) == true )
			{
                CcspWifiTrace(("RDK_LOG_INFO, Client is still connected to this AP, let's continue to blast\n"));
			}
			else
			{
			 	CcspWifiTrace(("RDK_LOG_INFO, Client is not there on this VAP, abort the blast for this client and move on to the next client\n"));
				continue;
			}


			CcspWifiTrace(("RDK_LOG_INFO, Blaster test is initiated for Dest mac [%s]\n", macStr));
            CcspWifiTrace(("RDK_LOG_INFO, Interface [%s], Send Duration: [%d msecs], Packet Size: [%d bytes], Sample count: [%d]\n",
                config.wlanInterface, config.sendDuration, config.packetSize, config.packetCount));

            wifi_dbg_print (1, "\n=========START THE TEST=========\n");
            wifi_dbg_print(1,"Interface [%s], Send Duration: [%d msecs], Packet Size: [%d bytes], Sample count: [%d]\n",
                   config.wlanInterface,config.sendDuration,config.packetSize,config.packetCount);

            /* no need to configure pktgen for offline clients */
            if (apIndex >= 0)
            {
                /* configure pktgen based on given Arguments */
                configurePktgen(&config);

                /* configure the MAC address in the pktgen file */
                memset(command,0,BUFF_LEN_MAX);
                snprintf(command,BUFF_LEN_MAX,"echo \"dst_mac %s\" >> %s%s",macStr,PKTGEN_DEVICE_FILE,config.wlanInterface );
                executeCommand(command,result);
            }

            /* start blasting the packets to calculate the throughput */
             pktGen_BlastClient();

            /* no need to kill pktgen for offline clients */
            if (apIndex >= 0)
            {
                if (startpkt_thread_id != 0)
                {
                    ret = pthread_cancel(startpkt_thread_id);
                }
    
                if ( ret == 0) {
                    wifi_dbg_print (1,"startpkt_thread_id is killed\n");
                    CcspWifiTrace(("RDK_LOG_DEBUG, startpkt_thread_id is killed\n"));
                } else {
                    wifi_dbg_print (1,"pthread_kill returns error : %d\n", ret);
                    CcspWifiTrace(("RDK_LOG_DEBUG, pthread_cance returns error : %d errno :%d - %s\n", ret, errno, strerror(errno)));
                }
    
                /* stop blasting */
                StopWifiBlast ();
            }

            /* calling process_active_msmt_diagnostics to update the station info */
            wifi_dbg_print(1, "%s : %d calling process_active_msmt_diagnostics\n",__func__,__LINE__);
            CcspWifiTrace(("RDK_LOG_DEBUG, %s-%d: calling process_active_msmt_diagnostics to update the station info\n", __FUNCTION__, __LINE__));
            process_active_msmt_diagnostics(apIndex);

            /* calling stream_client_msmt_data to upload the data to AVRO schema */
            wifi_dbg_print(1, "%s : %d calling stream_client_msmt_data\n",__func__,__LINE__);
            CcspWifiTrace(("RDK_LOG_DEBUG, %s : %d calling stream_client_msmt_data\n", __FUNCTION__, __LINE__));
            stream_client_msmt_data(true);

            wifi_dbg_print(1, "%s : %d updated stepIns to 0 for step : %d\n",__func__,__LINE__,StepCount);
            g_active_msmt.active_msmt.StepInstance[StepCount] = 0;
        }
        if (!g_active_msmt.active_msmt.ActiveMsmtEnable)
        {
            for (StepCount = StepCount+1; StepCount < MAX_STEP_COUNT; StepCount++)
            {
                g_active_msmt.active_msmt.StepInstance[StepCount] = 0;
            }
            CcspWifiTrace(("RDK_LOG_INFO, ActiveMsmtEnable changed from TRUE to FALSE"
                  "Setting remaining [%d] step count to 0 and STOPPING further processing\n",
                  (MAX_STEP_COUNT - StepCount)));
            break;
        }
    }
    if (frameCountSample != NULL)
    {
        wifi_dbg_print(1, "%s : %d freeing memory for frameCountSample \n",__func__,__LINE__);
        free(frameCountSample);
        frameCountSample = NULL;
    }
    g_monitor_module.blastReqInQueueCount--;
    wifi_dbg_print(1, "%s : %d decrementing blastReqInQueueCount to %d\n",__func__,__LINE__,g_monitor_module.blastReqInQueueCount);

    wifi_dbg_print(1, "%s : %d exiting the function\n",__func__,__LINE__);
    return NULL;
}
/*********************************************************************************/
/*                                                                               */
/* FUNCTION NAME : process_active_msmt_diagnostics                               */
/*                                                                               */
/* DESCRIPTION   : This function update the station info with the global monitor */
/*                 data info which gets uploaded to the AVRO schema              */
/*                                                                               */
/* INPUT         : ap_index - AP index                                           */
/*                                                                               */
/* OUTPUT        : NONE                                                          */
/*                                                                               */
/* RETURN VALUE  : NONE                                                          */
/*                                                                               */
/*********************************************************************************/
void process_active_msmt_diagnostics (int ap_index)
{
    hash_map_t     *sta_map;
    sta_data_t *sta;
    sta_key_t       sta_key;
    unsigned int count = 0;

    wifi_dbg_print(1, "%s : %d  apindex : %d \n",__func__,__LINE__,ap_index);

    /* changing the ApIndex to 0 since for offline client the ApIndex will be -1.
       with ApIndex as -1 the sta_map will fail to fetch the value which result in crash
     */
    if (ap_index == -1)
    {
        ap_index = 0;
        wifi_dbg_print(1, "%s : %d  changed ap_index to : %d \n",__func__,__LINE__,ap_index);
    }
    sta_map = g_monitor_module.bssid_data[ap_index].sta_map;

    sta = (sta_data_t *)hash_map_get(sta_map, to_sta_key(g_active_msmt.curStepData.DestMac, sta_key));

    if (sta == NULL)
    {
        /* added the data in sta map for offline clients */
        wifi_dbg_print(1, "%s : %d station info is null \n",__func__,__LINE__);
        sta = (sta_data_t *) malloc (sizeof(sta_data_t));
        memset(sta, 0, sizeof(sta_data_t));
        pthread_mutex_lock(&g_monitor_module.lock);
        memcpy(sta->sta_mac, g_active_msmt.curStepData.DestMac, sizeof(mac_addr_t));
        sta->updated = true;
        //Set cli_Active to false for offline clients. or remove the line as it will be updated to false by process_diagnostics function
        sta->dev_stats.cli_Active = false;
        hash_map_put(sta_map, strdup(to_sta_key(g_active_msmt.curStepData.DestMac, sta_key)), sta);
        memcpy(&sta->dev_stats.cli_MACAddress, g_active_msmt.curStepData.DestMac, sizeof(mac_addr_t));
        pthread_mutex_unlock(&g_monitor_module.lock);
    }
    else
    {
        wifi_dbg_print(1, "%s : %d copying mac : %02x:%02x:%02x:%02x:%02x:%02x to station info \n",__func__,__LINE__,
              g_active_msmt.curStepData.DestMac[0], g_active_msmt.curStepData.DestMac[1], g_active_msmt.curStepData.DestMac[2], g_active_msmt.curStepData.DestMac[3], g_active_msmt.curStepData.DestMac[4], g_active_msmt.curStepData.DestMac[5]);
        memcpy(&sta->dev_stats.cli_MACAddress, g_active_msmt.curStepData.DestMac, sizeof(mac_addr_t));
    }
    wifi_dbg_print(1, "%s : %d allocating memory for sta_active_msmt_data \n",__func__,__LINE__);
    sta->sta_active_msmt_data = (active_msmt_data_t *) calloc (g_active_msmt.active_msmt.ActiveMsmtNumberOfSamples,sizeof(active_msmt_data_t));

    if (sta->sta_active_msmt_data == NULL)
    {
       wifi_dbg_print(1, "%s : %d allocating memory for sta_active_msmt_data failed\n",__func__,__LINE__);
       /*CID: 146766 Dereference after null check*/
       return;
    }

    CcspWifiTrace(("RDK_LOG_DEBUG, %s:%d Number of sample %d for client [%02x:%02x:%02x:%02x:%02x:%02x]\n", __FUNCTION__, __LINE__,
         g_active_msmt.active_msmt.ActiveMsmtNumberOfSamples, g_active_msmt.curStepData.DestMac[0], g_active_msmt.curStepData.DestMac[1],
         g_active_msmt.curStepData.DestMac[2], g_active_msmt.curStepData.DestMac[3], g_active_msmt.curStepData.DestMac[4], g_active_msmt.curStepData.DestMac[5]));

    for (count = 0; count < g_active_msmt.active_msmt.ActiveMsmtNumberOfSamples; count++)
    {
        sta->sta_active_msmt_data[count].rssi = g_active_msmt.active_msmt_data[count].rssi;
        sta->sta_active_msmt_data[count].TxPhyRate = g_active_msmt.active_msmt_data[count].TxPhyRate;
        sta->sta_active_msmt_data[count].RxPhyRate = g_active_msmt.active_msmt_data[count].RxPhyRate;
        sta->sta_active_msmt_data[count].SNR = g_active_msmt.active_msmt_data[count].SNR;
        sta->sta_active_msmt_data[count].ReTransmission = g_active_msmt.active_msmt_data[count].ReTransmission;
        sta->sta_active_msmt_data[count].MaxRxRate = g_active_msmt.active_msmt_data[count].MaxRxRate;
        sta->sta_active_msmt_data[count].MaxTxRate = g_active_msmt.active_msmt_data[count].MaxTxRate;
        strncpy(sta->sta_active_msmt_data[count].Operating_standard, g_active_msmt.active_msmt_data[count].Operating_standard,OPER_BUFFER_LEN);
        strncpy(sta->sta_active_msmt_data[count].Operating_channelwidth, g_active_msmt.active_msmt_data[count].Operating_channelwidth,OPER_BUFFER_LEN);
        sta->sta_active_msmt_data[count].throughput = g_active_msmt.active_msmt_data[count].throughput;

        wifi_dbg_print(1,"count[%d] : standard[%s] chan_width[%s] Retransmission [%d]"
             "RSSI[%d] TxRate[%lu Mbps] RxRate[%lu Mbps] SNR[%d] throughput[%.5lf Mbps]"
             "MaxTxRate[%d] MaxRxRate[%d]\n",
             count, sta->sta_active_msmt_data[count].Operating_standard,
             sta->sta_active_msmt_data[count].Operating_channelwidth,
             sta->sta_active_msmt_data[count].ReTransmission,
             sta->sta_active_msmt_data[count].rssi, sta->sta_active_msmt_data[count].TxPhyRate,
             sta->sta_active_msmt_data[count].RxPhyRate, sta->sta_active_msmt_data[count].SNR,
             sta->sta_active_msmt_data[count].throughput,
             sta->sta_active_msmt_data[count].MaxTxRate,
             sta->sta_active_msmt_data[count].MaxRxRate);

       CcspWifiTrace(("RDK_LOG_DEBUG, Sampled data - [%d] : {standard[%s],\t chan_width[%s]\t"
           "Retransmission [%d]\t RSSI[%d]\t TxRate[%lu Mbps]\t RxRate[%lu Mbps]\t SNR[%d]\t"
           "throughput[%.5lf Mbps]\t MaxTxRate[%d]\t MaxRxRate[%d]\n}\n",
           count, sta->sta_active_msmt_data[count].Operating_standard,
             sta->sta_active_msmt_data[count].Operating_channelwidth,
             sta->sta_active_msmt_data[count].ReTransmission,
             sta->sta_active_msmt_data[count].rssi, sta->sta_active_msmt_data[count].TxPhyRate,
             sta->sta_active_msmt_data[count].RxPhyRate, sta->sta_active_msmt_data[count].SNR,
             sta->sta_active_msmt_data[count].throughput,
             sta->sta_active_msmt_data[count].MaxTxRate,
             sta->sta_active_msmt_data[count].MaxRxRate));
     }

    /* free the g_active_msmt.active_msmt_data allocated memory */
    if (g_active_msmt.active_msmt_data != NULL)
    {
        wifi_dbg_print(1, "%s : %d memory freed for g_active_msmt.active_msmt_data\n",__func__,__LINE__);
        free(g_active_msmt.active_msmt_data);
        g_active_msmt.active_msmt_data = NULL;
    }
    wifi_dbg_print(1, "%s : %d exiting the function\n",__func__,__LINE__);
}

