/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2019 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
   
#include "ssp_global.h"
#include "stdlib.h"
#include "ccsp_dm_api.h"
#include "harvester.h"
#include <sysevent/sysevent.h>
#include <syscfg/syscfg.h>
#include "cosa_apis.h"
#include <libparodus/libparodus.h>
#include "collection.h"
#include <math.h>
#include "webpa_interface.h"
#include <trower-base64/base64.h>
#include "cosa_dbus_api.h"
#include "safec_lib_common.h"

#define MAX_PARAMETERNAME_LEN   512
#define ETH_WAN_STATUS_PARAM "Device.Ethernet.X_RDKCENTRAL-COM_WAN.Enabled"
#define RDKB_ETHAGENT_COMPONENT_NAME                  "com.cisco.spvtg.ccsp.ethagent"
#define RDKB_ETHAGENT_DBUS_PATH                       "/com/cisco/spvtg/ccsp/ethagent"

extern ANSC_HANDLE bus_handle;
static webpa_interface_t	webpa_interface;

static void checkComponentHealthStatus(char * compName, char * dbusPath, char *status, int *retStatus);
static void waitForEthAgentComponentReady();
static int check_ethernet_wan_status();
static void *handle_parodus();
void wifi_dbg_print(int level, char *format, ...);
int s_sysevent_connect (token_t *out_se_token);

#define CCSP_AGENT_WEBPA_SUBSYSTEM         "eRT."

void print_b64_endcoded_buffer	(unsigned char *data, unsigned int size)
{
	uint8_t* b64buffer =  NULL;
  	size_t decodesize = 0;
	unsigned int k;
	errno_t rc = -1;

    /* b64 encoding */
    decodesize = b64_get_encoded_buffer_size(size);
    b64buffer = malloc(decodesize * sizeof(uint8_t));
    b64_encode( (uint8_t*)data, size, b64buffer);

	wifi_dbg_print(1, "\nAVro serialized data\n");
    for (k = 0; k < size ; k++)
    {
      	char buf[30];
      	if ( ( k % 32 ) == 0 )
			wifi_dbg_print(1, "\n");
		rc = sprintf_s(buf, sizeof(buf), "%02X", (unsigned char)data[k]);
		if(rc < EOK)
		{
			ERR_CHK(rc);
		}
		wifi_dbg_print(1, "%c%c", buf[0], buf[1]);
    }
	
	wifi_dbg_print(1, "\n\nB64 data\n");

    for (k = 0; k < decodesize; k++)
    {
      	if ( ( k % 32 ) == 0 )
			wifi_dbg_print(1, "\n");
		wifi_dbg_print(1, "%c", b64buffer[k]);
    }
    
	wifi_dbg_print(1, "\n\n");
    
	free(b64buffer);

}

static void *handle_parodus(void *arg)
{
    struct timespec time_to_wait;
    struct timeval tv_now;
    int rc = -1, ret = 0;
    wrp_msg_t *wrp_msg ;
    webpa_interface_t *interface = (webpa_interface_t *)arg;
    int count = 0;

    pthread_detach(pthread_self());

    while (interface->thread_exit == false) {
	gettimeofday(&tv_now, NULL);
			
	time_to_wait.tv_nsec = 0;
	time_to_wait.tv_sec = tv_now.tv_sec + 120;

	pthread_mutex_lock(&interface->lock);
        rc = pthread_cond_timedwait(&interface->cond, &interface->lock, &time_to_wait);
		
	if ((rc == ETIMEDOUT) || (rc == 0)) {

		// get the data from the queue and try to send all the messages
		while ((count = queue_count(interface->queue)) >= 0) {

			wifi_dbg_print(1, "%s:%d: Queue count:%d\n", __func__, __LINE__, count);
			if (count == 0) {
				break;	
			}

			wrp_msg = queue_peek(interface->queue, (uint32_t)(count - 1));
			if (wrp_msg == NULL) {
				assert(0);
			}
			
			wifi_dbg_print(1, "Source:%s Destination:%s Content Type:%s\n", wrp_msg->u.event.source, wrp_msg->u.event.dest, wrp_msg->u.event.content_type);
			//print_b64_endcoded_buffer(wrp_msg->u.event.payload, wrp_msg->u.event.payload_size);

			ret = libparodus_send(interface->client_instance, wrp_msg);
			if (ret != 0) {
				CcspTraceError(("Parodus send failed: '%s'\n",libparodus_strerror(ret)));	
			}

			queue_remove(interface->queue, (uint32_t)count - 1);									
			free(wrp_msg->u.event.source);
			free(wrp_msg->u.event.dest);
			free(wrp_msg->u.event.content_type);	
			free(wrp_msg->u.event.payload);
	       		free(wrp_msg);
		}
		pthread_mutex_unlock(&interface->lock);

	}
    }

	rc = libparodus_shutdown(interface->client_instance);
    
	return 0;
}
void sendWebpaMsg(char *serviceName, char *dest, char *trans_id, char *contentType, char *payload, unsigned int payload_len)
{
    wrp_msg_t *wrp_msg ;
    char source[MAX_PARAMETERNAME_LEN/2] = {'\0'};

    if ((serviceName == NULL) || (dest == NULL) || (trans_id == NULL) || (contentType == NULL) || (payload == NULL)) {
	return;
    }
    
    pthread_mutex_lock(&webpa_interface.lock);

    snprintf(source, sizeof(source), "mac:%s/%s", webpa_interface.deviceMAC, serviceName);
    
    wrp_msg = (wrp_msg_t *)malloc(sizeof(wrp_msg_t));

    memset(wrp_msg, 0, sizeof(wrp_msg_t));
    wrp_msg->msg_type = WRP_MSG_TYPE__EVENT;
    wrp_msg->u.event.payload = (void *)payload;
    wrp_msg->u.event.payload_size = payload_len;
    wrp_msg->u.event.source = strdup(source);
    wrp_msg->u.event.dest = strdup(dest);
    wrp_msg->u.event.content_type = strdup(contentType);

    queue_push(webpa_interface.queue, wrp_msg);

    pthread_cond_signal(&webpa_interface.cond);

    pthread_mutex_unlock(&webpa_interface.lock);
}

int initparodusTask()
{
    int ret = 0;
    int backoffRetryTime = 0;
    int backoff_max_time = 9;
    int max_retry_sleep;
    //Retry Backoff count shall start at c=2 & calculate 2^c - 1.
    int c = 2;
	char *parodus_url = NULL;
	
	pthread_mutex_init(&webpa_interface.lock, NULL);
	pthread_mutex_init(&webpa_interface.device_mac_mutex, NULL);
	pthread_cond_init(&webpa_interface.cond, NULL);

	memset(webpa_interface.deviceMAC, 0, 32);

	webpa_interface.queue = queue_create();
	if (webpa_interface.queue == NULL) {
		pthread_mutex_destroy(&webpa_interface.lock);
		pthread_mutex_destroy(&webpa_interface.device_mac_mutex);
		return -1;
	}
        
	get_parodus_url(&parodus_url);
    max_retry_sleep = (int) pow(2, backoff_max_time) -1;


	if (parodus_url != NULL)
	{
		libpd_cfg_t cfg1 = {.service_name = "CcspWifiSsp",
						.receive = false, .keepalive_timeout_secs = 0,
						.parodus_url = parodus_url,
						.client_url = NULL
					   };
		   
		while(1)
		{
		    if (backoffRetryTime < max_retry_sleep) {
		        backoffRetryTime = (int) pow(2, c) -1;
		    } else {
				// give up trying to initialize parodus
				return -1;
			}
			ret = libparodus_init (&webpa_interface.client_instance, &cfg1);

		    if (ret == 0)
		    {
			CcspTraceInfo(("Init for parodus Success..!!\n"));
		        break;
		    }
		    else
		    {
			CcspTraceError(("Init for parodus failed: '%s'\n",libparodus_strerror(ret)));
		        sleep(backoffRetryTime);
		        c++;
		    }
		}
	}
	
	webpa_interface.thread_exit = false;
	
    if (pthread_create(&webpa_interface.parodusThreadId, NULL, handle_parodus, &webpa_interface) != 0) {
		return -1;
	}

	return 0;
}

static void waitForEthAgentComponentReady()
{
    char status[32] = {'\0'};
    int count = 0;
    int ret = -1;
    while(1)
    {
        checkComponentHealthStatus(RDKB_ETHAGENT_COMPONENT_NAME, RDKB_ETHAGENT_DBUS_PATH, status,&ret);
        if(ret == CCSP_SUCCESS && (strcmp(status, "Green") == 0))
        {
            break;
        }
        else
        {
            count++;
            if(count > 60)
            {
                break;
            }
            sleep(5);
        }
    }
}

static void checkComponentHealthStatus(char * compName, char * dbusPath, char *status, int *retStatus)
{
	int ret = 0, val_size = 0;
	parameterValStruct_t **parameterval = NULL;
	char *parameterNames[1] = {};
	char tmp[MAX_PARAMETERNAME_LEN];
	char str[MAX_PARAMETERNAME_LEN/2];     
	char l_Subsystem[MAX_PARAMETERNAME_LEN/2] = { 0 };
	errno_t rc = -1;

	rc = sprintf_s(tmp, sizeof(tmp), "%s.Health",compName);
	if(rc < EOK)
	{
		ERR_CHK(rc);
	}
	parameterNames[0] = tmp;

	rc = strcpy_s(l_Subsystem, sizeof(l_Subsystem), "eRT.");
	if (rc != 0) {
            ERR_CHK(rc);
	    return;
	}
	rc = sprintf_s(str, sizeof(str), "%s%s", l_Subsystem, compName);
	if(rc < EOK) ERR_CHK(rc);

	ret = CcspBaseIf_getParameterValues(bus_handle, str, dbusPath,  parameterNames, 1, &val_size, &parameterval);
	if(ret == CCSP_SUCCESS)
	{
        /*status is a pointer pointing to 32 bytes*/
		rc = strcpy_s(status, 32, parameterval[0]->parameterValue);
		ERR_CHK(rc);
	}
	free_parameterValStruct_t (bus_handle, val_size, parameterval);

	*retStatus = ret;
}

static int check_ethernet_wan_status()
{
    int ret = -1, size =0, val_size =0;
    char compName[MAX_PARAMETERNAME_LEN/2] = { '\0' };
    char dbusPath[MAX_PARAMETERNAME_LEN/2] = { '\0' };
    parameterValStruct_t **parameterval = NULL;
    char *getList[] = {ETH_WAN_STATUS_PARAM};
    componentStruct_t **        ppComponents = NULL;
    char dst_pathname_cr[256] = {0};
    char isEthEnabled[64]={'\0'};
    errno_t rc = -1;
    
    if(0 == syscfg_init())
    {
        if( 0 == syscfg_get( NULL, "eth_wan_enabled", isEthEnabled, sizeof(isEthEnabled)) && (isEthEnabled[0] != '\0' && strncmp(isEthEnabled, "true", strlen("true")) == 0))
        {
            ret = CCSP_SUCCESS;
        }
    }
    else
    {
        waitForEthAgentComponentReady();
        rc = sprintf_s(dst_pathname_cr, sizeof(dst_pathname_cr), "eRT.%s", CCSP_DBUS_INTERFACE_CR);
        if(rc < EOK)
        {
            ERR_CHK(rc);
        }
        ret = CcspBaseIf_discComponentSupportingNamespace(bus_handle, dst_pathname_cr, ETH_WAN_STATUS_PARAM, "", &ppComponents, &size);
        if ( ret == CCSP_SUCCESS && size >= 1)
        {
            strncpy(compName, ppComponents[0]->componentName, sizeof(compName)-1);
            strncpy(dbusPath, ppComponents[0]->dbusPath, sizeof(compName)-1);
        }
        else
        {
        }
        free_componentStruct_t(bus_handle, size, ppComponents);

        if(strlen(compName) != 0 && strlen(dbusPath) != 0)
        {
            ret = CcspBaseIf_getParameterValues(bus_handle, compName, dbusPath, getList, 1, &val_size, &parameterval);
            if(ret == CCSP_SUCCESS && val_size > 0)
            {
                if(parameterval[0]->parameterValue != NULL && strncmp(parameterval[0]->parameterValue, "true", strlen("true")) == 0)
                {
                    ret = CCSP_SUCCESS;
                }
                else
                {
                    ret = CCSP_FAILURE;
                }
            }
            else
            {
            }
            free_parameterValStruct_t(bus_handle, val_size, parameterval);
        }
    }
    return ret;
}

char * getDeviceMac()
{

    while(!strlen(webpa_interface.deviceMAC))
    {
        pthread_mutex_lock(&webpa_interface.device_mac_mutex);
        int ret = -1, val_size =0,cnt =0, fd = 0;
        parameterValStruct_t **parameterval = NULL;
	char* dstComp = NULL, *dstPath = NULL;
#if defined(_COSA_BCM_MIPS_)
#define GETLIST "Device.DPoE.Mac_address"
	char* getList1[] = {GETLIST};
#else
#if defined (_HUB4_PRODUCT_REQ_) || defined(_SR300_PRODUCT_REQ_)
#define GETLIST "Device.DeviceInfo.X_COMCAST-COM_WAN_MAC"
        char* getList1[] = {GETLIST};
#else
#define GETLIST "Device.X_CISCO_COM_CableModem.MACAddress"
        char* getList1[] = {GETLIST};
#endif
#endif /*_COSA_BCM_MIPS_*/
        token_t  token;
        char deviceMACValue[32] = { '\0' };

        if (strlen(webpa_interface.deviceMAC))
        {
            pthread_mutex_unlock(&webpa_interface.device_mac_mutex);
            break;
        }

        fd = s_sysevent_connect(&token);
        if(CCSP_SUCCESS == check_ethernet_wan_status() && sysevent_get(fd, token, "eth_wan_mac", deviceMACValue, sizeof(deviceMACValue)) == 0 && deviceMACValue[0] != '\0')
        {
            AnscMacToLower(webpa_interface.deviceMAC, deviceMACValue, sizeof(webpa_interface.deviceMAC));
        }
        else
        {
            if(!Cosa_FindDestComp(GETLIST, &dstComp, &dstPath) || !dstComp || !dstPath)
            {
                pthread_mutex_unlock(&webpa_interface.device_mac_mutex);
                sleep(10);
                /*CID: 54087,60662 Resource leak*/
                if(dstComp)
                {
                    AnscFreeMemory(dstComp);
                }
                if(dstPath)
                {
                    AnscFreeMemory(dstPath);
                }

                continue;
            }            

            ret = CcspBaseIf_getParameterValues(bus_handle,
                        dstComp, dstPath,
                        getList1,
                        1, &val_size, &parameterval);

            /*CID: 54087,60662 Resource leak*/
            if(dstComp)
            {
                    AnscFreeMemory(dstComp);
            }
            if(dstPath)
            {
                    AnscFreeMemory(dstPath);
            }

            if(ret == CCSP_SUCCESS)
            {
                for (cnt = 0; cnt < val_size; cnt++)
                {
                
                }
                AnscMacToLower(webpa_interface.deviceMAC, parameterval[0]->parameterValue, sizeof(webpa_interface.deviceMAC));
        
            }
            else
            {
			pthread_mutex_unlock(&webpa_interface.device_mac_mutex);
	        	sleep(10);
			free_parameterValStruct_t(bus_handle, val_size, parameterval);
                        continue;

            }
         
            free_parameterValStruct_t(bus_handle, val_size, parameterval);
        }

		pthread_mutex_unlock(&webpa_interface.device_mac_mutex);
    }
        
    return webpa_interface.deviceMAC;
}


