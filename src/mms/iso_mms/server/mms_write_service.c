/*
 *  mms_write_service.c
 *
 *  Copyright 2013 Michael Zillgith
 *
 *	This file is part of libIEC61850.
 *
 *	libIEC61850 is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	libIEC61850 is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with libIEC61850.  If not, see <http://www.gnu.org/licenses/>.
 *
 *	See COPYING file for the complete license text.
 */

#include "mms_server_internal.h"
#include "mms_common_internal.h"
#include "mms_types.h"
/**********************************************************************************************
 * MMS Write Service
 *********************************************************************************************/

int
mmsServer_createMmsWriteResponse(MmsServerConnection* connection,
		int invokeId, ByteBuffer* response, int numberOfItems, MmsDataAccessError* accessResults)
{
	MmsPdu_t* mmsPdu = mmsServer_createConfirmedResponse(invokeId);

	mmsPdu->choice.confirmedResponsePdu.confirmedServiceResponse.present =
			ConfirmedServiceResponse_PR_write;

	WriteResponse_t* writeResponse =
			&(mmsPdu->choice.confirmedResponsePdu.confirmedServiceResponse.choice.write);

	writeResponse->list.count = numberOfItems;
	writeResponse->list.size = numberOfItems;
	writeResponse->list.array = (struct WriteResponse__Member**) calloc(numberOfItems,
	        sizeof(struct WriteResponse__Member*));

	int i;

	for (i = 0; i < numberOfItems; i++) {
	    writeResponse->list.array[i] =  (struct WriteResponse__Member*) calloc(1, sizeof(struct WriteResponse__Member));

	    if (accessResults[i] == DATA_ACCESS_ERROR_SUCCESS)
	        writeResponse->list.array[i]->present = WriteResponse__Member_PR_success;
	    else {
	        writeResponse->list.array[i]->present = WriteResponse__Member_PR_failure;
	        asn_long2INTEGER(&writeResponse->list.array[i]->choice.failure, (long) accessResults[i]);
	    }
	}

	asn_enc_rval_t rval;

	rval = der_encode(&asn_DEF_MmsPdu, mmsPdu,
				mmsServer_write_out, (void*) response);

	if (DEBUG) xer_fprint(stdout, &asn_DEF_MmsPdu, mmsPdu);

	asn_DEF_MmsPdu.free_struct(&asn_DEF_MmsPdu, mmsPdu, 0);

 	return 0;
}


void
MmsServerConnection_sendWriteResponse(MmsServerConnection* self, uint32_t invokeId, MmsDataAccessError indication)
{
    ByteBuffer* response = ByteBuffer_create(NULL, self->maxPduSize);

    mmsServer_createMmsWriteResponse(self, invokeId, response, 1, &indication);

    IsoConnection_sendMessage(self->isoConnection, response);

    ByteBuffer_destroy(response);
}

int /* MmsServiceError */
mmsServer_handleWriteRequest(
		MmsServerConnection* connection,
		uint8_t* buffer, int bufPos, int maxBufPos,
		uint32_t invokeId,
		ByteBuffer* response)
{
	WriteRequest_t* writeRequest = 0;

	MmsPdu_t* mmsPdu = 0;

	asn_dec_rval_t rval; /* Decoder return value  */

	rval = ber_decode(NULL, &asn_DEF_MmsPdu, (void**) &mmsPdu, buffer, MMS_MAXIMUM_PDU_SIZE);

	writeRequest = &(mmsPdu->choice.confirmedRequestPdu.confirmedServiceRequest.choice.write);

	int numberOfWriteItems = writeRequest->variableAccessSpecification.choice.listOfVariable.list.count;

	if (numberOfWriteItems < 1)
		return -1; // TODO send reject PDU

    if (writeRequest->listOfData.list.count != numberOfWriteItems)
        // TODO send reject PDU
        return -1;

	MmsDataAccessError* accessResults =
			(MmsDataAccessError*) alloca(numberOfWriteItems * sizeof(MmsDataAccessError));

	bool sendResponse = true;

	int i;

	for (i = 0; i < numberOfWriteItems; i++) {
	    ListOfVariableSeq_t* varSpec =
                writeRequest->variableAccessSpecification.choice.listOfVariable.list.array[i];

        if (varSpec->variableSpecification.present != VariableSpecification_PR_name) {
            accessResults[i] = DATA_ACCESS_ERROR_OBJECT_ACCESS_UNSUPPORTED;
            continue;
            //mmsServer_createMmsWriteResponse(connection, invokeId, response, MMS_VALUE_ACCESS_DENIED);
            //return 0;
        }

        if (varSpec->variableSpecification.choice.name.present != ObjectName_PR_domainspecific) {
            accessResults[i] = DATA_ACCESS_ERROR_OBJECT_ACCESS_UNSUPPORTED;
            continue;
            //mmsServer_createMmsWriteResponse(connection, invokeId, response, MMS_VALUE_ACCESS_DENIED);
            //return 0;
        }

        Identifier_t domainId = varSpec->variableSpecification.choice.name.choice.domainspecific.domainId;
        char* domainIdStr = createStringFromBuffer(domainId.buf, domainId.size);

        MmsDevice* device = MmsServer_getDevice(connection->server);

        MmsDomain* domain = MmsDevice_getDomain(device, domainIdStr);

        free(domainIdStr);

        if (domain == NULL) {
            accessResults[i] = DATA_ACCESS_ERROR_OBJECT_NONE_EXISTENT;
            continue;
            //mmsServer_createMmsWriteResponse(connection, invokeId, response, MMS_VALUE_ACCESS_DENIED);
            //return 0;
        }

        Identifier_t nameId = varSpec->variableSpecification.choice.name.choice.domainspecific.itemId;
        char* nameIdStr = createStringFromBuffer(nameId.buf, nameId.size);

        MmsVariableSpecification* variable = MmsDomain_getNamedVariable(domain, nameIdStr);

        if (variable == NULL) {
            free(nameIdStr);
            accessResults[i] = DATA_ACCESS_ERROR_OBJECT_NONE_EXISTENT;
            continue;
            //goto return_access_denied;
        }

        AlternateAccess_t* alternateAccess = varSpec->alternateAccess;

        if (alternateAccess != NULL) {
            if (variable->type != MMS_ARRAY) {
                free(nameIdStr);
                accessResults[i] = DATA_ACCESS_ERROR_OBJECT_ATTRIBUTE_INCONSISTENT;
                continue;
               // goto return_access_denied;
            }

            if (!mmsServer_isIndexAccess(alternateAccess)) {
                free(nameIdStr);
                accessResults[i] = DATA_ACCESS_ERROR_OBJECT_ACCESS_UNSUPPORTED;
                continue;
                //goto return_access_denied;
            }
        }

        Data_t* dataElement = writeRequest->listOfData.list.array[i];

        MmsValue* value = mmsMsg_parseDataElement(dataElement);

        if (value == NULL) {
            free(nameIdStr);
            accessResults[i] = DATA_ACCESS_ERROR_OBJECT_ATTRIBUTE_INCONSISTENT;
            continue;
        }

        if (alternateAccess != NULL) {
            MmsValue* cachedArray = MmsServer_getValueFromCache(connection->server, domain, nameIdStr);

            if (cachedArray == NULL) {
                free(nameIdStr);
                MmsValue_delete(value);
                accessResults[i] = DATA_ACCESS_ERROR_OBJECT_ATTRIBUTE_INCONSISTENT;
                continue;
                //goto return_access_denied;
            }

            int index = mmsServer_getLowIndex(alternateAccess);

            MmsValue* elementValue = MmsValue_getElement(cachedArray, index);

            if (elementValue == NULL) {
                free(nameIdStr);
                MmsValue_delete(value);
                accessResults[i] = DATA_ACCESS_ERROR_OBJECT_ATTRIBUTE_INCONSISTENT;
                continue;
                //goto return_access_denied;
            }

            if (MmsValue_update(elementValue, value) == false) {
                free(nameIdStr);
                MmsValue_delete(value);
                accessResults[i] = DATA_ACCESS_ERROR_TYPE_INCONSISTENT;
                continue;
                //goto return_access_denied;
            }

            free(nameIdStr);
            MmsValue_delete(value);
            accessResults[i] = DATA_ACCESS_ERROR_SUCCESS;
            continue;

        }

        MmsServer_lockModel(connection->server);

        MmsDataAccessError valueIndication =
                mmsServer_setValue(connection->server, domain, nameIdStr, value, connection);

        MmsServer_unlockModel(connection->server);

        if (valueIndication == DATA_ACCESS_ERROR_NO_RESPONSE)
            sendResponse = false;

        accessResults[i] = valueIndication;
            //mmsServer_createMmsWriteResponse(connection, invokeId, response, valueIndication);

        MmsValue_delete(value);

        free(nameIdStr);
	}

	if (sendResponse) {
	    mmsServer_createMmsWriteResponse(connection, invokeId, response, numberOfWriteItems, accessResults);
	}

	asn_DEF_MmsPdu.free_struct(&asn_DEF_MmsPdu, mmsPdu, 0);
	return 0;
}


