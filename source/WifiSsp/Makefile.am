##########################################################################
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2015 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################

ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = CcspWifiSsp

CcspWifiSsp_CPPFLAGS = \
    -I$(top_srcdir)/source/TR-181/ml \
    -I$(top_srcdir)/source/TR-181/sbapi \
    -I$(top_srcdir)/include/TR-181/ml \
    -I$(top_srcdir)/source/Custom

CcspWifiSsp_CFLAGS = $(SYSTEMD_CFLAGS)
CcspWifiSsp_SOURCES = ssp_main.c ssp_messagebus_interface.c ssp_action.c dm_pack_datamodel.c
CcspWifiSsp_LDFLAGS = -lccsp_common -lhal_wifi -rdynamic -lsyscfg -lwebconfig_framework -lprint_uptime -lsecure_wrapper -ltelemetry_msgsender $(SYSTEMD_LDFLAGS) -lpthread
