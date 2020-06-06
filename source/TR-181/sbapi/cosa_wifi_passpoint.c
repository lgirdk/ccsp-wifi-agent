/************************************************************************************
  If not stated otherwise in this file or this component's Licenses.txt file the
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

#include "cosa_apis.h"
#include "cosa_dbus_api.h"
#include "cosa_wifi_apis.h"
#include "cosa_wifi_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include "wifi_hal.h"
#include "cosa_wifi_passpoint.h"
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/un.h>
#include <assert.h>
#include "ansc_status.h"
#include <sysevent/sysevent.h>
#include <cJSON.h>
#include <dirent.h>
#include <errno.h>

#define GAS_CFG_TYPE_SUPPORTED 1
#define GAS_STATS_FIXED_WINDOW_SIZE 10
#define GAS_STATS_TIME_OUT 60

static cosa_wifi_passpoint_t g_passpoint = {0};

static cosa_wifi_anqp_data_t g_anqp_data[16];
static COSA_DML_WIFI_GASSTATS gasStats[GAS_CFG_TYPE_SUPPORTED];
 
extern ANSC_HANDLE bus_handle;
extern char        g_Subsystem[32];

static void wifi_passpoint_dbg_print(int level, char *format, ...)
{
    char buff[2048] = {0};
    va_list list;
    static FILE *fpg = NULL;

    if ((access("/nvram/wifiPasspointDbg", R_OK)) != 0) {
        return;
    }

    get_formatted_time(buff);
    strcat(buff, " ");

    va_start(list, format);
    vsprintf(&buff[strlen(buff)], format, list);
    va_end(list);

    if (fpg == NULL) {
        fpg = fopen("/tmp/wifiPasspoint", "a+");
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

static long readFileToBuffer(const char *fileName, char **buffer)
{
    FILE    *infile = NULL;
    long    numbytes;
    DIR     *passPointDir = NULL;
   
    passPointDir = opendir(WIFI_PASSPOINT_DIR);
    if(passPointDir){
        closedir(passPointDir);
    }else if(ENOENT == errno){
        if(0 != mkdir(WIFI_PASSPOINT_DIR, 0777)){
            wifi_passpoint_dbg_print(1,"Failed to Create Passpoint Configuration directory.\n");
            return 0;
        }
    }else{
        wifi_passpoint_dbg_print(1,"Error opening Passpoint Configuration directory.\n");
        return 0;
    } 
 
    infile = fopen(fileName, "r");
 
    /* quit if the file does not exist */
    if(infile == NULL)
        return 0;
 
    /* Get the number of bytes */
    fseek(infile, 0L, SEEK_END);
    numbytes = ftell(infile);
 
    /* reset the file position indicator to 
    the beginning of the file */
    fseek(infile, 0L, SEEK_SET);	
 
    /* grab sufficient memory for the 
    buffer to hold the text */
    *buffer = (char*)calloc(numbytes+1, sizeof(char));	
 
    /* memory error */
    if(*buffer == NULL){
        fclose(infile);
        return 0;
    }
 
    /* copy all the text into the buffer */
    fread(*buffer, sizeof(char), numbytes, infile);
    fclose(infile);
    return numbytes;
}

void *passpoint_main_func  (void *arg)
{
    char *strValue = NULL;
    int retPsmGet = CCSP_SUCCESS;
    bool exit = false;
    struct timespec time_to_wait;
    struct timeval tv_now;
    struct timeval last_clean_time, curr_time;
    int rc;
    int count = 0;
    mac_addr_str_t mac_str;
    cosa_wifi_anqp_context_t *anqpReq = NULL;
    wifi_anqp_node_t *anqpList = NULL;
    int respLength = 0, prevRealmCnt = 0, prevDomainCnt = 0, prev3gppCnt = 0;
    UCHAR apIns;
    int mallocRetryCount = 0;
    int capLen;
    UCHAR wfa_oui[3] = {0x50, 0x6f, 0x9a};
    UCHAR *data_pos = NULL; 

    UCHAR gas_query_rate_queue[GAS_STATS_FIXED_WINDOW_SIZE] = {0};
    UCHAR gas_response_rate_queue[GAS_STATS_FIXED_WINDOW_SIZE] = {0};
    UCHAR gas_query_rate_window_sum = 0;
    UCHAR gas_response_rate_window_sum = 0;
  
    UCHAR gas_rate_head = 0;

    UCHAR gas_queries_per_minute_new = 0;
    UCHAR gas_responses_per_minute_new = 0;

    UCHAR gas_queries_per_minute_old = 0;
    UCHAR gas_responses_per_minute_old = 0;

    UCHAR gas_rate_divisor = 0;
    
    gettimeofday(&last_clean_time, NULL);

    while (exit == false) {
        gettimeofday(&tv_now, NULL);

        time_to_wait.tv_nsec = 0;
        time_to_wait.tv_sec = tv_now.tv_sec + GAS_STATS_TIME_OUT; //set timeout to 60 sec

        pthread_mutex_lock(&g_passpoint.lock);
        rc = pthread_cond_timedwait(&g_passpoint.cond, &g_passpoint.lock,&time_to_wait);

        gettimeofday(&curr_time, NULL);
        if ((GAS_STATS_TIME_OUT <= (curr_time.tv_sec - last_clean_time.tv_sec)) ||
            ((curr_time.tv_sec > GAS_STATS_TIME_OUT) &&
             (last_clean_time.tv_sec > curr_time.tv_sec)))
        {
            int gas_rate_tail = (gas_rate_head + 1) % GAS_STATS_FIXED_WINDOW_SIZE;
          
            gas_queries_per_minute_new = gasStats[GAS_CFG_TYPE_SUPPORTED - 1].Queries - gas_queries_per_minute_old;
            gas_queries_per_minute_old = gasStats[GAS_CFG_TYPE_SUPPORTED - 1].Queries;
          
            gas_query_rate_window_sum = gas_query_rate_window_sum - gas_query_rate_queue[gas_rate_tail] + gas_queries_per_minute_new;
            
            gas_responses_per_minute_new = gasStats[GAS_CFG_TYPE_SUPPORTED - 1].Responses - gas_responses_per_minute_old;
            gas_responses_per_minute_old = gasStats[GAS_CFG_TYPE_SUPPORTED - 1].Responses;
          
            gas_response_rate_window_sum = gas_response_rate_window_sum - gas_response_rate_queue[gas_rate_tail] + gas_responses_per_minute_new;
            
            
            //move the head
            gas_rate_head = (gas_rate_head + 1) % GAS_STATS_FIXED_WINDOW_SIZE;
            gas_query_rate_queue[gas_rate_head] = gas_queries_per_minute_new;
            gas_response_rate_queue[gas_rate_head] = gas_responses_per_minute_new;
          
            if (gas_rate_divisor < GAS_STATS_FIXED_WINDOW_SIZE)
            {
                gas_rate_divisor++;//Increment the divisor for the first 10 minutes.
            }

            if (gas_rate_divisor)
            {
                //update stats with calculated values
                gasStats[GAS_CFG_TYPE_SUPPORTED - 1].QueryRate = gas_query_rate_window_sum / gas_rate_divisor;
                gasStats[GAS_CFG_TYPE_SUPPORTED - 1].ResponseRate = gas_response_rate_window_sum / gas_rate_divisor;
            }
          
            last_clean_time.tv_sec = curr_time.tv_sec;
        }

        if ((count = queue_count(g_passpoint.queue)) == 0) {
            //wifi_passpoint_dbg_print(1, "%s:%d: queue is empty. Continue...\n", __func__, __LINE__);
            pthread_mutex_unlock(&g_passpoint.lock);
            continue;
        }

        anqpReq = queue_peek(g_passpoint.queue, count - 1);
        respLength = 0;
        apIns = anqpReq->apIndex;

        if((apIns < 0) || (apIns > 15)){
            wifi_passpoint_dbg_print(1, "%s:%d: Invalid AP Index: %d.\n", __func__, __LINE__,apIns);
        }
      
        //A gas query received increase the stats.
        gasStats[GAS_CFG_TYPE_SUPPORTED - 1].Queries++;

        anqpList = anqpReq->head;

        while(anqpList){
            anqpList->value->len = 0;
            if(anqpList->value->data){
                free(anqpList->value->data);
                anqpList->value->data = NULL;
            }
            if(anqpList->value->type == wifi_anqp_id_type_anqp){
                wifi_passpoint_dbg_print(1, "%s:%d: Received ANQP Request\n", __func__, __LINE__);
                switch (anqpList->value->u.anqp_elem_id){
                    //CapabilityListANQPElement
                    case wifi_anqp_element_name_capability_list:
                        capLen = (g_anqp_data[apIns].capabilityInfoLength * sizeof(USHORT));
                        wifi_passpoint_dbg_print(1, "%s:%d: Received CapabilityListANQPElement Request\n", __func__, __LINE__);
                        anqpList->value->data = malloc(capLen);//To be freed in wifi_anqpSendResponse()
                        if(NULL == anqpList->value->data){
                            wifi_passpoint_dbg_print(1, "%s:%d: Failed to allocate memory\n", __func__, __LINE__);
                            if(mallocRetryCount > 5){
                                pthread_mutex_unlock(&g_passpoint.lock);
                                exit = true;
                                break;
                            }
                            mallocRetryCount++;
                            anqpList = anqpList->next;
                            continue;
                        }
                        data_pos = anqpList->value->data;
                        wifi_passpoint_dbg_print(1, "%s:%d: Preparing to Copy Data. Length: %d\n", __func__, __LINE__,capLen);
                        memset(data_pos,0,capLen);
                        memcpy(data_pos,&g_anqp_data[apIns].capabilityInfo,(g_anqp_data[apIns].capabilityInfoLength * sizeof(USHORT)));
                        data_pos += (g_anqp_data[apIns].capabilityInfoLength * sizeof(USHORT));
                        anqpList->value->len = capLen;
                        respLength += anqpList->value->len;
                        wifi_passpoint_dbg_print(1, "%s:%d: Copied CapabilityListANQPElement Data. Length: %d\n", __func__, __LINE__,anqpList->value->len);
                        break;
                    //IPAddressTypeAvailabilityANQPElement
                    case wifi_anqp_element_name_ip_address_availabality:
                        wifi_passpoint_dbg_print(1, "%s:%d: Received IPAddressTypeAvailabilityANQPElement Request\n", __func__, __LINE__);
                        if(g_anqp_data[apIns].ipAddressInfo){
                            anqpList->value->data = malloc(sizeof(wifi_ipAddressAvailabality_t));//To be freed in wifi_anqpSendResponse()
                            if(NULL == anqpList->value->data){
                                wifi_passpoint_dbg_print(1, "%s:%d: Failed to allocate memory\n", __func__, __LINE__);
                                if(mallocRetryCount > 5){
                                    pthread_mutex_unlock(&g_passpoint.lock);
                                    exit = true;
                                    break;
                                }
                                mallocRetryCount++;
                                anqpList = anqpList->next;
                                continue;
                            }
                            mallocRetryCount = 0;
                            wifi_passpoint_dbg_print(1, "%s:%d: Preparing to Copy Data. Length: %d\n", __func__, __LINE__,sizeof(wifi_ipAddressAvailabality_t));
                            memset(anqpList->value->data,0,sizeof(wifi_ipAddressAvailabality_t));
                            memcpy(anqpList->value->data,g_anqp_data[apIns].ipAddressInfo,sizeof(wifi_ipAddressAvailabality_t));
                            anqpList->value->len = sizeof(wifi_ipAddressAvailabality_t);
                            respLength += anqpList->value->len;
                            wifi_passpoint_dbg_print(1, "%s:%d: Copied IPAddressTypeAvailabilityANQPElement Data. Length: %d. Data: %02X\n", __func__, __LINE__,anqpList->value->len, ((wifi_ipAddressAvailabality_t *)anqpList->value->data)->field_format);
                        }
                        break;
                    //NAIRealmANQPElement
                    case wifi_anqp_element_name_nai_realm:
                        wifi_passpoint_dbg_print(1, "%s:%d: Received NAIRealmANQPElement Request\n", __func__, __LINE__);
                        if(g_anqp_data[apIns].realmInfoLength && g_anqp_data[apIns].realmInfo){
                            anqpList->value->data = malloc(g_anqp_data[apIns].realmInfoLength);//To be freed in wifi_anqpSendResponse()
                            if(NULL == anqpList->value->data){
                                wifi_passpoint_dbg_print(1, "%s:%d: Failed to allocate memory\n", __func__, __LINE__);
                                if(mallocRetryCount > 5){
                                    pthread_mutex_unlock(&g_passpoint.lock);
                                    exit = true;
                                    break;
                                }
                                mallocRetryCount++;
                                anqpList = anqpList->next;
                                continue;
                            }
                            wifi_passpoint_dbg_print(1, "%s:%d: Preparing to Copy Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].realmInfoLength);
                            memset(anqpList->value->data,0,g_anqp_data[apIns].realmInfoLength);
                            memcpy(anqpList->value->data,g_anqp_data[apIns].realmInfo,g_anqp_data[apIns].realmInfoLength);
                            anqpList->value->len = g_anqp_data[apIns].realmInfoLength;
                            respLength += anqpList->value->len;
                            wifi_passpoint_dbg_print(1, "%s:%d: Copied NAIRealmANQPElement Data. Length: %d\n", __func__, __LINE__,anqpList->value->len);
                  
                        }
                        break;
                    //VenueNameANQPElement
                    case wifi_anqp_element_name_venue_name:
                        wifi_passpoint_dbg_print(1, "%s:%d: Received VenueNameANQPElement Request\n", __func__, __LINE__);
                        if(g_anqp_data[apIns].venueInfoLength && g_anqp_data[apIns].venueInfo){
                            anqpList->value->data = malloc(g_anqp_data[apIns].venueInfoLength);//To be freed in wifi_anqpSendResponse()
                            if(NULL == anqpList->value->data){
                                wifi_passpoint_dbg_print(1, "%s:%d: Failed to allocate memory\n", __func__, __LINE__);
                                if(mallocRetryCount > 5){
                                    pthread_mutex_unlock(&g_passpoint.lock);
                                    exit = true;
                                    break;
                                }
                                mallocRetryCount++;
                                anqpList = anqpList->next;
                                continue;
                            }
                            wifi_passpoint_dbg_print(1, "%s:%d: Preparing to Copy Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].venueInfoLength);
                            memset(anqpList->value->data,0,g_anqp_data[apIns].venueInfoLength);
                            memcpy(anqpList->value->data,g_anqp_data[apIns].venueInfo,g_anqp_data[apIns].venueInfoLength);
                            anqpList->value->len = g_anqp_data[apIns].venueInfoLength;
                            respLength += anqpList->value->len;
                            wifi_passpoint_dbg_print(1, "%s:%d: Copied VenueNameANQPElement Data. Length: %d\n", __func__, __LINE__,anqpList->value->len);
                        }
                        break;
                    //3GPPCellularANQPElement
                    case wifi_anqp_element_name_3gpp_cellular_network:
                        wifi_passpoint_dbg_print(1, "%s:%d: Received 3GPPCellularANQPElement Request\n", __func__, __LINE__);
                        if(g_anqp_data[apIns].gppInfoLength && g_anqp_data[apIns].gppInfo){
                            anqpList->value->data = malloc(g_anqp_data[apIns].gppInfoLength);//To be freed in wifi_anqpSendResponse()
                            if(NULL == anqpList->value->data){
                                wifi_passpoint_dbg_print(1, "%s:%d: Failed to allocate memory\n", __func__, __LINE__);
                                if(mallocRetryCount > 5){
                                    pthread_mutex_unlock(&g_passpoint.lock);
                                    exit = true;
                                    break;
                                }
                                mallocRetryCount++;
                                anqpList = anqpList->next;
                                continue;
                            }
                            wifi_passpoint_dbg_print(1, "%s:%d: Preparing to Copy Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].gppInfoLength);
                            memset(anqpList->value->data,0,g_anqp_data[apIns].gppInfoLength);
                            memcpy(anqpList->value->data,g_anqp_data[apIns].gppInfo,g_anqp_data[apIns].gppInfoLength);
                            anqpList->value->len = g_anqp_data[apIns].gppInfoLength;
                            respLength += anqpList->value->len;
                            wifi_passpoint_dbg_print(1, "%s:%d: Copied 3GPPCellularANQPElement Data. Length: %d\n", __func__, __LINE__,anqpList->value->len);
                        }
                        break;
                    //RoamingConsortiumANQPElement
                    case wifi_anqp_element_name_roaming_consortium:
                        wifi_passpoint_dbg_print(1, "%s:%d: Received RoamingConsortiumANQPElement Request\n", __func__, __LINE__);
                        if(g_anqp_data[apIns].roamInfoLength && g_anqp_data[apIns].roamInfo){
                            anqpList->value->data = malloc(g_anqp_data[apIns].roamInfoLength);//To be freed in wifi_anqpSendResponse()
                            if(NULL == anqpList->value->data){
                                wifi_passpoint_dbg_print(1, "%s:%d: Failed to allocate memory\n", __func__, __LINE__);
                                if(mallocRetryCount > 5){
                                    pthread_mutex_unlock(&g_passpoint.lock);
                                    exit = true;
                                    break;
                                }
                                mallocRetryCount++;
                                anqpList = anqpList->next;
                                continue;
                            }
                            wifi_passpoint_dbg_print(1, "%s:%d: Preparing to Copy Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].roamInfoLength);
                            memset(anqpList->value->data,0,g_anqp_data[apIns].roamInfoLength);
                            memcpy(anqpList->value->data,g_anqp_data[apIns].roamInfo,g_anqp_data[apIns].roamInfoLength);
                            anqpList->value->len = g_anqp_data[apIns].roamInfoLength;
                            respLength += anqpList->value->len;
                            wifi_passpoint_dbg_print(1, "%s:%d: Copied RoamingConsortiumANQPElement Data. Length: %d\n", __func__, __LINE__,anqpList->value->len);
                        }
                        break;
                    //DomainANQPElement
                    case wifi_anqp_element_name_domain_name:
                        wifi_passpoint_dbg_print(1, "%s:%d: Received DomainANQPElement Request\n", __func__, __LINE__);
                        if(g_anqp_data[apIns].domainInfoLength && g_anqp_data[apIns].domainNameInfo){
                            anqpList->value->data = malloc(g_anqp_data[apIns].domainInfoLength);//To be freed in wifi_anqpSendResponse()
                            if(NULL == anqpList->value->data){
                                wifi_passpoint_dbg_print(1, "%s:%d: Failed to allocate memory\n", __func__, __LINE__);
                                if(mallocRetryCount > 5){
                                    pthread_mutex_unlock(&g_passpoint.lock);
                                    exit = true;
                                    break;
                                }
                                mallocRetryCount++;
                                anqpList = anqpList->next;
                                continue;
                            }
                            wifi_passpoint_dbg_print(1, "%s:%d: Preparing to Copy Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].domainInfoLength);
                            memset(anqpList->value->data,0,g_anqp_data[apIns].domainInfoLength);
                            memcpy(anqpList->value->data,g_anqp_data[apIns].domainNameInfo,g_anqp_data[apIns].domainInfoLength);
                            anqpList->value->len = g_anqp_data[apIns].domainInfoLength;
                            respLength += anqpList->value->len;
                            wifi_passpoint_dbg_print(1, "%s:%d: Copied DomainANQPElement Data. Length: %d\n", __func__, __LINE__,anqpList->value->len);
                        }
                        break;
                   default:
                        wifi_passpoint_dbg_print(1, "%s:%d: Received Unsupported ANQPElement Request: %d\n", __func__, __LINE__,anqpList->value->u.anqp_elem_id);
                        break;
                }
            }else{
                wifi_passpoint_dbg_print(1, "%s:%d: Invalid Request Type\n", __func__, __LINE__);
            }
            anqpList = anqpList->next;
        }
        queue_pop(g_passpoint.queue);
#ifdef DUAL_CORE_XB3
        if(respLength == 0){
            wifi_passpoint_dbg_print(1, "%s:%d: Requested ANQP parameter is NULL\n", __func__, __LINE__);
        }
        if(RETURN_OK != (wifi_anqpSendResponse(anqpReq->apIndex, anqpReq->sta, anqpReq->token,  anqpReq->head))){
            //We have failed to send a gas response increase the stats
            gasStats[GAS_CFG_TYPE_SUPPORTED - 1].FailedResponses++;

            wifi_passpoint_dbg_print(1, "%s:%d: Failed to send ANQP Response. Clear Request and Continue\n", __func__, __LINE__);
        }else{
            //We have sent a gas response increase the stats
            gasStats[GAS_CFG_TYPE_SUPPORTED - 1].Responses++;
            wifi_passpoint_dbg_print(1, "%s:%d: Successfully sent ANQP Response.\n", __func__, __LINE__);
        }
#endif //DUAL_CORE_XB3
        if(anqpReq){
            free(anqpReq);
            anqpReq = NULL;
        }
        pthread_mutex_unlock(&g_passpoint.lock);
    }
    destroy_passpoint();
}

void anqpRequest_callback(UINT apIndex, mac_address_t sta, unsigned char token,  wifi_anqp_node_t *head)
{
    cosa_wifi_anqp_context_t *anqpReq = malloc(sizeof(cosa_wifi_anqp_context_t));
    memset(anqpReq,0,sizeof(cosa_wifi_anqp_context_t));
    anqpReq->apIndex = apIndex;
    memcpy(anqpReq->sta, sta, sizeof(mac_address_t));
    anqpReq->head = head;
    anqpReq->token = token;

    pthread_mutex_lock(&g_passpoint.lock);
    queue_push(g_passpoint.queue,anqpReq);
    pthread_cond_signal(&g_passpoint.cond);
    pthread_mutex_unlock(&g_passpoint.lock);
}

int start_passpoint_thread (void)
{
#ifdef DUAL_CORE_XB3
    // if the provisioning thread does not exist create
    if ((g_passpoint.tid == -1) && (pthread_create(&g_passpoint.tid, NULL, passpoint_main_func,NULL) != 0)) {
        wifi_passpoint_dbg_print(1, "%s:%d: Passpoint monitor thread create error\n", __func__, __LINE__);
    }
    wifi_passpoint_dbg_print(1, "%s:%d: Passpoint started thread and Exit\n", __func__, __LINE__);
#endif //DUAL_CORE_XB3 
    return RETURN_OK;
}

void destroy_passpoint (void)
{
    queue_destroy(g_passpoint.queue);
    pthread_mutex_destroy(&g_passpoint.lock);
    pthread_cond_destroy(&g_passpoint.cond);
    pthread_cancel(g_passpoint.tid);
    g_passpoint.tid = -1; 
}

int init_passpoint (void)
{
#ifdef DUAL_CORE_XB3
    wifi_passpoint_dbg_print(1, "%s:%d: Initializing Passpoint\n", __func__, __LINE__);

    pthread_cond_init(&g_passpoint.cond, NULL);
    pthread_mutex_init(&g_passpoint.lock, NULL);

    g_passpoint.queue = queue_create();
    g_passpoint.tid = -1;

    wifi_anqp_request_callback_register(anqpRequest_callback);
#endif //#if !defined(_XF3_PRODUCT_REQ_) && !defined(_CBR_PRODUCT_REQ_) && !defined(_HUB4_PRODUCT_REQ_) && !defined (_XB6_PRODUCT_REQ_) && !defined (_ARRIS_XB6_PRODUCT_REQ_) && !defined(_XB7_PRODUCT_REQ_) && !defined(_PLATFORM_TURRIS_)
    return RETURN_OK;
}

ANSC_STATUS CosaDmlWiFi_initPasspoint(void)
{
    if ((init_passpoint() < 0)) {
        fprintf(stderr, "-- %s %d init_passpoint fail\n", __func__, __LINE__);
        return ANSC_STATUS_FAILURE;
    }
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFi_startPasspoint(void)
{
    start_passpoint_thread();
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFi_SetGasConfig(PANSC_HANDLE phContext, char *JSON_STR)
{
    PCOSA_DATAMODEL_WIFI    pMyObject               = ( PCOSA_DATAMODEL_WIFI )phContext;
    wifi_GASConfiguration_t gasConfig_struct;

    if(!pMyObject){
        wifi_passpoint_dbg_print(1,"Wifi Context is NULL\n");
        return ANSC_STATUS_FAILURE;
    }
    PCOSA_DML_WIFI_GASCFG  pGASconf = NULL;

    cJSON *gasList = NULL;
    cJSON *gasEntry = NULL;
    cJSON *gasParam = NULL;

    if(!JSON_STR){
        wifi_passpoint_dbg_print(1,"Failed to read JSON\n");
        return ANSC_STATUS_FAILURE;
    }

    cJSON *passPointCfg = cJSON_Parse(JSON_STR);

    if (NULL == passPointCfg) {
        wifi_passpoint_dbg_print(1,"Failed to parse JSON\n");
        return ANSC_STATUS_FAILURE;
    }

    gasList = cJSON_GetObjectItem(passPointCfg,"gasConfig");
    if(NULL == gasList){
        wifi_passpoint_dbg_print(1,"gasList is NULL\n");
        cJSON_Delete(passPointCfg);
        return ANSC_STATUS_FAILURE;
    }  
    cJSON_ArrayForEach(gasEntry, gasList) {
        gasParam = cJSON_GetObjectItem(gasEntry,"advertId");
        if((!gasParam) || (0 != gasParam->valuedouble)){
            wifi_passpoint_dbg_print(1,"Invalid Configuration. Only Advertisement ID 0 - ANQP is Supported\n");
            cJSON_Delete(passPointCfg);
            return ANSC_STATUS_FAILURE;
        }
        gasConfig_struct.AdvertisementID = 0;
        pGASconf = &pMyObject->GASCfg[gasConfig_struct.AdvertisementID];
        gasParam = cJSON_GetObjectItem(gasEntry,"pauseForServerResp");
        gasConfig_struct.PauseForServerResponse = gasParam ? (((gasParam->type & cJSON_True) !=0) ? true:false) : pGASconf->PauseForServerResponse;
        gasParam = cJSON_GetObjectItem(gasEntry,"respTimeout");
        gasConfig_struct.ResponseTimeout = gasParam ? gasParam->valuedouble : pGASconf->ResponseTimeout;
        gasParam = cJSON_GetObjectItem(gasEntry,"comebackDelay");
        gasConfig_struct.ComeBackDelay = gasParam ? gasParam->valuedouble : pGASconf->ComeBackDelay;
        gasParam = cJSON_GetObjectItem(gasEntry,"respBufferTime");
        gasConfig_struct.ResponseBufferingTime = gasParam ? gasParam->valuedouble : pGASconf->ResponseBufferingTime;
        gasParam = cJSON_GetObjectItem(gasEntry,"queryRespLengthLimit");
        gasConfig_struct.QueryResponseLengthLimit = gasParam ? gasParam->valuedouble : pGASconf->QueryResponseLengthLimit;
    }

#ifdef DUAL_CORE_XB3
    if(RETURN_OK == wifi_setGASConfiguration(gasConfig_struct.AdvertisementID, &gasConfig_struct)){
#endif //DUAL_CORE_XB3
        pGASconf->AdvertisementID = gasConfig_struct.AdvertisementID; 
        pGASconf->PauseForServerResponse = gasConfig_struct.PauseForServerResponse;
        pGASconf->ResponseTimeout = gasConfig_struct.ResponseTimeout;
        pGASconf->ComeBackDelay = gasConfig_struct.ComeBackDelay;
        pGASconf->ResponseBufferingTime = gasConfig_struct.ResponseBufferingTime;
        pGASconf->QueryResponseLengthLimit = gasConfig_struct.QueryResponseLengthLimit;
        cJSON_Delete(passPointCfg);
        return ANSC_STATUS_SUCCESS;
#ifdef DUAL_CORE_XB3
      }
#endif //DUAL_CORE_XB3
    wifi_passpoint_dbg_print(1,"Failed to update HAL with GAS Config. Adv-ID:%d\n",gasConfig_struct.AdvertisementID);
    cJSON_Delete(passPointCfg);
    return ANSC_STATUS_FAILURE;
}

ANSC_STATUS CosaDmlWiFi_DefaultGasConfig(PANSC_HANDLE phContext)
{
    PCOSA_DATAMODEL_WIFI    pMyObject               = ( PCOSA_DATAMODEL_WIFI )phContext;

    if(!pMyObject){
        wifi_passpoint_dbg_print(1,"Wifi Context is NULL\n");
        return ANSC_STATUS_FAILURE;
    }
    char *JSON_STR = malloc(strlen(WIFI_PASSPOINT_DEFAULT_GAS_CFG)+1);
    memset(JSON_STR,0,(strlen(WIFI_PASSPOINT_DEFAULT_GAS_CFG)+1));
    AnscCopyString(JSON_STR, WIFI_PASSPOINT_DEFAULT_GAS_CFG);

    if(!JSON_STR || (ANSC_STATUS_SUCCESS != CosaDmlWiFi_SetGasConfig(phContext,JSON_STR))){
        if(JSON_STR){
            free(JSON_STR);
            JSON_STR = NULL;
        }
        wifi_passpoint_dbg_print(1,"Failed to update HAL with default GAS Config.\n");
        return ANSC_STATUS_FAILURE;
    }
    pMyObject->GASConfiguration = JSON_STR;
    return ANSC_STATUS_SUCCESS; 
}

ANSC_STATUS CosaDmlWiFi_SaveGasCfg(char *buffer, int len)
{
    DIR     *passPointDir = NULL;
   
    passPointDir = opendir(WIFI_PASSPOINT_DIR);
    if(passPointDir){
        closedir(passPointDir);
    }else if(ENOENT == errno){
        if(0 != mkdir(WIFI_PASSPOINT_DIR, 0777)){
            wifi_passpoint_dbg_print(1,"Failed to Create Passpoint Configuration directory. Setting Default\n");
            return ANSC_STATUS_FAILURE;;
        }
    }else{
        wifi_passpoint_dbg_print(1,"Error opening Passpoint Configuration directory. Setting Default\n");
        return ANSC_STATUS_FAILURE;;
    } 
 
    FILE *fPasspointGasCfg = fopen(WIFI_PASSPOINT_GAS_CFG_FILE, "w");
    if(0 == fwrite(buffer, len,1, fPasspointGasCfg)){
        fclose(fPasspointGasCfg);
        return ANSC_STATUS_FAILURE;
    }else{
        fclose(fPasspointGasCfg);
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS CosaDmlWiFi_InitGasConfig(PANSC_HANDLE phContext)
{
    PCOSA_DATAMODEL_WIFI    pMyObject               = ( PCOSA_DATAMODEL_WIFI )phContext;

    if(!pMyObject){
        wifi_passpoint_dbg_print(1,"Wifi Context is NULL\n");
        return ANSC_STATUS_FAILURE;
    }
    pMyObject->GASConfiguration = NULL;
    char *JSON_STR = NULL;
   
    long confSize = readFileToBuffer(WIFI_PASSPOINT_GAS_CFG_FILE,&JSON_STR);

    if(!confSize || !JSON_STR || (ANSC_STATUS_SUCCESS != CosaDmlWiFi_SetGasConfig(phContext,JSON_STR))){
        if(JSON_STR){
            free(JSON_STR);
            JSON_STR = NULL;
        }
        wifi_passpoint_dbg_print(1,"Failed to Initialize GAS Configuration from memory. Setting Default\n");
        return CosaDmlWiFi_DefaultGasConfig(phContext);
    }
    pMyObject->GASConfiguration = JSON_STR;
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFi_GetGasStats(PANSC_HANDLE phContext)
{
    PCOSA_DML_WIFI_GASSTATS  pGASStats   = (PCOSA_DML_WIFI_GASSTATS)phContext;
    if(!pGASStats){
        wifi_passpoint_dbg_print(1,"Wifi GAS Context is NULL\n");
        return ANSC_STATUS_FAILURE;
    }

    memset(pGASStats,0,sizeof(COSA_DML_WIFI_GASSTATS));
    memcpy(pGASStats,&gasStats[GAS_CFG_TYPE_SUPPORTED - 1],sizeof(gasStats));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFi_SetANQPConfig(PCOSA_DML_WIFI_AP_CFG pCfg, char *JSON_STR)
{
    if(!pCfg){
        wifi_passpoint_dbg_print(1,"AP Context is NULL\n");
        return ANSC_STATUS_FAILURE;
    }
    int apIns = pCfg->InstanceNumber -1;
    if((apIns < 0) || (apIns > 15)){
        wifi_passpoint_dbg_print(1, "%s:%d: Invalid AP Index. Setting to 1\n", __func__, __LINE__);
        apIns = 0;
    }

    cJSON *mainEntry = NULL;
    cJSON *anqpElement = NULL;
    cJSON *anqpList = NULL;
    cJSON *anqpEntry = NULL;
    cJSON *anqpParam = NULL;
    cJSON *subList = NULL;
    cJSON *subEntry = NULL;
    cJSON *subParam = NULL;

    if(!JSON_STR){
        wifi_passpoint_dbg_print(1,"JSON String is NULL\n");
        return ANSC_STATUS_FAILURE;
    }

    cJSON *passPointCfg = cJSON_Parse(JSON_STR);

    if (NULL == passPointCfg) {
        wifi_passpoint_dbg_print(1,"Failed to parse JSON\n");
        return ANSC_STATUS_FAILURE;
    }

    mainEntry = cJSON_GetObjectItem(passPointCfg,"InterworkingService");
    if(NULL == mainEntry){
        wifi_passpoint_dbg_print(1,"ANQP entry is NULL\n");
        cJSON_Delete(passPointCfg);
        return ANSC_STATUS_FAILURE;
    }
   
    pthread_mutex_lock(&g_passpoint.lock);//Take lock in case requests come during update.

    //CapabilityListANQPElement
    memset(&g_anqp_data[apIns].capabilityInfo, 0, sizeof(wifi_capabilityListANQP_t));
    g_anqp_data[apIns].capabilityInfoLength = 0;
    g_anqp_data[apIns].capabilityInfo.capabilityList[g_anqp_data[apIns].capabilityInfoLength++] = wifi_anqp_element_name_query_list;
    g_anqp_data[apIns].capabilityInfo.capabilityList[g_anqp_data[apIns].capabilityInfoLength++] = wifi_anqp_element_name_capability_list;
 
    UCHAR *next_pos = NULL;

    //VenueNameANQPElement
    if(g_anqp_data[apIns].venueInfo){
        free(g_anqp_data[apIns].venueInfo);
        g_anqp_data[apIns].venueInfo = NULL;
        g_anqp_data[apIns].venueInfoLength = 0;
    }
    anqpElement = cJSON_GetObjectItem(mainEntry,"VenueNameANQPElement");
    if(anqpElement){
        g_anqp_data[apIns].venueInfo = malloc(sizeof(wifi_venueNameElement_t));
        memset(g_anqp_data[apIns].venueInfo,0,sizeof(wifi_venueNameElement_t));
        next_pos = g_anqp_data[apIns].venueInfo;
        wifi_venueNameElement_t *venueElem = (wifi_venueNameElement_t *)next_pos;
        venueElem->venueGroup = pCfg->IEEE80211uCfg.IntwrkCfg.iVenueGroup;
        next_pos += sizeof(venueElem->venueGroup);
        venueElem->venueType = pCfg->IEEE80211uCfg.IntwrkCfg.iVenueType;
        next_pos += sizeof(venueElem->venueType);
        anqpList    = cJSON_GetObjectItem(anqpElement,"VenueInfo");
        if(anqpList){
            g_anqp_data[apIns].venueCount = cJSON_GetArraySize(anqpList);
            if(cJSON_GetArraySize(anqpList) > 16){
                wifi_passpoint_dbg_print(1, "%s:%d: Venue entries cannot be more than 16. Discarding Configuration\n", __func__, __LINE__);
                free(g_anqp_data[apIns].venueInfo);
                g_anqp_data[apIns].venueInfo = NULL;
                g_anqp_data[apIns].venueInfoLength = 0;
                cJSON_Delete(passPointCfg);
                pthread_mutex_unlock(&g_passpoint.lock);
                return ANSC_STATUS_FAILURE;
            }
            if(g_anqp_data[apIns].venueCount){
                cJSON_ArrayForEach(anqpEntry, anqpList){
                    wifi_venueName_t *venueBuf = (wifi_venueName_t *)next_pos;
                    anqpParam = cJSON_GetObjectItem(anqpEntry,"Length");
                    venueBuf->length = anqpParam ? anqpParam->valuedouble : 0;
                    next_pos += sizeof(venueBuf->length);
                    anqpParam = cJSON_GetObjectItem(anqpEntry,"Language");
                    AnscCopyString(next_pos,anqpParam->valuestring);
                    next_pos += AnscSizeOfString(anqpParam->valuestring);
                    anqpParam = cJSON_GetObjectItem(anqpEntry,"Name");
                    if(AnscSizeOfString(anqpParam->valuestring) > 255){
                        wifi_passpoint_dbg_print(1, "%s:%d: Venue name cannot be more than 255. Discarding Configuration\n", __func__, __LINE__);
                        free(g_anqp_data[apIns].realmInfo);
                        g_anqp_data[apIns].venueInfo = NULL;
                        g_anqp_data[apIns].venueInfoLength = 0;
                        cJSON_Delete(passPointCfg);
                        pthread_mutex_unlock(&g_passpoint.lock);
                        return ANSC_STATUS_FAILURE;
                    }
                    AnscCopyString(next_pos,anqpParam->valuestring);
                    next_pos += AnscSizeOfString(anqpParam->valuestring);
               }
            }
        }
        g_anqp_data[apIns].venueInfoLength = next_pos - g_anqp_data[apIns].venueInfo;
        g_anqp_data[apIns].capabilityInfo.capabilityList[g_anqp_data[apIns].capabilityInfoLength++] = wifi_anqp_element_name_venue_name;
        wifi_passpoint_dbg_print(1, "%s:%d: Copied VenueNameANQPElement Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].venueInfoLength);
    }

    //RoamingConsortiumANQPElement
    if(g_anqp_data[apIns].roamInfo){
        free(g_anqp_data[apIns].roamInfo);
        g_anqp_data[apIns].roamInfo = NULL;
        g_anqp_data[apIns].roamInfoLength = 0;
    }
    anqpElement = cJSON_GetObjectItem(mainEntry,"RoamingConsortiumANQPElement");
    memset(&pCfg->IEEE80211uCfg.RoamCfg, 0, sizeof(pCfg->IEEE80211uCfg.RoamCfg));
    if(anqpElement){
        g_anqp_data[apIns].roamInfo = malloc(sizeof(wifi_roamingConsortium_t));
        memset(g_anqp_data[apIns].roamInfo,0,sizeof(wifi_roamingConsortium_t));
        next_pos = g_anqp_data[apIns].roamInfo;
        anqpList = cJSON_GetObjectItem(anqpElement,"OI");
        int ouiCount = 0;

        if(cJSON_GetArraySize(anqpList) > 32){
            wifi_passpoint_dbg_print(1, "%s:%d: Only 32 OUI supported in RoamingConsortiumANQPElement Data. Discarding Configuration\n", __func__, __LINE__);
            free(g_anqp_data[apIns].roamInfo);
            g_anqp_data[apIns].roamInfo = NULL;
            g_anqp_data[apIns].roamInfoLength = 0;
            cJSON_Delete(passPointCfg);
            pthread_mutex_unlock(&g_passpoint.lock);
            return ANSC_STATUS_FAILURE;
        }

        cJSON_ArrayForEach(anqpEntry, anqpList){
            wifi_ouiDuple_t *ouiBuf = (wifi_ouiDuple_t *)next_pos;
            UCHAR ouiStr[30+1];
            int i, ouiStrLen;
            memset(ouiStr,0,sizeof(ouiStr));
            anqpParam = cJSON_GetObjectItem(anqpEntry,"OI");
            if(anqpParam){
                ouiStrLen = AnscSizeOfString(anqpParam->valuestring);
                if((ouiStrLen < 6) || (ouiStrLen > 30) || (ouiStrLen % 2)){
                    wifi_passpoint_dbg_print(1, "%s:%d: Invalid OUI Length in RoamingConsortiumANQPElement Data. Discarding Configuration\n", __func__, __LINE__);
                    free(g_anqp_data[apIns].roamInfo);
                    g_anqp_data[apIns].roamInfo = NULL;
                    g_anqp_data[apIns].roamInfoLength = 0;
                    cJSON_Delete(passPointCfg);
                    pthread_mutex_unlock(&g_passpoint.lock);
                    return ANSC_STATUS_FAILURE;
                }
                AnscCopyString(ouiStr,anqpParam->valuestring);
	    }
            //Covert the incoming string to HEX 
            for(i = 0; i < ouiStrLen; i++){
                if((ouiStr[i] >= '0') && (ouiStr[i] <= '9')){
                    ouiStr[i] -= '0';
                }else if((ouiStr[i] >= 'a') && (ouiStr[i] <= 'f')){
                    ouiStr[i] -= ('a' - 10);//a=10
                }else if((ouiStr[i] >= 'A') && (ouiStr[i] <= 'F')){
                    ouiStr[i] -= ('A' - 10);//A=10
                }else{
                    wifi_passpoint_dbg_print(1, "%s:%d: Invalid OUI in RoamingConsortiumANQPElement Data. Discarding Configuration\n", __func__, __LINE__);
                    free(g_anqp_data[apIns].roamInfo);
                    g_anqp_data[apIns].roamInfo = NULL;
                    g_anqp_data[apIns].roamInfoLength = 0;
                    cJSON_Delete(passPointCfg);
                    pthread_mutex_unlock(&g_passpoint.lock);
                    return ANSC_STATUS_FAILURE;
                }
                if(i%2){
                    ouiBuf->oui[(i/2)] = ouiStr[i] | (ouiStr[i-1] << 4);
                }
            }
            ouiBuf->length = i/2;
            next_pos += sizeof(ouiBuf->length);
            next_pos += ouiBuf->length;
            if(ouiCount < 3){
                memcpy(&pCfg->IEEE80211uCfg.RoamCfg.iWIFIRoamingConsortiumOui[ouiCount][0],&ouiBuf->oui[0],ouiBuf->length);
                pCfg->IEEE80211uCfg.RoamCfg.iWIFIRoamingConsortiumLen[ouiCount++] = ouiBuf->length;
            }
        }
       
        pCfg->IEEE80211uCfg.RoamCfg.iWIFIRoamingConsortiumCount = cJSON_GetArraySize(anqpList);
        //Push Interworkoing Element to HAL
        if(ANSC_STATUS_SUCCESS != CosaDmlWiFi_ApplyRoamingConsortiumElement(pCfg)){
            wifi_passpoint_dbg_print(1, "%s:%d: CosaDmlWiFi_ApplyRoamingConsortiumElement Failed.\n", __func__, __LINE__);
        } 
        g_anqp_data[apIns].roamInfoLength = next_pos - g_anqp_data[apIns].roamInfo;
        g_anqp_data[apIns].capabilityInfo.capabilityList[g_anqp_data[apIns].capabilityInfoLength++] = wifi_anqp_element_name_roaming_consortium;
        wifi_passpoint_dbg_print(1, "%s:%d: Copied RoamingConsortiumANQPElement Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].roamInfoLength);
    }

    //IPAddressTypeAvailabilityANQPElement
    if(g_anqp_data[apIns].ipAddressInfo){
        free(g_anqp_data[apIns].ipAddressInfo);
        g_anqp_data[apIns].ipAddressInfo = NULL;
    }
    anqpEntry = cJSON_GetObjectItem(mainEntry,"IPAddressTypeAvailabilityANQPElement");
    if(anqpEntry){
        g_anqp_data[apIns].ipAddressInfo = malloc(sizeof(wifi_ipAddressAvailabality_t));
        wifi_ipAddressAvailabality_t *ipInfoBuf = (wifi_ipAddressAvailabality_t *)g_anqp_data[apIns].ipAddressInfo;
        ipInfoBuf->field_format = 0;
        anqpParam = cJSON_GetObjectItem(anqpEntry,"IPv6AddressType");
        if(anqpParam){
            if((0 > anqpParam->valuedouble) || (2 < anqpParam->valuedouble)){
                wifi_passpoint_dbg_print(1, "%s:%d: Invalid IPAddressTypeAvailabilityANQPElement. Discarding Configuration\n", __func__, __LINE__);
                free(g_anqp_data[apIns].ipAddressInfo);
                g_anqp_data[apIns].ipAddressInfo = NULL;
                cJSON_Delete(passPointCfg);
                pthread_mutex_unlock(&g_passpoint.lock);
                return ANSC_STATUS_FAILURE;
            }
            ipInfoBuf->field_format = (UCHAR)anqpParam->valuedouble;
        }
        anqpParam = cJSON_GetObjectItem(anqpEntry,"IPv4AddressType");
        if(anqpParam){
            if((0 > anqpParam->valuedouble) || (7 < anqpParam->valuedouble)){
                wifi_passpoint_dbg_print(1, "%s:%d: Invalid IPAddressTypeAvailabilityANQPElement. Discarding Configuration\n", __func__, __LINE__);
                free(g_anqp_data[apIns].ipAddressInfo);
                g_anqp_data[apIns].ipAddressInfo = NULL;
                cJSON_Delete(passPointCfg);
                pthread_mutex_unlock(&g_passpoint.lock);
                return ANSC_STATUS_FAILURE;
            }
            UCHAR ipv4Field = (UCHAR)anqpParam->valuedouble;
            ipInfoBuf->field_format |= (ipv4Field << 2);
        }
        g_anqp_data[apIns].capabilityInfo.capabilityList[g_anqp_data[apIns].capabilityInfoLength++] = wifi_anqp_element_name_ip_address_availabality;
        wifi_passpoint_dbg_print(1, "%s:%d: Copied IPAddressTypeAvailabilityANQPElement Data. Length: %d. Data: %02X\n", __func__, __LINE__,sizeof(wifi_ipAddressAvailabality_t),ipInfoBuf->field_format);
    }
   
    //NAIRealmANQPElement
    if(g_anqp_data[apIns].realmInfo){
        free(g_anqp_data[apIns].realmInfo);
        g_anqp_data[apIns].realmInfo = NULL;
        g_anqp_data[apIns].realmInfoLength = 0;
    }
    anqpElement = cJSON_GetObjectItem(mainEntry,"NAIRealmANQPElement");
    if(anqpElement){
        anqpList    = cJSON_GetObjectItem(anqpElement,"Realm");
        if(anqpList){
            g_anqp_data[apIns].realmCount = cJSON_GetArraySize(anqpList);
            if(g_anqp_data[apIns].realmCount){
                if(g_anqp_data[apIns].realmCount > 20){
                    wifi_passpoint_dbg_print(1, "%s:%d: Only 20 Realm Entries are supported. Discarding Configuration\n", __func__, __LINE__);
                    cJSON_Delete(passPointCfg);
                    pthread_mutex_unlock(&g_passpoint.lock);
                    return ANSC_STATUS_FAILURE;
                }
                g_anqp_data[apIns].realmInfo = malloc(sizeof(wifi_naiRealmElement_t));
                memset(g_anqp_data[apIns].realmInfo,0,sizeof(wifi_naiRealmElement_t));
                next_pos = g_anqp_data[apIns].realmInfo;
                wifi_naiRealmElement_t *naiElem = (wifi_naiRealmElement_t *)next_pos;
                naiElem->nai_realm_count = g_anqp_data[apIns].realmCount;
                next_pos += sizeof(naiElem->nai_realm_count);
                cJSON_ArrayForEach(anqpEntry, anqpList){
                    wifi_naiRealm_t *realmInfoBuf = (wifi_naiRealm_t *)next_pos;
                    next_pos += sizeof(realmInfoBuf->data_field_length);
                    anqpParam = cJSON_GetObjectItem(anqpEntry,"RealmEncoding");
                    realmInfoBuf->encoding = anqpParam ? anqpParam->valuedouble : 0;
                    next_pos += sizeof(realmInfoBuf->encoding);
                    anqpParam = cJSON_GetObjectItem(anqpEntry,"Realms");
                    realmInfoBuf->realm_length = AnscSizeOfString(anqpParam->valuestring);
                    if(realmInfoBuf->realm_length > 255){
                        wifi_passpoint_dbg_print(1, "%s:%d: Realm Length cannot be more than 255. Discarding Configuration\n", __func__, __LINE__);
                        free(g_anqp_data[apIns].realmInfo);
                        g_anqp_data[apIns].realmInfo = NULL; 
                        g_anqp_data[apIns].realmInfoLength = 0;
                        cJSON_Delete(passPointCfg);
                        pthread_mutex_unlock(&g_passpoint.lock);
                        return ANSC_STATUS_FAILURE;
                    }
                    next_pos += sizeof(realmInfoBuf->realm_length);
                    AnscCopyString(next_pos,anqpParam->valuestring);
                    next_pos += realmInfoBuf->realm_length;

                    subList = cJSON_GetObjectItem(anqpEntry,"EAP");
                    if(cJSON_GetArraySize(subList) > 16){
                        wifi_passpoint_dbg_print(1, "%s:%d: EAP entries cannot be more than 16. Discarding Configuration\n", __func__, __LINE__);
                        free(g_anqp_data[apIns].realmInfo);
                        g_anqp_data[apIns].realmInfo = NULL;
                        g_anqp_data[apIns].realmInfoLength = 0;
                        cJSON_Delete(passPointCfg);
                        pthread_mutex_unlock(&g_passpoint.lock);
                        return ANSC_STATUS_FAILURE;
                    }
                    *next_pos = cJSON_GetArraySize(subList);//eap_method_count
                    next_pos += sizeof(realmInfoBuf->eap_method_count);
                    cJSON_ArrayForEach(subEntry, subList){
                        wifi_eapMethod_t *eapBuf = (wifi_eapMethod_t *)next_pos;
                        next_pos += sizeof(eapBuf->length);
                        subParam = cJSON_GetObjectItem(subEntry,"Method");
                        eapBuf->method = subParam ? subParam->valuedouble : 0; 
                        next_pos += sizeof(eapBuf->method);
                        cJSON *subList_1  = NULL;
                        cJSON *subEntry_1 = NULL;
                        cJSON *subParam_1 = NULL;
                        subList_1 = cJSON_GetObjectItem(subEntry,"AuthenticationParameter");
                        eapBuf->auth_param_count = cJSON_GetArraySize(subList_1);
                        if(eapBuf->auth_param_count > 16){
                            wifi_passpoint_dbg_print(1, "%s:%d: Auth entries cannot be more than 16. Discarding Configuration\n", __func__, __LINE__);
                            free(g_anqp_data[apIns].realmInfo);
                            g_anqp_data[apIns].realmInfo = NULL;
                            g_anqp_data[apIns].realmInfoLength = 0;
                            cJSON_Delete(passPointCfg);
                            pthread_mutex_unlock(&g_passpoint.lock);
                            return ANSC_STATUS_FAILURE;
                        }
                        next_pos += sizeof(eapBuf->auth_param_count);
                        cJSON_ArrayForEach(subEntry_1, subList_1){
                            int i,authStrLen;
                            UCHAR authStr[14+1];
                            wifi_authMethod_t *authBuf = (wifi_authMethod_t *)next_pos;
                            subParam_1 = cJSON_GetObjectItem(subEntry_1,"ID");
                            authBuf->id = subParam_1 ? subParam_1->valuedouble:0;
                            next_pos += sizeof(authBuf->id);
                            subParam_1 = cJSON_GetObjectItem(subEntry_1,"Value");

                            if(!subParam_1){
                                wifi_passpoint_dbg_print(1, "%s:%d: Auth Parameter Value not prensent in NAIRealmANQPElement EAP Data. Discarding Configuration\n", __func__, __LINE__);
                                free(g_anqp_data[apIns].realmInfo);
                                g_anqp_data[apIns].realmInfo = NULL;
                                g_anqp_data[apIns].realmInfoLength = 0;
                                cJSON_Delete(passPointCfg);
                                pthread_mutex_unlock(&g_passpoint.lock);
                                return ANSC_STATUS_FAILURE;
                            } else if (subParam_1->valuedouble) {
                                authBuf->length = 1;
                                authBuf->val[0] = subParam_1->valuedouble;
                            } else {
                                authStrLen = AnscSizeOfString(subParam_1->valuestring);
                                if((authStrLen != 2) && (authStrLen != 14)){
                                    wifi_passpoint_dbg_print(1, "%s:%d: Invalid EAP Value Length in NAIRealmANQPElement Data. Has to be 1 to 7 bytes Long. Discarding Configuration\n", __func__, __LINE__);
                                    free(g_anqp_data[apIns].realmInfo);
                                    g_anqp_data[apIns].realmInfo = NULL;
                                    g_anqp_data[apIns].realmInfoLength = 0;
                                    cJSON_Delete(passPointCfg);
                                    pthread_mutex_unlock(&g_passpoint.lock);
                                    return ANSC_STATUS_FAILURE;
                                }

                                AnscCopyString(authStr,subParam_1->valuestring);

                                //Covert the incoming string to HEX
                                for(i = 0; i < authStrLen; i++){ 
                                    if((authStr[i] >= '0') && (authStr[i] <= '9')){
                                        authStr[i] -= '0'; 
                                    }else if((authStr[i] >= 'a') && (authStr[i] <= 'f')){
                                        authStr[i] -= ('a' - 10);//a=10
                                    }else if((authStr[i] >= 'A') && (authStr[i] <= 'F')){
                                        authStr[i] -= ('A' - 10);//A=10
                                    }else{
                                        wifi_passpoint_dbg_print(1, "%s:%d: Invalid EAP val in NAIRealmANQPElement Data. Discarding Configuration\n", __func__, __LINE__);
                                        free(g_anqp_data[apIns].realmInfo);
                                        g_anqp_data[apIns].realmInfo = NULL;
                                        g_anqp_data[apIns].realmInfoLength = 0;
                                        cJSON_Delete(passPointCfg);
                                        pthread_mutex_unlock(&g_passpoint.lock);
                                        return ANSC_STATUS_FAILURE;
                                    }
                                    if(i%2){
                                        authBuf->val[(i/2)] = authStr[i] | (authStr[i-1] << 4);
                                    }
                                }
                                authBuf->length = i/2;
                            }
                            next_pos += sizeof(authBuf->length);
                            next_pos += authBuf->length;
                        }
                        eapBuf->length = next_pos - &eapBuf->method;
                    }
                    realmInfoBuf->data_field_length = next_pos - &realmInfoBuf->encoding;
                }
                g_anqp_data[apIns].realmInfoLength = next_pos - g_anqp_data[apIns].realmInfo;
            }
        }
        g_anqp_data[apIns].capabilityInfo.capabilityList[g_anqp_data[apIns].capabilityInfoLength++] = wifi_anqp_element_name_nai_realm;
        wifi_passpoint_dbg_print(1, "%s:%d: Copied NAIRealmANQPElement Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].realmInfoLength);
    }

    //3GPPCellularANQPElement
    if(g_anqp_data[apIns].gppInfo){
        free(g_anqp_data[apIns].gppInfo);
        g_anqp_data[apIns].gppInfo = NULL;
        g_anqp_data[apIns].gppInfoLength = 0;
    }
    anqpElement = cJSON_GetObjectItem(mainEntry,"3GPPCellularANQPElement");
    if(anqpElement){
        g_anqp_data[apIns].gppInfo = malloc(sizeof(wifi_3gppCellularNetwork_t));
        memset(g_anqp_data[apIns].gppInfo,0,sizeof(wifi_3gppCellularNetwork_t));
        next_pos = g_anqp_data[apIns].gppInfo;
        wifi_3gppCellularNetwork_t *gppBuf = (wifi_3gppCellularNetwork_t *)next_pos;
        anqpParam = cJSON_GetObjectItem(anqpElement,"GUD");
        gppBuf->gud = anqpParam ? anqpParam->valuedouble:0;
        next_pos += sizeof(gppBuf->gud);
        next_pos += sizeof(gppBuf->uhdLength);//Skip over UHD length to be filled at the end
        UCHAR *uhd_pos = next_pos;//Beginning of UHD data
    
        wifi_3gpp_plmn_list_information_element_t *plmnInfoBuf = (wifi_3gpp_plmn_list_information_element_t *)next_pos;
        plmnInfoBuf->iei = 0;
        next_pos += sizeof(plmnInfoBuf->iei);
        next_pos += sizeof(plmnInfoBuf->plmn_length);//skip through the length field that will be filled at the end
        UCHAR *plmn_pos = next_pos;//beginnig of PLMN data
        
        anqpList    = cJSON_GetObjectItem(anqpElement,"PLMN");
        plmnInfoBuf->number_of_plmns = cJSON_GetArraySize(anqpList);
        next_pos += sizeof(plmnInfoBuf->number_of_plmns);

        if(plmnInfoBuf->number_of_plmns > 16){
            wifi_passpoint_dbg_print(1, "%s:%d: 3GPP entries cannot be more than 16. Discarding Configuration\n", __func__, __LINE__);
            free(g_anqp_data[apIns].gppInfo);
            g_anqp_data[apIns].gppInfo = NULL;
            g_anqp_data[apIns].gppInfoLength = 0;
            cJSON_Delete(passPointCfg);
            pthread_mutex_unlock(&g_passpoint.lock);
            return ANSC_STATUS_FAILURE;
        }

        cJSON_ArrayForEach(anqpEntry, anqpList){
            UCHAR mccStr[3+1];
            UCHAR mncStr[3+1];
            memset(mccStr,0,sizeof(mccStr));
            memset(mncStr,0,sizeof(mncStr));
             
            anqpParam = cJSON_GetObjectItem(anqpEntry,"MCC");
            if(anqpParam && anqpParam->valuestring && (AnscSizeOfString(anqpParam->valuestring) == (sizeof(mccStr) -1))){
                AnscCopyString(mccStr,anqpParam->valuestring);
            }else if(anqpParam && anqpParam->valuestring && (AnscSizeOfString(anqpParam->valuestring) == (sizeof(mccStr) -2))){
                mccStr[0] = '0';
                AnscCopyString(&mccStr[1],anqpParam->valuestring);
            }else{
                wifi_passpoint_dbg_print(1, "%s:%d: Invalid MCC in 3GPPCellularANQPElement Data. Discarding Configuration\n", __func__, __LINE__);
                free(g_anqp_data[apIns].gppInfo);
                g_anqp_data[apIns].gppInfo = NULL;
                g_anqp_data[apIns].gppInfoLength = 0;
                cJSON_Delete(passPointCfg);
                pthread_mutex_unlock(&g_passpoint.lock);
                return ANSC_STATUS_FAILURE;
            }
            anqpParam = cJSON_GetObjectItem(anqpEntry,"MNC");
            if(anqpParam && anqpParam->valuestring && (AnscSizeOfString(anqpParam->valuestring) == (sizeof(mccStr) -1))){
                AnscCopyString(mncStr,anqpParam->valuestring);
            }else if(anqpParam && anqpParam->valuestring && (AnscSizeOfString(anqpParam->valuestring) ==  (sizeof(mccStr) -2))){
                mncStr[0] = '0';
                AnscCopyString(&mncStr[1],anqpParam->valuestring);
            }else{
                wifi_passpoint_dbg_print(1, "%s:%d: Invalid MNC in 3GPPCellularANQPElement Data. Discarding Configuration\n", __func__, __LINE__);
                free(g_anqp_data[apIns].gppInfo);
                g_anqp_data[apIns].gppInfo = NULL;
                g_anqp_data[apIns].gppInfoLength = 0;
                cJSON_Delete(passPointCfg);
                pthread_mutex_unlock(&g_passpoint.lock);
                return ANSC_STATUS_FAILURE;
            }
            wifi_plmn_t *plmnBuf = (wifi_plmn_t *)next_pos;
            plmnBuf->PLMN[0] = (UCHAR)((mccStr[0] - '0') | ((mccStr[1] - '0') << 4));
            plmnBuf->PLMN[1] = (UCHAR)((mccStr[2] - '0') | ((mncStr[2] - '0') << 4));
            plmnBuf->PLMN[2] = (UCHAR)((mncStr[0] - '0') | ((mncStr[1] - '0') << 4));
            next_pos += sizeof(wifi_plmn_t);

        }
        gppBuf->uhdLength = next_pos - uhd_pos;
        plmnInfoBuf->plmn_length = next_pos - plmn_pos;
        g_anqp_data[apIns].gppInfoLength = next_pos - g_anqp_data[apIns].gppInfo;
        g_anqp_data[apIns].capabilityInfo.capabilityList[g_anqp_data[apIns].capabilityInfoLength++] = wifi_anqp_element_name_3gpp_cellular_network;
        wifi_passpoint_dbg_print(1, "%s:%d: Copied 3GPPCellularANQPElement Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].gppInfoLength);
    }

    //DomainANQPElement
    if(g_anqp_data[apIns].domainNameInfo){
        free(g_anqp_data[apIns].domainNameInfo);
        g_anqp_data[apIns].domainNameInfo = NULL;
        g_anqp_data[apIns].domainInfoLength = 0;
    }
    anqpElement = cJSON_GetObjectItem(mainEntry,"DomainANQPElement");
    if(anqpElement){
        g_anqp_data[apIns].domainNameInfo = malloc(sizeof(wifi_domainName_t));
        memset(g_anqp_data[apIns].domainNameInfo,0,sizeof(wifi_domainName_t));
        next_pos = g_anqp_data[apIns].domainNameInfo;
        wifi_domainName_t *domainBuf = (wifi_domainName_t *)next_pos;
        anqpList = cJSON_GetObjectItem(anqpElement,"DomainName");

        if(cJSON_GetArraySize(anqpList) > 4){
            wifi_passpoint_dbg_print(1, "%s:%d: Only 4 Entries supported in DomainNameANQPElement Data. Discarding Configuration\n", __func__, __LINE__);
            free(g_anqp_data[apIns].domainNameInfo);
            g_anqp_data[apIns].domainNameInfo = NULL;
            g_anqp_data[apIns].domainInfoLength = 0;
        }

        cJSON_ArrayForEach(anqpEntry, anqpList){
            wifi_domainNameTuple_t *nameBuf = (wifi_domainNameTuple_t *)next_pos;
            anqpParam = cJSON_GetObjectItem(anqpEntry,"Name");
            if(AnscSizeOfString(anqpParam->valuestring) > 255){
                wifi_passpoint_dbg_print(1, "%s:%d: Domain name length cannot be more than 255. Discarding Configuration\n", __func__, __LINE__);
                free(g_anqp_data[apIns].domainNameInfo);
                g_anqp_data[apIns].domainNameInfo = NULL;
                g_anqp_data[apIns].domainInfoLength = 0;
                cJSON_Delete(passPointCfg);
                pthread_mutex_unlock(&g_passpoint.lock);
                return ANSC_STATUS_FAILURE;
            }
            nameBuf->length = AnscSizeOfString(anqpParam->valuestring);
            next_pos += sizeof(nameBuf->length);
            AnscCopyString(next_pos,anqpParam->valuestring);
            next_pos += nameBuf->length;

        }
        g_anqp_data[apIns].domainInfoLength = next_pos - g_anqp_data[apIns].domainNameInfo;
        g_anqp_data[apIns].capabilityInfo.capabilityList[g_anqp_data[apIns].capabilityInfoLength++] = wifi_anqp_element_name_domain_name;
        wifi_passpoint_dbg_print(1, "%s:%d: Copied DomainANQPElement Data. Length: %d\n", __func__, __LINE__,g_anqp_data[apIns].domainInfoLength);
    }

    pthread_mutex_unlock(&g_passpoint.lock);
    cJSON_Delete(passPointCfg);

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS CosaDmlWiFi_DefaultANQPConfig(PCOSA_DML_WIFI_AP_CFG pCfg)
{
    if(!pCfg){
        wifi_passpoint_dbg_print(1,"AP Context is NULL\n");
        return ANSC_STATUS_FAILURE;
    }
    char *JSON_STR = malloc(strlen(WIFI_PASSPOINT_DEFAULT_ANQP_CFG)+1);
    memset(JSON_STR,0,(strlen(WIFI_PASSPOINT_DEFAULT_ANQP_CFG)+1));
    AnscCopyString(JSON_STR, WIFI_PASSPOINT_DEFAULT_ANQP_CFG);

    if(!JSON_STR || (ANSC_STATUS_SUCCESS != CosaDmlWiFi_SetANQPConfig(pCfg,JSON_STR))){
        if(JSON_STR){
            free(JSON_STR);
            JSON_STR = NULL;
        }
        wifi_passpoint_dbg_print(1,"Failed to update default ANQP Config.\n");
        return ANSC_STATUS_FAILURE;
    }
    pCfg->IEEE80211uCfg.PasspointCfg.ANQPConfigParameters = JSON_STR;
    return ANSC_STATUS_SUCCESS; 
}

ANSC_STATUS CosaDmlWiFi_SaveANQPCfg(PCOSA_DML_WIFI_AP_CFG pCfg, char *buffer, int len)
{
    char cfgFile[64];
    DIR     *passPointDir = NULL;
    int apIns = 0;

    passPointDir = opendir(WIFI_PASSPOINT_DIR);
    if(passPointDir){
        closedir(passPointDir);
    }else if(ENOENT == errno){
        if(0 != mkdir(WIFI_PASSPOINT_DIR, 0777)){
            wifi_passpoint_dbg_print(1,"Failed to Create Passpoint Configuration directory.\n");
            return ANSC_STATUS_FAILURE;
        }
    }else{
        wifi_passpoint_dbg_print(1,"Error opening Passpoint Configuration directory.\n");
        return ANSC_STATUS_FAILURE;
    } 
 
    if(!pCfg){
        wifi_passpoint_dbg_print(1,"AP Context is NULL\n");
        return ANSC_STATUS_FAILURE;
    }
    apIns = pCfg->InstanceNumber;
    sprintf(cfgFile,"%s.%d",WIFI_PASSPOINT_ANQP_CFG_FILE,apIns);
    FILE *fPasspointAnqpCfg = fopen(cfgFile, "w");
    if(0 == fwrite(buffer, len,1, fPasspointAnqpCfg)){
        fclose(fPasspointAnqpCfg);
        return ANSC_STATUS_FAILURE;
    }else{
        fclose(fPasspointAnqpCfg);
        return ANSC_STATUS_SUCCESS;
    }
}

ANSC_STATUS CosaDmlWiFi_InitANQPConfig(PCOSA_DML_WIFI_AP_CFG pCfg)
{
    char cfgFile[64];
    char *JSON_STR = NULL;
    int apIns = 0;
    long confSize = 0;

    if(!pCfg){
        wifi_passpoint_dbg_print(1,"AP Context is NULL\n");
        return ANSC_STATUS_FAILURE;
    }
    pCfg->IEEE80211uCfg.PasspointCfg.ANQPConfigParameters = NULL;
    apIns = pCfg->InstanceNumber;
    if((apIns < 1) || (apIns > 16)){
        wifi_passpoint_dbg_print(1, "%s:%d: Invalid AP Index. Return\n", __func__, __LINE__);
        return ANSC_STATUS_FAILURE;
    }

    sprintf(cfgFile,"%s.%d",WIFI_PASSPOINT_ANQP_CFG_FILE,apIns);
   
    confSize = readFileToBuffer(cfgFile,&JSON_STR);

    //Initialize global buffer
    g_anqp_data[apIns-1].venueCount = 0;
    g_anqp_data[apIns-1].venueInfoLength = 0;
    g_anqp_data[apIns-1].venueInfo = NULL;
    g_anqp_data[apIns-1].ipAddressInfo = NULL;
    g_anqp_data[apIns-1].realmCount = 0;
    g_anqp_data[apIns-1].realmInfoLength = 0;
    g_anqp_data[apIns-1].realmInfo = NULL;
    g_anqp_data[apIns-1].gppInfoLength = 0;
    g_anqp_data[apIns-1].gppInfo = NULL;
    g_anqp_data[apIns-1].roamInfoLength = 0;
    g_anqp_data[apIns-1].roamInfo = NULL;
    g_anqp_data[apIns-1].domainInfoLength = 0;
    g_anqp_data[apIns-1].domainNameInfo = NULL;

    if(!confSize || !JSON_STR || (ANSC_STATUS_SUCCESS != CosaDmlWiFi_SetANQPConfig(pCfg,JSON_STR))){
        if(JSON_STR){
            free(JSON_STR);
            JSON_STR = NULL;
        }
        wifi_passpoint_dbg_print(1,"Failed to Initialize ANQP Configuration from memory for AP: %d. Setting Default\n",apIns);
        return CosaDmlWiFi_DefaultANQPConfig(pCfg);
    }
    wifi_passpoint_dbg_print(1,"Initialized ANQP Configuration from memory for AP: %d.\n",apIns);
    pCfg->IEEE80211uCfg.PasspointCfg.ANQPConfigParameters = JSON_STR;
    return ANSC_STATUS_SUCCESS;
}

void CosaDmlWiFi_UpdateANQPVenueInfo(PCOSA_DML_WIFI_AP_CFG pCfg)
{
    int apIns = pCfg->InstanceNumber - 1;

    if((apIns < 0) || (apIns > 15)){
        wifi_passpoint_dbg_print(1, "%s:%d: Invalid AP Index. Return\n", __func__, __LINE__);
        return;
    }

    if(g_anqp_data[apIns].venueInfoLength && g_anqp_data[apIns].venueInfo){
        //Copy Venue Group and Type from Interworking Structure
        wifi_venueNameElement_t *venueElem = (wifi_venueNameElement_t *)g_anqp_data[apIns].venueInfo;
        venueElem->venueGroup = pCfg->IEEE80211uCfg.IntwrkCfg.iVenueGroup;
        venueElem->venueType = pCfg->IEEE80211uCfg.IntwrkCfg.iVenueType;
        wifi_passpoint_dbg_print(1, "%s:%d: Updated VenueNameANQPElement from Interworking\n", __func__, __LINE__);

    }
}

