/*
 *  mms_server.c
 *
 *  Copyright 2013 Michael Zillgith
 *
 *  This file is part of libIEC61850.
 *
 *  libIEC61850 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  libIEC61850 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with libIEC61850.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  See COPYING file for the complete license text.
 */

#include "mms_server.h"
#include "mms_server_connection.h"
#include "mms_value_cache.h"
#include "map.h"
#include "thread.h"

struct sMmsServer {
    IsoServer isoServer;
    MmsDevice* device;
    ReadVariableHandler readHandler;
    void* readHandlerParameter;
    WriteVariableHandler writeHandler;
    void* writeHandlerParameter;
    MmsConnectionHandler connectionHandler;
    void* connectionHandlerParameter;
    Map openConnections;
    Map valueCaches;
    bool isLocked;
    Semaphore modelMutex;

    char* vendorName;
    char* modelName;
    char* revision;
};

static Map
createValueCachesForDomains(MmsDevice* device)
{
    Map valueCaches = Map_create();

    int i;
    for (i = 0; i < device->domainCount; i++) {
        MmsValueCache valueCache = MmsValueCache_create(device->domains[i]);
        Map_addEntry(valueCaches, device->domains[i], valueCache);
    }

    return valueCaches;
}

MmsServer
MmsServer_create(IsoServer isoServer, MmsDevice* device)
{
    MmsServer self = (MmsServer) calloc(1, sizeof(struct sMmsServer));

    self->isoServer = isoServer;
    self->device = device;
    self->openConnections = Map_create();
    self->valueCaches = createValueCachesForDomains(device);
    self->isLocked = false;
    self->modelMutex = Semaphore_create(1);

    return self;
}

void
MmsServer_setServerIdentity(MmsServer self, char* vendorName, char* modelName, char* revision)
{
    self->vendorName = vendorName;
    self->modelName = modelName;
    self->revision = revision;
}

char*
MmsServer_getVendorName(MmsServer self)
{
    if (self->vendorName != NULL)
        return self->vendorName;
    else
        return CONFIG_DEFAULT_MMS_VENDOR_NAME;
}

char*
MmsServer_getModelName(MmsServer self)
{
    if (self->modelName != NULL)
        return self->modelName;
    else
        return CONFIG_DEFAULT_MMS_MODEL_NAME;
}

char*
MmsServer_getRevision(MmsServer self)
{
    if (self->revision != NULL)
        return self->revision;
    else
        return CONFIG_DEFAULT_MMS_REVISION;
}

void
MmsServer_lockModel(MmsServer self)
{
    Semaphore_wait(self->modelMutex);
}

void
MmsServer_unlockModel(MmsServer self)
{
    Semaphore_post(self->modelMutex);
}

void
MmsServer_installReadHandler(MmsServer self, ReadVariableHandler readHandler, void* parameter)
{
    self->readHandler = readHandler;
    self->readHandlerParameter = parameter;
}

void
MmsServer_installWriteHandler(MmsServer self, WriteVariableHandler writeHandler, void* parameter)
{
    self->writeHandler = writeHandler;
    self->writeHandlerParameter = parameter;
}

void
MmsServer_installConnectionHandler(MmsServer self, MmsConnectionHandler connectionHandler, void* parameter)
{
    self->connectionHandler = connectionHandler;
    self->connectionHandlerParameter = parameter;
}


static void
closeConnection(void* con)
{
    MmsServerConnection* connection = (MmsServerConnection*) con;

    MmsServerConnection_destroy(connection);
}

static void
deleteSingleCache(MmsValueCache cache)
{
    MmsValueCache_destroy(cache);
}

void
MmsServer_destroy(MmsServer self)
{
    Map_deleteDeep(self->openConnections, false, closeConnection);
    Map_deleteDeep(self->valueCaches, false, (void (*) (void*)) deleteSingleCache);
    Semaphore_destroy(self->modelMutex);
    free(self);
}

MmsValue*
MmsServer_getValueFromCache(MmsServer self, MmsDomain* domain, char* itemId)
{
    MmsValueCache cache = (MmsValueCache) Map_getEntry(self->valueCaches, domain);

    if (cache != NULL) {
        return MmsValueCache_lookupValue(cache, itemId);
    }

    return NULL ;
}

void
MmsServer_insertIntoCache(MmsServer self, MmsDomain* domain, char* itemId, MmsValue* value)
{
    MmsValueCache cache = (MmsValueCache) Map_getEntry(self->valueCaches, domain);

    if (cache != NULL) {
        MmsValueCache_insertValue(cache, itemId, value);
    }
}

MmsDataAccessError
mmsServer_setValue(MmsServer self, MmsDomain* domain, char* itemId, MmsValue* value,
        MmsServerConnection* connection)
{
    MmsDataAccessError indication;

    if (self->writeHandler != NULL) {
        indication = self->writeHandler(self->writeHandlerParameter, domain,
                itemId, value, connection);
    } else {
        MmsValue* cachedValue;

        cachedValue = MmsServer_getValueFromCache(self, domain, itemId);

        if (cachedValue != NULL) {
            MmsValue_update(cachedValue, value);
            indication = DATA_ACCESS_ERROR_SUCCESS;
        } else
            indication = DATA_ACCESS_ERROR_OBJECT_VALUE_INVALID;
    }

    return indication;
}

MmsValue*
mmsServer_getValue(MmsServer self, MmsDomain* domain, char* itemId, MmsServerConnection* connection)
{
    MmsValue* value = NULL;

    value = MmsServer_getValueFromCache(self, domain, itemId);

    if (value == NULL)
        if (self->readHandler != NULL)
            value = self->readHandler(self->readHandlerParameter, domain,
                    itemId, connection);

    return value;
}


MmsDevice*
MmsServer_getDevice(MmsServer self)
{
    return self->device;
}

inline void
MmsServer_setDevice(MmsServer server, MmsDevice* device)
{
    server->device = device;
}

static void /* will be called by ISO server stack */
isoConnectionIndicationHandler(IsoConnectionIndication indication,
		void* parameter, IsoConnection connection)
{
    MmsServer mmsServer = (MmsServer) parameter;

    if (indication == ISO_CONNECTION_OPENED) {
        MmsServerConnection* mmsCon = MmsServerConnection_init(0, mmsServer,
                connection);
        Map_addEntry(mmsServer->openConnections, connection, mmsCon);

        if (mmsServer->connectionHandler != NULL)
            mmsServer->connectionHandler(mmsServer->connectionHandlerParameter,
                    mmsCon, MMS_SERVER_NEW_CONNECTION);
    } else if (indication == ISO_CONNECTION_CLOSED) {
        MmsServerConnection* mmsCon = (MmsServerConnection*) Map_removeEntry(
                mmsServer->openConnections, connection, false);

        if (mmsServer->connectionHandler != NULL)
            mmsServer->connectionHandler(mmsServer->connectionHandlerParameter,
                    mmsCon, MMS_SERVER_CONNECTION_CLOSED);

        if (mmsCon != NULL)
            MmsServerConnection_destroy(mmsCon);
    }
}

void
MmsServer_startListening(MmsServer server, int tcpPort)
{
    IsoServer_setConnectionHandler(server->isoServer, isoConnectionIndicationHandler, (void*) server);
    IsoServer_setTcpPort(server->isoServer, tcpPort);
    IsoServer_startListening(server->isoServer);
}

void
MmsServer_stopListening(MmsServer server)
{
    IsoServer_stopListening(server->isoServer);
}
