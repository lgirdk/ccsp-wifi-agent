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


#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <rbus/rbus.h>
#include <signal.h>
#include <wifi_hal.h>
#include <collection.h>
#include <wifi_monitor.h>
#include <sys/time.h>
#include <sys/stat.h>

#define MAX_EVENTS 8
#define DEFAULT_CSI_INTERVAL 500
#define DEFAULT_CLIENTDIAG_INTERVAL 5000
#define MAX_CSI_INTERVAL    30000
#define MIN_CSI_INTERVAL    100
#define DEFAULT_DBG_FILE "/tmp/wifiEventConsumer"
#ifndef  UNREFERENCED_PARAMETER
 #define UNREFERENCED_PARAMETER(_p_)         (void)(_p_)
#endif

#define WIFI_EVENT_CONSUMER_DGB(msg, ...) \
    wifievents_consumer_dbg_print("%s:%d  "msg"\n", __func__, __LINE__, ##__VA_ARGS__);

FILE *g_fpg = NULL;

char g_component_name[RBUS_MAX_NAME_LENGTH];
char g_debug_file_name[RBUS_MAX_NAME_LENGTH];
int g_pid;

rbusHandle_t g_handle;
rbusEventSubscription_t *g_all_subs = NULL;
int g_sub_total = 0;

int g_events_list[MAX_EVENTS];
int g_events_cnt = 0;
int g_vaps_list[MAX_VAP];
int g_vaps_cnt = 0;
int g_csi_interval = 0;
bool g_csi_session_set = false;
uint32_t g_csi_index = 0;
int g_clientdiag_interval = 0;
int g_max_vaps = 0;

static void wifievents_get_max_vaps() {
    char const*     paramNames[] = {"Device.WiFi.SSIDNumberOfEntries"};
    rbusValue_t value;
    int rc = RBUS_ERROR_SUCCESS;

    rc = rbus_get(g_handle, paramNames[0], &value);
    if(rc != RBUS_ERROR_SUCCESS)
    {
        printf ("rbus_get failed for [%s] with error [%d]\n", paramNames[0], rc);
        return;
    }
    g_max_vaps = rbusValue_GetUInt32(value);
}

static void wifievents_consumer_dbg_print(char *format, ...)
{
    char buff[2048] = {0};
    va_list list;

    if ((access("/nvram/wifiEventConsumerDbg", R_OK)) != 0)
    {
        return;
    }

    snprintf(buff, 12, " pid:%d ", g_pid);

    va_start(list, format);
    vsprintf(&buff[strlen(buff)], format, list);
    va_end(list);

    if (g_fpg == NULL)
    {
        g_fpg = fopen(g_debug_file_name, "a+");
        if (g_fpg == NULL)
        {
            printf("Failed to open file\n");
            return;
        }
        else
        {
            fputs(buff, g_fpg);
        }
    }
    else
    {
        fputs(buff, g_fpg);
    }
    fflush(g_fpg);
}

static void diagHandler(rbusHandle_t handle, rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    rbusValue_t value;
    int vap;

    if(!event || (sscanf(subscription->eventName,
        "Device.WiFi.AccessPoint.%d.X_RDK_DiagData", &vap) != 1))
    {
        WIFI_EVENT_CONSUMER_DGB("Invalid Event Received %s", subscription->eventName);
        return;
    }
    value = rbusObject_GetValue(event->data, subscription->eventName);
    if (value)
    {
        WIFI_EVENT_CONSUMER_DGB("VAP %d Device Diag Data '%s'\n", vap, rbusValue_GetString(value, NULL));
    }
    UNREFERENCED_PARAMETER(handle);
}

static void deviceConnectHandler(rbusHandle_t handle, rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    rbusValue_t value;
    int vap, len;
    uint8_t const *data_ptr;
    mac_addr_t  sta_mac;

    if(!event || (sscanf(subscription->eventName,
        "Device.WiFi.AccessPoint.%d.X_RDK_deviceConnected", &vap) != 1))
    {
        WIFI_EVENT_CONSUMER_DGB("Invalid Event Received %s %d %p", subscription->eventName, vap, event);
        return;
    }

    value = rbusObject_GetValue(event->data, subscription->eventName);
    if (value)
    {
        data_ptr = rbusValue_GetBytes(value, &len);
        if(data_ptr != NULL && len == sizeof(mac_addr_t))
        {
            memcpy(&sta_mac, data_ptr, sizeof(mac_addr_t));
            WIFI_EVENT_CONSUMER_DGB("Device %02x:%02x:%02x:%02x:%02x:%02x connected to VAP %d\n", sta_mac[0], sta_mac[1], sta_mac[2], 
                                                                            sta_mac[3], sta_mac[4], sta_mac[5], vap);
        }
        else
        {
            WIFI_EVENT_CONSUMER_DGB("Invalid Event Data Received %s", subscription->eventName);
            return;
        }
    }
    UNREFERENCED_PARAMETER(handle);
}

static void deviceDisonnectHandler(rbusHandle_t handle, rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    rbusValue_t value;
    int vap, len;
    uint8_t const *data_ptr;
    mac_addr_t  sta_mac;

    if(!event || (sscanf(subscription->eventName,
        "Device.WiFi.AccessPoint.%d.X_RDK_deviceDisconnected", &vap) != 1))
    {
        WIFI_EVENT_CONSUMER_DGB("Invalid Event Received %s %d %p", subscription->eventName, vap, event);
        return;
    }
    value = rbusObject_GetValue(event->data, subscription->eventName);
    if (value)
    {
        data_ptr = rbusValue_GetBytes(value, &len);
        if(data_ptr != NULL && len == sizeof(mac_addr_t))
        {
            memcpy(&sta_mac, data_ptr, sizeof(mac_addr_t));
            WIFI_EVENT_CONSUMER_DGB("Device %02x:%02x:%02x:%02x:%02x:%02x disconnected from VAP %d\n", sta_mac[0], sta_mac[1], sta_mac[2], 
                                                                                sta_mac[3], sta_mac[4], sta_mac[5], vap);
        }
        else
        {
            WIFI_EVENT_CONSUMER_DGB("Invalid Event Data Received %s", subscription->eventName);
            return;
        }
    }
    UNREFERENCED_PARAMETER(handle);
}

static void deviceDeauthHandler(rbusHandle_t handle, rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    rbusValue_t value;
    int vap, len;
    uint8_t const *data_ptr;
    mac_addr_t  sta_mac;

    if(!event || (sscanf(subscription->eventName,
        "Device.WiFi.AccessPoint.%d.X_RDK_deviceDeauthenticated", &vap) != 1))
    {
        WIFI_EVENT_CONSUMER_DGB("Invalid Event Received %s", subscription->eventName);
        return;
    }

    value = rbusObject_GetValue(event->data, subscription->eventName);
    if (value)
    {
        data_ptr = rbusValue_GetBytes(value, &len);
        if(data_ptr != NULL && len == sizeof(mac_addr_t))
        {
            memcpy(&sta_mac, data_ptr, sizeof(mac_addr_t));
            WIFI_EVENT_CONSUMER_DGB("Device %02x:%02x:%02x:%02x:%02x:%02x deauthenticated from VAP %d\n", sta_mac[0], sta_mac[1], sta_mac[2], 
                                                                                sta_mac[3], sta_mac[4], sta_mac[5], vap);
        }
        else
        {
            WIFI_EVENT_CONSUMER_DGB("Invalid Event Data Received %s", subscription->eventName);
            return;
        }
    }

    UNREFERENCED_PARAMETER(handle);
}

static void statusHandler(rbusHandle_t handle, rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    rbusValue_t value;
    int vap;

    if(!event || (sscanf(subscription->eventName,
        "Device.WiFi.AccessPoint.%d.Status", &vap) != 1))
    {
        WIFI_EVENT_CONSUMER_DGB("Invalid Event Received %s", subscription->eventName);
        return;
    }

    value = rbusObject_GetValue(event->data, "value");
    if (value)
    {
        WIFI_EVENT_CONSUMER_DGB("AP %d status changed to %s", vap, rbusValue_GetString(value, NULL));
    }

    UNREFERENCED_PARAMETER(handle);
}

static void csiMacListHandler(rbusHandle_t handle, rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    rbusValue_t value;
    int csi_session;

    if(!event || (sscanf(subscription->eventName,
        "Device.WiFi.X_RDK_CSI.%d.ClientMaclist", &csi_session) != 1))
    {
        WIFI_EVENT_CONSUMER_DGB("Invalid Event Received %s", subscription->eventName);
        return;
    }

    value = rbusObject_GetValue(event->data, "value");
    if (value)
    {
        WIFI_EVENT_CONSUMER_DGB("CSI session %d MAC list changed to %s", csi_session, rbusValue_GetString(value, NULL));
    }

    UNREFERENCED_PARAMETER(handle);
}

void rotate_and_write_CSIData(mac_addr_t sta_mac, wifi_csi_data_t *csi) {
#define MB(x)   ((long int) (x) << 20)
#define CSI_FILE "/tmp/CSI.bin"
#define CSI_TMP_FILE "/tmp/CSI_tmp.bin"
    WIFI_EVENT_CONSUMER_DGB("Enter %s: %d\n", __FUNCTION__, __LINE__);
    char                filename[] = CSI_FILE;
    char                filename_tmp[] = CSI_TMP_FILE;
    FILE                *csifptr;
    FILE                *csifptr_tmp;
    struct stat         st;
    mac_addr_t          tmp_mac;
    wifi_csi_matrix_t   tmp_csi_matrix;
    wifi_frame_info_t   tmp_frame_info;

    if(csi == NULL)
        return;
    csifptr = fopen(filename,"r");
    csifptr_tmp = fopen(filename_tmp,"w");
    if(csifptr !=NULL )
    {
        //get the size of the file
        stat(filename, &st);
        if(st.st_size > MB(1)) // if file size is greate than 1 mb
        {
            mac_addr_t tmp_mac;
            wifi_frame_info_t tmp_frame_info;
            wifi_csi_matrix_t tmp_csi_matrix;

            fread(&tmp_mac, sizeof(mac_addr_t), 1, csifptr);
            fread(&tmp_frame_info, sizeof(wifi_frame_info_t), 1, csifptr);
            fread(&tmp_csi_matrix, sizeof(wifi_csi_matrix_t), 1, csifptr);
        }
        //copy rest of the content in to the temp file
        while(csifptr != NULL && fread(&tmp_mac, sizeof(mac_addr_t), 1, csifptr)) {
            fread(&tmp_frame_info, sizeof(wifi_frame_info_t), 1, csifptr);
            fread(&tmp_csi_matrix, sizeof(wifi_csi_matrix_t), 1, csifptr);
            fwrite(&tmp_mac, sizeof(mac_addr_t), 1, csifptr_tmp);
            fwrite(&tmp_frame_info, sizeof(wifi_frame_info_t), 1, csifptr_tmp);
            fwrite(&tmp_csi_matrix, sizeof(wifi_csi_matrix_t), 1, csifptr_tmp);
        }
    }

    if(csifptr_tmp!=NULL) {
        fwrite(sta_mac, sizeof(mac_addr_t), 1, csifptr_tmp);
        fwrite(&(csi->frame_info), sizeof(wifi_frame_info_t), 1, csifptr_tmp);
        fwrite(&(csi->csi_matrix), sizeof(wifi_csi_matrix_t), 1, csifptr_tmp);
    }

    if(csifptr!=NULL){
        fclose(csifptr);
        unlink(filename);
    }
    if(csifptr_tmp!=NULL){
        fclose(csifptr_tmp);
        rename(filename_tmp, filename);
    }

    WIFI_EVENT_CONSUMER_DGB("Exit %s: %d\n", __FUNCTION__, __LINE__);
}

static void csiDataHandler(rbusHandle_t handle, rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    int len;
    int csi_session;
    int itr;
    rbusValue_t csi_data;
    uint8_t const *data_ptr;
    char csilabel[4];
    unsigned int total_length, num_csi_clients, csi_data_length;
    time_t datetime;
    wifi_csi_data_t csi;
    mac_addr_t  sta_mac;
    char buf[128] = {0};

    if(!event || (sscanf(subscription->eventName,
        "Device.WiFi.X_RDK_CSI.%d.data", &csi_session) != 1))
    {
        WIFI_EVENT_CONSUMER_DGB("Invalid Event Received %s", subscription->eventName);
        return;
    }
    
    csi_data = rbusObject_GetValue(event->data, subscription->eventName);
    if(!csi_data) {
        WIFI_EVENT_CONSUMER_DGB("Invalid csi data Received");
        return;
    }

    data_ptr = rbusValue_GetBytes(csi_data, &len);
    if(!data_ptr) {
        WIFI_EVENT_CONSUMER_DGB("Invalid csi data Received");
        return;
    }

    //ASCII characters "CSI"
    memcpy(csilabel, data_ptr, 4);
    data_ptr = data_ptr + 4;
    WIFI_EVENT_CONSUMER_DGB("%s\n", csilabel);

    //Total length:  <length of this entire data field as an unsigned int> 
    memcpy(&total_length, data_ptr, sizeof(unsigned int));
    data_ptr = data_ptr + sizeof(unsigned int);
    WIFI_EVENT_CONSUMER_DGB("total_length %u\n", total_length);

    //DataTimeStamp:  <date-time, number of seconds since the Epoch> 
    memcpy(&datetime, data_ptr, sizeof(time_t));
    data_ptr = data_ptr + sizeof(time_t);
    memset(buf, 0, sizeof(buf));
    ctime_r(&datetime, buf);
    WIFI_EVENT_CONSUMER_DGB("datetime %s\n", buf);

    //NumberOfClients:  <unsigned int number of client devices> 
    memcpy(&num_csi_clients, data_ptr, sizeof(unsigned int));
    data_ptr = data_ptr + sizeof(unsigned int);
    WIFI_EVENT_CONSUMER_DGB("num_csi_clients %u\n", num_csi_clients);

    //clientMacAddress:  <client mac address>
    memcpy(&sta_mac, data_ptr, sizeof(mac_addr_t));
    data_ptr = data_ptr + sizeof(mac_addr_t);
    WIFI_EVENT_CONSUMER_DGB("==========================================================");
    WIFI_EVENT_CONSUMER_DGB("MAC %02x%02x%02x%02x%02x%02x\n", sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);

    //length of client CSI data:  <size of the next field in bytes>
    memcpy(&csi_data_length,data_ptr, sizeof(unsigned int));
    data_ptr = data_ptr + sizeof(unsigned int);
    WIFI_EVENT_CONSUMER_DGB("csi_data_length %u\n", csi_data_length);

    //<client device CSI data>
    memcpy(&csi, data_ptr, sizeof(wifi_csi_data_t));
    
    //Writing the CSI data to /tmp/CSI.bin
    rotate_and_write_CSIData(sta_mac, &csi);

    //Printing _wifi_frame_info
    WIFI_EVENT_CONSUMER_DGB("bw_mode %d, mcs %d, Nr %d, Nc %d, valid_mask %hu, phy_bw %hu, cap_bw %hu, num_sc %hu, decimation %d, channel %d, time_stamp %llu",csi.frame_info.bw_mode, csi.frame_info.mcs, csi.frame_info.Nr, csi.frame_info.Nc,csi.frame_info.valid_mask,csi.frame_info.phy_bw, csi.frame_info.cap_bw, csi.frame_info.num_sc, csi.frame_info.decimation, csi.frame_info.channel, csi.frame_info.time_stamp);

    //Printing rssii
    WIFI_EVENT_CONSUMER_DGB("rssi values on each Nr are");
    for(itr=0; itr<=csi.frame_info.Nr; itr++) {
        WIFI_EVENT_CONSUMER_DGB("%d...", csi.frame_info.nr_rssi[itr]);
    }
    WIFI_EVENT_CONSUMER_DGB("==========================================================");
    UNREFERENCED_PARAMETER(handle);
}

static void csiEnableHandler(rbusHandle_t handle, rbusEvent_t const* event,
    rbusEventSubscription_t* subscription)
{
    rbusValue_t value;
    int csi_session;

    if(!event || (sscanf(subscription->eventName,
        "Device.WiFi.X_RDK_CSI.%d.Enable", &csi_session) != 1))
    {
        WIFI_EVENT_CONSUMER_DGB("Invalid Event Received %s", subscription->eventName);
        return;
    }

    value = rbusObject_GetValue(event->data, "value");
    if (value)
    {
        WIFI_EVENT_CONSUMER_DGB("CSI session %d enable changed to %d", csi_session, rbusValue_GetBoolean(value));
    }

    UNREFERENCED_PARAMETER(handle);
}

rbusEventSubscription_t g_subscriptions[8] = {
    /* Event Name,                                             filter, interval,   duration,   handler,                user data, handle */
    {"Device.WiFi.AccessPoint.%d.X_RDK_DiagData",              NULL,   0,          0,          diagHandler,            NULL, NULL, NULL, false},
    {"Device.WiFi.AccessPoint.%d.X_RDK_deviceConnected",       NULL,   0,          0,          deviceConnectHandler,   NULL, NULL, NULL, false},
    {"Device.WiFi.AccessPoint.%d.X_RDK_deviceDisconnected",    NULL,   0,          0,          deviceDisonnectHandler, NULL, NULL, NULL, false},
    {"Device.WiFi.AccessPoint.%d.X_RDK_deviceDeauthenticated", NULL,   0,          0,          deviceDeauthHandler,    NULL, NULL, NULL, false},
    {"Device.WiFi.AccessPoint.%d.Status",                       NULL,   0,          0,          statusHandler,          NULL, NULL, NULL, false},
    {"Device.WiFi.X_RDK_CSI.%d.ClientMaclist",                  NULL,   0,          0,          csiMacListHandler,      NULL, NULL, NULL, false},
    {"Device.WiFi.X_RDK_CSI.%d.data",                          NULL,   100,        0,          csiDataHandler,         NULL, NULL, NULL, false},
    {"Device.WiFi.X_RDK_CSI.%d.Enable",                         NULL,   0,          0,          csiEnableHandler,       NULL, NULL, NULL, false},
};

static int isCsiEventSet(void)
{
    return (g_events_list[5] || g_events_list[6] || g_events_list[7]);
}

static bool parseEvents(char *ev_list)
{
    int i, event;
    char *token;
    char *saveptr = NULL;

    if (!ev_list)
    {
        return false;
    }

    for (i = 0; i < MAX_EVENTS; i++)
    {
        g_events_list[i] = 0;
    }

    token = strtok_r(ev_list, ",", &saveptr);
    while( token != NULL )
    {
        event = atoi(token); 
        if (event < 1 || event > MAX_EVENTS)
        {
            return false;
        }
        g_events_list[event - 1] = 1;
        token = strtok_r(NULL, ",", &saveptr);
        g_events_cnt++;
    }

    return true;
}

static bool parseVaps(char *vap_list)
{
    char *token;
    char *saveptr = NULL;

    if (!vap_list)
    {
        return false;
    }

    token = strtok_r(vap_list, ",", &saveptr);
    while( token != NULL )
    {
        g_vaps_list[g_vaps_cnt] = atoi(token);
        if (g_vaps_list[g_vaps_cnt] < 1 || g_vaps_list[g_vaps_cnt] > g_max_vaps)
        {
            return false;
        }
        token = strtok_r(NULL, ",", &saveptr);
        g_vaps_cnt++;
    }

    return true;
}

static int fillSubscribtion(int index, char *name, int event_index)
{
    if(name==NULL)
    {
        return -1;
    }
    g_all_subs[index].eventName = malloc(strlen(name) + 1);
    memcpy((char *)g_all_subs[index].eventName, name, strlen(name) + 1);

    g_all_subs[index].handler = g_subscriptions[event_index].handler;

    g_all_subs[index].userData = NULL;
    g_all_subs[index].filter = NULL;
    g_all_subs[index].handle = NULL;
    g_all_subs[index].asyncHandler = NULL;
    return 0;
}

static void freeSubscription(rbusEventSubscription_t* sub)
{
    if (sub && sub->eventName)
    {
        free((void*)sub->eventName);
    }
}

static bool parseArguments(int argc, char **argv)
{
    int c;
    bool ret = true;
    char *p;

    while ((c = getopt(argc, argv, "he:s:v:i:c:f:")) != -1)
    {
        switch (c)
        {
            case 'h':
                printf("HELP :  wifi_events_consumer -e [numbers] - default all events\n"
                "\t1 - subscribe to client diagnostic event\n"
                "\t2 - subscribe to device connected event\n"
                "\t3 - subscribe to device disconnected\n"
                "\t4 - subscribe to device deauthenticated\n"
                "\t5 - subscribe to VAP status\n"
                "\t6 - subscribe to csi ClientMacList\n"
                "\t7 - subscribe to csi data\n"
                "\t8 - subscribe to csi Enable\n"
                "-s [csi session] - default create session\n"
                "-v [vap index list] - default all VAPs\n"
                "-i [csi data interval] - default %dms min %d max %d\n"
                "-c [client diag interval] - default %dms\n"
                "-f [debug file name] - default /tmp/wifiEventConsumer\n"
                "Example: wifi_events_consumer -e 1,2,3,7 -s 1 -v 1,2,13,14\n", DEFAULT_CSI_INTERVAL, MIN_CSI_INTERVAL, MAX_CSI_INTERVAL, DEFAULT_CLIENTDIAG_INTERVAL);
                exit(0);
                break;
            case 'e':
                if(!parseEvents(optarg))
                {
                    printf(" Failed to parse events list\n");
                    ret = false;
                }
                break;
            case 's':
                if(!optarg || atoi(optarg) < 0)
                {
                    printf(" Failed to parse csi session\n");
                    ret = false;
                }
                g_csi_index = strtoul(optarg, &p, 10);
                g_csi_session_set = true;
                break;
            case 'v':
                if(!parseVaps(optarg))
                {
                    printf(" Failed to parse VAPs list\n");
                    ret = false;
                }
                break;
            case 'i':
                if(!optarg || atoi(optarg) <= 0)
                {
                    printf(" Failed to parse csi interval: %s\n", optarg);
                    ret = false;
                }
                g_csi_interval = atoi(optarg);
                break;
            case 'c':
                if(!optarg || atoi(optarg) < 0)
                {
                    printf(" Failed to parse client diag interval: %s\n", optarg);
                    ret = false;
                }
                g_clientdiag_interval = atoi(optarg);
                break;
            case 'f':
                if(!optarg)
                {
                    printf(" Failed to parse debug file name\n");
                    ret = false;
                }
                snprintf(g_debug_file_name, RBUS_MAX_NAME_LENGTH, "/tmp/%s", optarg);
                break;
            case '?' :
                printf("Supposed to get an argument for this option or invalid option\n");
                exit(0);
            default:
                printf("Starting with default values\n");
                break;
        }
    }

    return ret;
}

static void termSignalHandler(int sig)
{
    char name[RBUS_MAX_NAME_LENGTH];
    int i;

    WIFI_EVENT_CONSUMER_DGB("Caught signal %d",sig);

    if (g_all_subs)
    {
        rbusEvent_UnsubscribeEx(g_handle, g_all_subs, g_sub_total);
        for (i = 0; i < g_sub_total; i++)
            freeSubscription(&g_all_subs[i]);

        free(g_all_subs);
    }

    if (!g_events_cnt || (!g_csi_session_set && isCsiEventSet()))
    {
        snprintf(name, RBUS_MAX_NAME_LENGTH, "Device.WiFi.X_RDK_CSI.%d.", g_csi_index);
        WIFI_EVENT_CONSUMER_DGB("Remove %s", name);
        rbusTable_removeRow(g_handle, name);
    }

    rbus_close(g_handle);

    if (g_fpg)
    {
        fclose(g_fpg);
    }

    exit(0);
}

int main(int argc, char *argv[])
{
    struct sigaction new_action;
    char name[RBUS_MAX_NAME_LENGTH];
    int i, j, vaps_subsribe;
    int rc = RBUS_ERROR_SUCCESS;
    int sub_index = 0;

    /* Add pid to rbus component name */
    g_pid = getpid();
    snprintf(g_component_name, RBUS_MAX_NAME_LENGTH, "%s%d", "WifiEventConsumer", g_pid);

    rc = rbus_open(&g_handle, g_component_name);
    if(rc != RBUS_ERROR_SUCCESS)
    {
        printf("consumer: rbus_open failed: %d\n", rc);
        if (g_fpg)
        {
            fclose(g_fpg);
        }
        return rc;
    }

    wifievents_get_max_vaps();

    if(!parseArguments(argc, argv))
    {
        return -1;
    }

    /* Set default debug file */
    if (g_debug_file_name[0] == '\0')
    {
        snprintf(g_debug_file_name, RBUS_MAX_NAME_LENGTH, "%s", DEFAULT_DBG_FILE);
    }

    /* Register signal handler */
    new_action.sa_handler = termSignalHandler; 
    sigaction(SIGTERM, &new_action, NULL);
    sigaction(SIGINT, &new_action, NULL);

    for (i = 0; i < MAX_EVENTS; i++)
    {
        if (g_events_cnt && !g_events_list[i])
        {
            continue;
        }
        switch (i)
        {
            case 0: /* Device.WiFi.AccessPoint.{i}.X_RDK_DiagData */
            case 1: /* Device.WiFi.AccessPoint.{i}.X_RDK_deviceConnected" */
            case 2: /* Device.WiFi.AccessPoint.{i}.X_RDK_deviceDisconnected */
            case 3: /* Device.WiFi.AccessPoint.{i}.X_RDK_deviceDeauthenticated*/
            case 4: /* Device.WiFi.AccessPoint.{i}.Status */
                g_sub_total += g_vaps_cnt ? g_vaps_cnt : g_max_vaps;
                break;
            case 5: /* Device.WiFi.X_RDK_CSI.{i}.ClientMaclist */
            case 6: /* Device.WiFi.X_RDK_CSI.{i}.data */
            case 7: /* Device.WiFi.X_RDK_CSI.{i}.Enable */
                g_sub_total++;
                break;
        }
    }

    /* Create new CSI session if index was not set by command line */
    if (!g_events_cnt || (!g_csi_session_set && isCsiEventSet()))
    {
        rc = rbusTable_addRow(g_handle, "Device.WiFi.X_RDK_CSI.", NULL, &g_csi_index);
        if(rc != RBUS_ERROR_SUCCESS)
        {
            printf("Failed to add CSI\n");
            goto exit;
        }
    }

    g_all_subs = malloc(sizeof(rbusEventSubscription_t) * g_sub_total);
    if (!g_all_subs)
    {
        printf("Failed to alloc memory\n");
        goto exit1;
    }

    memset(g_all_subs, 0, (sizeof(rbusEventSubscription_t) * g_sub_total));

    for (i = 0; i < MAX_EVENTS; i++)
    {
        /* Should not happen */
        if (g_sub_total == sub_index)
            break;

        if (g_events_cnt && !g_events_list[i])
            continue;

        switch (i)
        {
            case 0: /* Device.WiFi.AccessPoint.{i}.X_RDK_DiagData */
                vaps_subsribe = g_vaps_cnt ? g_vaps_cnt : g_max_vaps; 

                for (j = 0; j < vaps_subsribe; j++)
                {
                    if (g_clientdiag_interval) {
                        g_all_subs[sub_index].interval = g_clientdiag_interval;
                    }
                    else {
                        g_all_subs[sub_index].interval = DEFAULT_CLIENTDIAG_INTERVAL;
                    }
                    snprintf(name, RBUS_MAX_NAME_LENGTH, g_subscriptions[i].eventName, g_vaps_cnt ? g_vaps_list[j] : (j + 1));
                    WIFI_EVENT_CONSUMER_DGB("Add subscription %s", name);
                    fillSubscribtion(sub_index, name, i);
                    sub_index++;
                }
                break;
            case 1: /* Device.WiFi.AccessPoint.{i}.X_RDK_deviceConnected*/
            case 2: /* Device.WiFi.AccessPoint.{i}.X_RDK_deviceDisconnected */
            case 3: /* Device.WiFi.AccessPoint.{i}.X_RDK_deviceDeauthenticated*/
            case 4: /* Device.WiFi.AccessPoint.{i}.Status */
                vaps_subsribe = g_vaps_cnt ? g_vaps_cnt : g_max_vaps; 

                for (j = 0; j < vaps_subsribe; j++)
                {
                    snprintf(name, RBUS_MAX_NAME_LENGTH, g_subscriptions[i].eventName, g_vaps_cnt ? g_vaps_list[j] : (j + 1));
                    WIFI_EVENT_CONSUMER_DGB("Add subscription %s", name);
                    fillSubscribtion(sub_index, name, i);
                    sub_index++;
                }
                break;
            case 6: /* Device.WiFi.X_RDK_CSI.{i}.data */
                if (g_csi_interval) {
                    g_all_subs[sub_index].interval = g_csi_interval;
                }
                else {
                    g_all_subs[sub_index].interval = DEFAULT_CSI_INTERVAL;
                }

                snprintf(name, RBUS_MAX_NAME_LENGTH, g_subscriptions[i].eventName, g_csi_index);
                WIFI_EVENT_CONSUMER_DGB("Add subscription %s", name);
                fillSubscribtion(sub_index, name, i);
                sub_index++;
                break;
            case 5: /* Device.WiFi.X_RDK_CSI.{i}.ClientMaclist */
            case 7: /* Device.WiFi.X_RDK_CSI.{i}.Enable */
                snprintf(name, RBUS_MAX_NAME_LENGTH, g_subscriptions[i].eventName, g_csi_index);
                WIFI_EVENT_CONSUMER_DGB("Add subscription %s", name);
                fillSubscribtion(sub_index, name, i);
                sub_index++;
                break;
            default:
                break;
        }
    }

    rc = rbusEvent_SubscribeEx(g_handle, g_all_subs, g_sub_total, 0);
    if(rc != RBUS_ERROR_SUCCESS)
    {
        printf("consumer: rbusEvent_Subscribe failed: %d\n", rc);
        goto exit2;
    }

    while(1)
    {
        sleep(5);
    }

exit2:
    if (g_all_subs)
    {
        rbusEvent_UnsubscribeEx(g_handle, g_all_subs, g_sub_total);
        for (i = 0; i < g_sub_total; i++)
        {
            freeSubscription(&g_all_subs[i]);
        }
        free(g_all_subs);
    }

exit1:
    if (!g_csi_session_set && isCsiEventSet())
    {
        snprintf(name, RBUS_MAX_NAME_LENGTH, "Device.WiFi.X_RDK_CSI.%d.", g_csi_index);
        WIFI_EVENT_CONSUMER_DGB("Remove %s", name);
        rbusTable_removeRow(g_handle, name);
    }

exit:
    printf("consumer: exit\n");

    rbus_close(g_handle);

    if (g_fpg)
    {
        fclose(g_fpg);
    }
    return rc;
}
