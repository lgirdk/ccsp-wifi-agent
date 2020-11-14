/*********************************************************************
 * Copyright 2017-2019 ARRIS Enterprises, LLC.
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
 **********************************************************************/
#define _GNU_SOURCE
#include "ctype.h"
#include "ansc_platform.h"
#include "cosa_wifi_dml.h"
#include "cosa_wifi_internal.h"
#include "plugin_main_apis.h"
#include "wifi_hal.h"
#include "cosa_lgi_wifi_wifilog_dml.h"
#include <sys/time.h>
#include <sys/stat.h>

/***********************************************************************
 IMPORTANT NOTE:

 According to TR69 spec:
 On successful receipt of a SetParameterValues RPC, the CPE MUST apply
 the changes to all of the specified Parameters atomically. That is, either
 all of the value changes are applied together, or none of the changes are
 applied at all. In the latter case, the CPE MUST return a fault response
 indicating the reason for the failure to apply the changes.

 The CPE MUST NOT apply any of the specified changes without applying all
 of them.

 In order to set parameter values correctly, the back-end is required to
 hold the updated values until "Validate" and "Commit" are called. Only after
 all the "Validate" passed in different objects, the "Commit" will be called.
 Otherwise, "Rollback" will be called instead.

 The sequence in COSA Data Model will be:

 SetParamBoolValue/SetParamIntValue/SetParamUlongValue/SetParamStringValue
 -- Backup the updated values;

 if( Validate_XXX())
 {
     Commit_XXX();    -- Commit the update all together in the same object
 }
 else
 {
     Rollback_XXX();  -- Remove the update at backup;
 }

***********************************************************************/
#define WIFI_LOG_REFRESH_INTERVAL 20
#define WIFI_EVENT_LOG_SIZE 100
#ifdef WIFI_EVENT_LOG_RDKLOGGER
 /* Event Log format: <time> <mod> <lv> <tid> <ssid> <info> */
#define WIFI_LOG_LINE_FORMAT "%127s %127s %127s %127s %127s %511[^\t\n]"
#else
/* Event Log format: <date> <time> <lv> <mod> <ssid> <info> */
/* eg. 01/28/2019 01:54:02 [info][Wifi][[ssid=1] <info> */
#define WIFI_LOG_LINE_FORMAT "%127s %127s [%127[^]]][%127[^]]][[%127[^]]] %511[^\t\n]"
#endif
#define WIFI_LOG_STR_SIZE 128
#define WIFI_LOG_PARAM_NUM_IN_LINE 6
#ifdef WIFI_EVENT_LOG_RDKLOGGER
#define LGI_WIFI_LOG_FILE_0 "/nvram/log/WifiEventLog.txt.0"
#else
#define LGI_WIFI_LOG_FILE_0 "/nvram/log/WifiEventLog.txt"
#endif
#define LGI_WIFI_LOG_FILE_1 "/nvram/log/WifiEventLog.txt.1"
#define LGI_WIFI_LOG_FILE_TEMP "/var/tmp/WifiEventLogTemp"
#define RDK_LOG_FATAL 0
#define RDK_LOG_ERROR 1
#define RDK_LOG_WARN 2
#define RDK_LOG_NOTICE 3
#define RDK_LOG_INFO 4
#define RDK_LOG_DEBUG 5
#define RDK_LOG_TRACE 6
#define LOG_FATAL_STR   "FATAL"
#define LOG_ERROR_STR  "ERROR"
#define LOG_WARN_STR   "WARN"
#define LOG_NOTICE_STR "NOTICE"
#define LOG_INFO_STR     "INFO"
#define LOG_DEBUG_STR  "DEBUG"
#define LOG_TRACE_STR   "TRACE"
#define SYSLOG_EMERG_STR  "emerg"
#define SYSLOG_ALERT_STR  "alert"
#define SYSLOG_CRIT_STR   "crit"
#define SYSLOG_ERROR_STR  "error"
#define SYSLOG_WARN_STR   "warn"
#define SYSLOG_NOTICE_STR "notice"
#define SYSLOG_INFO_STR   "info"
#define SYSLOG_DEBUG_STR  "debug"

#define WIFI_CHANNEL_CHANGE_STR "Channel Changed to channel "
#ifdef WIFI_EVENT_LOG_RDKLOGGER
#define WIFI_CHANNEL_CHANGE_LINE_FORMAT WIFI_CHANNEL_CHANGE_STR"%u, reason: %s[^\t\n]"
#else
#define WIFI_CHANNEL_CHANGE_LINE_FORMAT WIFI_CHANNEL_CHANGE_STR"%u, reason: %8[^]]"
#endif
#define WIFI_CHANNEL_CHANGE_PARAM_NUM_IN_LINE 2

#define SIZE_OF_ARRAY(x) (sizeof(x) / sizeof((x)[0]))
static int g_WiFiLog_clean_flg = 0;
static ulong logupdatetime;
typedef struct _acs_reason
{
    char *reasoncode;
    char *reasonstring;
} acs_reason_t;

static acs_reason_t acsReason[] =
{
    {"1001", "bootup"},
    {"1002", "manually"},
    {"1003", "auto sched"},
    {"1004", "dynamic int reached"},
    {"1101", "dfs detect"},
    {"1102", "dfs move back"},
    {"2001", "idle check failed"},
    {"3001", "unspecified error"}
};

static int translateReasonCode4ChChangeLog(const char *reasoncode, char *reasonstring);

#ifdef WIFI_EVENT_LOG_RDKLOGGER
static int getLogLevel(char *logStr)
{
    if(strstr(logStr, LOG_TRACE_STR))
    {
        return RDK_LOG_TRACE;
    }
    else if (strstr(logStr, LOG_DEBUG_STR))
    {
        return RDK_LOG_DEBUG;
    }
    else if (strstr(logStr, LOG_INFO_STR))
    {
        return RDK_LOG_INFO;
    }
    else if (strstr(logStr, LOG_NOTICE_STR))
    {
        return RDK_LOG_NOTICE;
    }
    else if (strstr(logStr, LOG_WARN_STR))
    {
        return RDK_LOG_WARN;
    }
    else if (strstr(logStr, LOG_ERROR_STR))
    {
        return RDK_LOG_ERROR;
    }
    else if (strstr(logStr, LOG_FATAL_STR))
    {
        return RDK_LOG_FATAL;
    }
    return -1;
}
#else //WIFI_EVENT_LOG_RDKLOGGER
/*
 For backwards compatible with RDK logger levels, we map syslog level as below:
   emerg, alert, crit -> RDK_LOG_FATAL
   err -> RDK_LOG_ERROR
   warning -> RDK_LOG_WARN
   notice -> RDK_LOG_NOTICE
   info -> RDK_LOG_INFO
   debug -> RDK_LOG_DEBUG
*/
static int getSyslogLevel(char *logStr)
{
    if (strstr(logStr, SYSLOG_DEBUG_STR))
    {
        return RDK_LOG_DEBUG;
    }
    else if (strstr(logStr, SYSLOG_INFO_STR))
    {
        return RDK_LOG_INFO;
    }
    else if (strstr(logStr, SYSLOG_NOTICE_STR))
    {
        return RDK_LOG_NOTICE;
    }
    else if (strstr(logStr, SYSLOG_WARN_STR))
    {
        return RDK_LOG_WARN;
    }
    else if (strstr(logStr, SYSLOG_ERROR_STR))
    {
        return RDK_LOG_ERROR;
    }
    else if (strstr(logStr, SYSLOG_EMERG_STR) ||
             strstr(logStr, SYSLOG_ALERT_STR) ||
             strstr(logStr, SYSLOG_CRIT_STR) )
    {
        return RDK_LOG_FATAL;
    }
    return -1;
}
#endif //WIFI_EVENT_LOG_RDKLOGGER
static ULONG getLogLines(char *filename)
{
    FILE *fp = NULL;
    char ch = '\0';
    ULONG lines = 0;

    fp = fopen(filename, "r");
    if (fp == NULL)
    {
        return 0;
    }

    while(!feof(fp))
    {
        ch = fgetc(fp);
        if(ch == '\n')
        {
            lines++;
        }
    }
    fclose(fp);
    return lines;
}

ANSC_STATUS
CosaDmlGetWiFiLog
    (
        ANSC_HANDLE                 hContext,
        PULONG                      pulCount,
        PCOSA_DML_WIFILOG_FULL    *ppConf
    )
{
    int log_max_num = 0;
    int count = 0;
    int i = 0;
    int param_num = 0;
    PCOSA_DML_WIFILOG_FULL pWIFILog = NULL;
    unsigned long long pos = 0;

    char str[WIFI_LOG_PARAM_NUM_IN_LINE*WIFI_LOG_STR_SIZE] = {0};
    char cmd[WIFI_LOG_STR_SIZE] = {0};
    char time[WIFI_LOG_STR_SIZE] = {0};
#ifndef WIFI_EVENT_LOG_RDKLOGGER
    char date[WIFI_LOG_STR_SIZE] = {0};
    int len=0;
#endif
    char dump[WIFI_LOG_STR_SIZE] = {0};
    char lv[WIFI_LOG_STR_SIZE] = {0};
    char desc[LGI_WIFI_EVENT_LOG_INFO_LEN] = {0};
    char tmp[WIFI_LOG_STR_SIZE] = {0};
    FILE *fp = NULL;
    int log0_lines =  0;
    int log1_lines =  0;

    char *desc_tmp;
    int event_id = 0;
    if (access(LGI_WIFI_LOG_FILE_0, F_OK) == 0)
    {
        log0_lines = getLogLines(LGI_WIFI_LOG_FILE_0);
    }
    if (access(LGI_WIFI_LOG_FILE_1, F_OK) == 0)
    {
        log1_lines = getLogLines(LGI_WIFI_LOG_FILE_1);
    }
    if((!pulCount) || (!ppConf) || log0_lines == 0)
    {
        return ANSC_STATUS_FAILURE;
    }
    if (log0_lines >= WIFI_EVENT_LOG_SIZE || log1_lines == 0)
    {
        sprintf(cmd, "cat %s > %s", LGI_WIFI_LOG_FILE_0, LGI_WIFI_LOG_FILE_TEMP);
    }
    else
    {
        sprintf(cmd, "cat %s %s > %s", LGI_WIFI_LOG_FILE_1, LGI_WIFI_LOG_FILE_0, LGI_WIFI_LOG_FILE_TEMP);
    }
    system(cmd);
    log_max_num =( log0_lines + log1_lines < WIFI_EVENT_LOG_SIZE) ? (log0_lines + log1_lines) : (WIFI_EVENT_LOG_SIZE);
    pWIFILog= (PCOSA_DML_WIFILOG_FULL)AnscAllocateMemory(log_max_num * sizeof(COSA_DML_WIFILOG_FULL));
    if(pWIFILog== NULL)
    {
        unlink(LGI_WIFI_LOG_FILE_TEMP);
        return ANSC_STATUS_FAILURE;
    }
    fp = fopen(LGI_WIFI_LOG_FILE_TEMP, "r");
    if(!fp)
    {
        AnscFreeMemory(pWIFILog);
        unlink(LGI_WIFI_LOG_FILE_TEMP);
        return ANSC_STATUS_FAILURE;
    }
    if (fseek(fp, 0, SEEK_END))
    {
        AnscFreeMemory(pWIFILog);
        fclose(fp);
        unlink(LGI_WIFI_LOG_FILE_TEMP);
        return ANSC_STATUS_FAILURE;
    }
    else
    {
        pos = ftell(fp);
        while (pos)
        {
            if (!fseek(fp, --pos, SEEK_SET))
            {
                if (fgetc(fp) == '\n')
                {
                    if (count++ == WIFI_EVENT_LOG_SIZE)
                    {
                        break;
                    }
                }
            }
        }
        while (fgets(str, sizeof(str), fp))
        {
            if (i >= log_max_num)
            {
                break;
            }
#ifdef WIFI_EVENT_LOG_RDKLOGGER
            /* Event Log format: <time> <mod> <lv> <tid> <ssid> <info> */
            param_num = sscanf(str, WIFI_LOG_LINE_FORMAT, time, dump, lv, dump, dump, desc);
            if (param_num != WIFI_LOG_PARAM_NUM_IN_LINE)
            {
                //ignore this line if the format is not correct
                continue;
            }
            struct tm timeStruct;
            /* add 20xx for year string since wifi log's time
               only shows latest two numbers of year.*/
            sprintf(tmp, "20%s", time);
            if (strptime(tmp, "%Y%m%d-%H:%M:%S", &timeStruct) != NULL)
            {
                strftime(time, sizeof(time), "%Y-%m-%d,%H:%M:%S.0", &timeStruct);
            }
            strncpy(pWIFILog[i].Time,time,LGI_WIFI_EVENT_LOG_TIME_LEN);
#else
            /* Event Log format: <date> <time> <lv> <mod> <ssid> <info> */
            param_num = sscanf(str, WIFI_LOG_LINE_FORMAT, date, time, lv, dump, dump, desc);
            if (param_num != WIFI_LOG_PARAM_NUM_IN_LINE)
            {
                //ignore this line if the format is not correct
                continue;
            }
            len=strlen(desc);
            if(len< LGI_WIFI_EVENT_LOG_INFO_LEN)
            {
              desc[len-1]='\0'; // Remove last ']' character
            }
            snprintf(tmp,LGI_WIFI_EVENT_LOG_TIME_LEN,"%s,%s.0", date,time);
            strncpy(pWIFILog[i].Time,tmp,LGI_WIFI_EVENT_LOG_TIME_LEN);
#endif

            pWIFILog[i].Index = i+1;
            pWIFILog[i].EventID = 0; //TODO - current wifi event log did not provide EventID
#ifdef WIFI_EVENT_LOG_RDKLOGGER
            pWIFILog[i].EventLevel = getLogLevel(lv);
#else
            pWIFILog[i].EventLevel = getSyslogLevel(lv);
#endif
            desc_tmp = strstr(desc, "ID:");
            if (desc_tmp != NULL)
            {
               sscanf(desc_tmp, "ID: %d", &event_id);
            }
            else
            {
               event_id = 0;
            }
            pWIFILog[i].EventID = event_id;
            strncpy(pWIFILog[i].Description, desc, LGI_WIFI_EVENT_LOG_INFO_LEN);
            i++;
        }
    }
    fclose(fp);
    unlink(LGI_WIFI_LOG_FILE_TEMP);
    *pulCount = i;
    *ppConf = pWIFILog;
    return ANSC_STATUS_SUCCESS;
}
/***********************************************************************

 APIs for Object:

    X_LGI_COM_WiFiLog.

    *  X_LGI_COM_WiFiLog_GetParamUlongValue
    *  X_LGI_COM_WiFiLog_SetParamUlongValue

***********************************************************************/
/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        X_LGI_COM_WiFiLog_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
X_LGI_COM_WiFiLog_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    if (strcmp(ParamName, "LogControl") == 0)
    {
        *puLong = 0; //default value of arrisRouterWiFieventlogcontrol
	return TRUE;
    }
    return FALSE;
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        X_LGI_COM_WiFiLog_SetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG                       uValue
            );

    description:

        This function is called to set ULONG parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG                       uValue
                The updated ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
X_LGI_COM_WiFiLog_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    )
{
    if (strcmp(ParamName, "LogControl") == 0)
    {
        if (uValue == 1)
        {
            g_WiFiLog_clean_flg = 1;
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }
    return FALSE;
}

/***********************************************************************

 APIs for Object:

    X_LGI_COM_WiFiLog.WiFiLog

    *  WiFiLog_GetEntryCount
    *  WiFiLog_GetEntry
    *  WiFiLog_IsUpdated
    *  WiFiLog_Synchronize
    *  WiFiLog_GetParamUlongValue
    *  WiFiLog_GetParamStringValue

***********************************************************************/
/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        WiFiLog_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/
ULONG
WiFiLog_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    return pMyObject->WiFiLogEntryCount;
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_HANDLE
        WiFiLog_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/
ANSC_HANDLE
WiFiLog_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;

    if (nIndex < pMyObject->WiFiLogEntryCount)
    {
        *pInsNumber  = nIndex + 1;
        return  &pMyObject->pWiFiLogTable[nIndex];
    }
    return NULL; /* return the handle */
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        WiFiLog_IsUpdated
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is checking whether the table is updated or not.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     TRUE or FALSE.

**********************************************************************/
BOOL
WiFiLog_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    if ( !logupdatetime || g_WiFiLog_clean_flg == 1 )
    {
        logupdatetime = AnscGetTickInSeconds();
        g_WiFiLog_clean_flg = 0;
        return TRUE;
    }
    if ( logupdatetime >= TIME_NO_NEGATIVE(AnscGetTickInSeconds() - WIFI_LOG_REFRESH_INTERVAL) )
    {
        return FALSE;
    }
    else
    {
        logupdatetime = AnscGetTickInSeconds();
        return TRUE;
    }
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        WiFiLog_Synchronize
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to synchronize the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
WiFiLog_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    ANSC_STATUS                     ret           = ANSC_STATUS_SUCCESS;
    if ( pMyObject->pWiFiLogTable )
    {
        AnscFreeMemory(pMyObject->pWiFiLogTable);
        pMyObject->pWiFiLogTable = NULL;
        pMyObject->WiFiLogEntryCount = 0;
    }
    ret = CosaDmlGetWiFiLog
        (
            (ANSC_HANDLE)NULL,
            &pMyObject->WiFiLogEntryCount,
            &pMyObject->pWiFiLogTable
        );
    if ( ret != ANSC_STATUS_SUCCESS )
    {
        pMyObject->pWiFiLogTable = NULL;
        pMyObject->WiFiLogEntryCount = 0;
    }
    return ANSC_STATUS_SUCCESS;
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        WiFiLog_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WiFiLog_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
     PCOSA_DML_WIFILOG_FULL        pWifiLogInfo        = (PCOSA_DML_WIFILOG_FULL)hInsContext;
     if (strcmp(ParamName, "Index") == 0)
     {
         *puLong = pWifiLogInfo->Index;
         return TRUE;
     }

    if (strcmp(ParamName, "EventID") == 0)
    {
        *puLong = pWifiLogInfo->EventID;
        return TRUE;
    }
    if (strcmp(ParamName, "EventLevel") == 0)
    {
        *puLong = pWifiLogInfo->EventLevel;
        return TRUE;
    }
    return FALSE;
}
/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        WiFiLog_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
WiFiLog_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
     PCOSA_DML_WIFILOG_FULL        pWifiLogInfo        = (PCOSA_DML_WIFILOG_FULL)hInsContext;

    if (strcmp(ParamName, "Description") == 0)
    {
        if ( _ansc_strlen(pWifiLogInfo->Description) >= *pUlSize )
        {
            *pUlSize = _ansc_strlen(pWifiLogInfo->Description);
            return 1;
        }
        AnscCopyString(pValue, pWifiLogInfo->Description);
        return 0;
    }
    if (strcmp(ParamName, "Time") == 0)
    {
        if ( _ansc_strlen(pWifiLogInfo->Time) >= *pUlSize )
        {
            *pUlSize = _ansc_strlen(pWifiLogInfo->Time);
            return 1;
        }
        AnscCopyString(pValue, pWifiLogInfo->Time);
        return 0;
    }
    return -1;
}

static int translateReasonCode4ChChangeLog(const char *reasoncode, char *reasonstring)
{
    int i = 0, size = 0;

    if ( !reasoncode || !reasonstring ) return -1;

    size = SIZE_OF_ARRAY(acsReason);

    for ( i=0; i<size; i++ )
    {
        if ( !strcmp(acsReason[i].reasoncode, reasoncode) )
        {
            sprintf(reasonstring, "%s", acsReason[i].reasonstring);
            return 0;
        }
    }

    return -1;
}

ANSC_STATUS
CosaDmlGetWiFiChannelChangeLog(char *filename)
{
    ANSC_STATUS                         returnStatus    = ANSC_STATUS_SUCCESS;
    PCOSA_DATAMODEL_WIFI                pWifiObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_DML_WIFI_CH_CHANGE_LOG_FULL   pWifiLogEntry   = NULL;
    PCOSA_CONTEXT_LINK_OBJECT           pCxtLink        = NULL;
    char str[WIFI_LOG_PARAM_NUM_IN_LINE*WIFI_LOG_STR_SIZE] = {0};
#ifndef WIFI_EVENT_LOG_RDKLOGGER
    char date[WIFI_LOG_STR_SIZE] = {0};
#endif
    char time[WIFI_LOG_STR_SIZE] = {0};
    char dump[WIFI_LOG_STR_SIZE] = {0};
    char lv[WIFI_LOG_STR_SIZE] = {0};
    char desc[WIFI_LOG_STR_SIZE] = {0};
    char tmp[WIFI_LOG_STR_SIZE] = {0};
    char reason[WIFI_LOG_STR_SIZE] = {0};
    char reasonstring[WIFI_LOG_STR_SIZE] = {0};
#ifdef WIFI_EVENT_LOG_RDKLOGGER
    struct tm timeStruct = {0};
#endif
    FILE *fpWifiLog = NULL;
    unsigned int channel = 0;
    unsigned int param_num = 0;

    fpWifiLog = fopen(filename, "r");
    if (!fpWifiLog)
    {
        return ANSC_STATUS_FAILURE;
    }

    while ((!feof(fpWifiLog)) && (fgets(str, sizeof(str), fpWifiLog)))
    {
        if (strstr(str, WIFI_CHANNEL_CHANGE_STR) == NULL)
        {
            // Ignore this line since it is not Channel Change log.
            continue;
        }

#ifdef WIFI_EVENT_LOG_RDKLOGGER
        /* Event Log format: <time> <mod> <lv> <tid> <ssid> <info> */
        param_num = sscanf(str, WIFI_LOG_LINE_FORMAT, time, dump, lv, dump, dump, desc);
        if (param_num != WIFI_LOG_PARAM_NUM_IN_LINE)
        {
            // Ignore this line if the format is not correct
            continue;
        }
#else
        /* Event Log format: <date> <time> <lv> <mod> <ssid> <info> */
        param_num = sscanf(str, WIFI_LOG_LINE_FORMAT, date, time, lv, dump, dump, desc);
        if (param_num != WIFI_LOG_PARAM_NUM_IN_LINE)
        {
            //ignore this line if the format is not correct
            continue;
        }
#endif

        param_num = sscanf(desc, WIFI_CHANNEL_CHANGE_LINE_FORMAT, &channel, reason);
        if (param_num != WIFI_CHANNEL_CHANGE_PARAM_NUM_IN_LINE)
        {
            // Ignore this line since it is not Channel Change log.
            continue;
        }

        /* Translate the reason code to reason string, making it readable */
        if ( !translateReasonCode4ChChangeLog(reason, reasonstring) )
        {
            sprintf(reason, "%s", reasonstring);
        }

        /* Create a new entry to store WiFi Channel Change Log. */
        pWifiLogEntry = (PCOSA_DML_WIFI_CH_CHANGE_LOG_FULL)AnscAllocateMemory(sizeof(COSA_DML_WIFI_CH_CHANGE_LOG_FULL));
        if (pWifiLogEntry == NULL)
        {
            returnStatus = ANSC_STATUS_RESOURCES;
            break;
        }

        pWifiLogEntry->Channel = channel;
        strncpy(pWifiLogEntry->Reason, reason, LGI_WIFI_EVENT_LOG_INFO_LEN);

#ifdef WIFI_EVENT_LOG_RDKLOGGER

        /* Add 20xx for year string since wifi log's time
           only shows latest two numbers of year.*/
        sprintf(tmp, "20%s", time);
        if (strptime(tmp, "%Y%m%d-%H:%M:%S", &timeStruct) != NULL)
        {
            strftime(time, sizeof(time), "%Y-%m-%d,%H:%M:%S.0", &timeStruct);
        }
        strncpy(pWifiLogEntry->Time,time,LGI_WIFI_EVENT_LOG_TIME_LEN);
#else
        snprintf(tmp,LGI_WIFI_EVENT_LOG_TIME_LEN,"%s,%s.0", date,time);
        strncpy(pWifiLogEntry->Time,tmp,LGI_WIFI_EVENT_LOG_TIME_LEN);
#endif
        /* Create a new node for linked list. */
        pCxtLink = AnscAllocateMemory(sizeof(COSA_CONTEXT_LINK_OBJECT));
        if (!pCxtLink)
        {
            AnscFreeMemory(pWifiLogEntry);
            returnStatus = ANSC_STATUS_RESOURCES;
            break;
        }

        /* Assign the content to the node with WiFi log entry. */
        if (pWifiObject->uWiFiChChangeLogNextInsNum == 0)
        {
            pWifiObject->uWiFiChChangeLogNextInsNum = 1;
        }
        pCxtLink->InstanceNumber = pWifiObject->uWiFiChChangeLogNextInsNum;
        pWifiLogEntry->Index = pWifiObject->uWiFiChChangeLogNextInsNum;
        pWifiObject->uWiFiChChangeLogNextInsNum++;

        pCxtLink->hContext = (ANSC_HANDLE)pWifiLogEntry;
        pCxtLink->hParentTable = NULL;

        /* Push the new node into linked list. */
        CosaSListPushEntryByInsNum((PSLIST_HEADER)&pWifiObject->pWiFiChChangeLogList, (PCOSA_CONTEXT_LINK_OBJECT)pCxtLink);
    }
    fclose(fpWifiLog);

    return returnStatus;
}
/***********************************************************************

 APIs for Object:

    X_LGI_COM_WiFiLog.WiFiChannelChangeLog

    *  WiFiChannelChangeLog_GetEntryCount
    *  WiFiChannelChangeLog_GetEntry
    *  WiFiChannelChangeLog_IsUpdated
    *  WiFiChannelChangeLog_Synchronize
    *  WiFiChannelChangeLog_GetParamUlongValue
    *  WiFiChannelChangeLog_GetParamStringValue

***********************************************************************/
/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        WiFiChannelChangeLog_GetEntryCount
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to retrieve the count of the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The count of the table

**********************************************************************/
ULONG
WiFiChannelChangeLog_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI                pWifiObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    return AnscSListQueryDepth(&pWifiObject->pWiFiChChangeLogList);
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ANSC_HANDLE
        WiFiChannelChangeLog_GetEntry
            (
                ANSC_HANDLE                 hInsContext,
                ULONG                       nIndex,
                ULONG*                      pInsNumber
            );

    description:

        This function is called to retrieve the entry specified by the index.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                ULONG                       nIndex,
                The index of this entry;

                ULONG*                      pInsNumber
                The output instance number;

    return:     The handle to identify the entry

**********************************************************************/
ANSC_HANDLE
WiFiChannelChangeLog_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_DATAMODEL_WIFI                pWifiObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT           pLinkObj    = NULL;
    PSINGLE_LINK_ENTRY                  pSLinkEntry = NULL;

    pSLinkEntry = AnscQueueGetEntryByIndex((ANSC_HANDLE)&pWifiObject->pWiFiChChangeLogList, nIndex);
    if (pSLinkEntry)
    {
        pLinkObj = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSLinkEntry);
        *pInsNumber = pLinkObj->InstanceNumber;
        return (PCOSA_DML_WIFI_CH_CHANGE_LOG_FULL)pLinkObj->hContext;
    }
    return NULL;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        WiFiChannelChangeLog_IsUpdated
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is checking whether the table is updated or not.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     TRUE or FALSE.

**********************************************************************/
BOOL
WiFiChannelChangeLog_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DATAMODEL_WIFI            pMyObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    if ( !logupdatetime || g_WiFiLog_clean_flg == 1 )
    {
        logupdatetime = AnscGetTickInSeconds();
        g_WiFiLog_clean_flg = 0;
        return TRUE;
    }
    if ( logupdatetime >= TIME_NO_NEGATIVE(AnscGetTickInSeconds() - WIFI_LOG_REFRESH_INTERVAL) )
    {
        return FALSE;
    }
    else
    {
        logupdatetime = AnscGetTickInSeconds();
        return TRUE;
    }
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        WiFiChannelChangeLog_Synchronize
            (
                ANSC_HANDLE                 hInsContext
            );

    description:

        This function is called to synchronize the table.

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

    return:     The status of the operation.

**********************************************************************/
ULONG
WiFiChannelChangeLog_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
    ANSC_STATUS                         ret             = ANSC_STATUS_FAILURE;
    PCOSA_DATAMODEL_WIFI                pWifiObject     = (PCOSA_DATAMODEL_WIFI)g_pCosaBEManager->hWifi;
    PCOSA_CONTEXT_LINK_OBJECT           pCxtLink        = NULL;
    PSINGLE_LINK_ENTRY                  pSListEntry     = NULL;
    PSINGLE_LINK_ENTRY                  pTmpSListEntry  = NULL;

    /* Remove all existing link entries first. */
    pSListEntry =   AnscSListGetFirstEntry(&pWifiObject->pWiFiChChangeLogList);
    while (pSListEntry)
    {
        pCxtLink          = ACCESS_COSA_CONTEXT_LINK_OBJECT(pSListEntry);
        pTmpSListEntry    = pSListEntry;
        pSListEntry       = AnscSListGetNextEntry(pSListEntry);
        AnscSListPopEntryByLink(&pWifiObject->pWiFiChChangeLogList, pTmpSListEntry);
        AnscFreeMemory(pCxtLink->hContext);
        AnscFreeMemory(pCxtLink);
    }
    pWifiObject->uWiFiChChangeLogNextInsNum = 1;

    /* Update WiFi Channel Change Log into linked list. */
    if (access(LGI_WIFI_LOG_FILE_1, F_OK) == 0)
    {
        ret = CosaDmlGetWiFiChannelChangeLog(LGI_WIFI_LOG_FILE_1);
    }
    if (access(LGI_WIFI_LOG_FILE_0, F_OK) == 0)
    {
        ret = CosaDmlGetWiFiChannelChangeLog(LGI_WIFI_LOG_FILE_0);
    }
    return ret;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        BOOL
        WiFiChannelChangeLog_GetParamUlongValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                ULONG*                      puLong
            );

    description:

        This function is called to retrieve ULONG parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                ULONG*                      puLong
                The buffer of returned ULONG value;

    return:     TRUE if succeeded.

**********************************************************************/
BOOL
WiFiChannelChangeLog_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    )
{
    PCOSA_DML_WIFI_CH_CHANGE_LOG_FULL        pWifiChChangeLogInfo    = (PCOSA_DML_WIFI_CH_CHANGE_LOG_FULL)hInsContext;
    if (strcmp(ParamName, "Index") == 0)
    {
        *puLong = pWifiChChangeLogInfo->Index;
        return TRUE;
    }

    if (strcmp(ParamName, "SelectedChannel") == 0)
    {
        *puLong = pWifiChChangeLogInfo->Channel;
        return TRUE;
    }
    return FALSE;
}

/**********************************************************************

    caller:     owner of this object

    prototype:

        ULONG
        WiFiChannelChangeLog_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );

    description:

        This function is called to retrieve string parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                char*                       pValue,
                The string value buffer;

                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;

    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.

**********************************************************************/
ULONG
WiFiChannelChangeLog_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    PCOSA_DML_WIFI_CH_CHANGE_LOG_FULL        pWifiChChangeLogInfo    = (PCOSA_DML_WIFI_CH_CHANGE_LOG_FULL)hInsContext;

    if (strcmp(ParamName, "Reason") == 0)
    {
        if ( _ansc_strlen(pWifiChChangeLogInfo->Reason) >= *pUlSize )
        {
            *pUlSize = _ansc_strlen(pWifiChChangeLogInfo->Reason);
            return 1;
        }
        AnscCopyString(pValue, pWifiChChangeLogInfo->Reason);
        return 0;
    }
    if (strcmp(ParamName, "Time") == 0)
    {
        if ( _ansc_strlen(pWifiChChangeLogInfo->Time) >= *pUlSize )
        {
            *pUlSize = _ansc_strlen(pWifiChChangeLogInfo->Time);
            return 1;
        }
        AnscCopyString(pValue, pWifiChChangeLogInfo->Time);
        return 0;
    }
    return -1;
}
