/*
 *  iso_presentation.c
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

#include "libiec61850_platform_includes.h"

#include "iso_presentation.h"
#include "stack_config.h"
#include "ber_encoder.h"
#include "ber_decode.h"

static uint8_t callingPresentationSelector[] = {0x00, 0x00, 0x00, 0x01};
static uint8_t calledPresentationSelector[] = {0x00, 0x00, 0x00, 0x01};

static uint8_t asn_id_as_acse[] = {0x52, 0x01, 0x00, 0x01};

static uint8_t asn_id_mms[] = {0x28, 0xca, 0x22, 0x02, 0x01};

static uint8_t ber_id[] = {0x51, 0x01};


static int
encodeAcceptBer(uint8_t* buffer, int bufPos)
{
	bufPos = BerEncoder_encodeTL(0x30, 7, buffer, bufPos);
	bufPos = BerEncoder_encodeTL(0x80, 1, buffer, bufPos);
	buffer[bufPos++] = 0;
	bufPos = BerEncoder_encodeTL(0x81, 2, buffer, bufPos);
	buffer[bufPos++] = 0x51;
	buffer[bufPos++] = 0x01;

	return bufPos;
}

static int
encodeUserData(uint8_t* buffer, int bufPos,
        ByteBuffer* payload, bool encode, uint8_t contextId)
{
	int payloadLength = payload->size;

	int encodedDataSetLength = 3; /* presentation-selector */

	/* presentation-data */
	encodedDataSetLength += payloadLength + 1;
	encodedDataSetLength += BerEncoder_determineLengthSize(payloadLength);

	int fullyEncodedDataLength = encodedDataSetLength;

	fullyEncodedDataLength += BerEncoder_determineLengthSize(encodedDataSetLength) + 1;


	if (encode) {
		/* fully-encoded-data */
		bufPos = BerEncoder_encodeTL(0x61, fullyEncodedDataLength, buffer, bufPos);
		bufPos = BerEncoder_encodeTL(0x30, encodedDataSetLength, buffer, bufPos);

		/* presentation-selector acse */
		bufPos = BerEncoder_encodeTL(0x02, 1, buffer, bufPos);
		buffer[bufPos++] = contextId;

		/* presentation-data (= acse payload) */
		bufPos = BerEncoder_encodeTL(0xa0, payloadLength, buffer, bufPos);
		memcpy(buffer + bufPos, payload->buffer, payloadLength);
		bufPos += payloadLength;

		return bufPos;
	}
	else {
		int encodedUserDataLength = fullyEncodedDataLength + 1;
		encodedUserDataLength += BerEncoder_determineLengthSize(fullyEncodedDataLength);

		return encodedUserDataLength;
	}
}

static void
createAcceptPdu(IsoPresentation* self, ByteBuffer* writeBuffer, ByteBuffer* payload)
{
	int contentLength = 0;

	/* mode-selector */
	contentLength += 5;

	int normalModeLength = 0;

	normalModeLength += 6; /* responding-presentation-selector */

	normalModeLength += 20; /* context-definition-result-list */

	normalModeLength += encodeUserData(NULL, 0, payload, false, self->acseContextId);

	contentLength += normalModeLength;

	contentLength += BerEncoder_determineLengthSize(normalModeLength) + 1;

	uint8_t* buffer = writeBuffer->buffer;
	int bufPos = 0;

	bufPos = BerEncoder_encodeTL(0x31, contentLength, buffer, bufPos);

	/* mode-selector */
	bufPos = BerEncoder_encodeTL(0xa0, 3, buffer, bufPos);
	bufPos = BerEncoder_encodeTL(0x80, 1, buffer, bufPos);
	buffer[bufPos++] = 1; /* 1 = normal-mode */

	/* normal-mode-parameters */
	bufPos = BerEncoder_encodeTL(0xa2, normalModeLength, buffer, bufPos);

	/* responding-presentation-selector */
	bufPos = BerEncoder_encodeTL(0x83, 4, buffer, bufPos);
	memcpy(buffer + bufPos, calledPresentationSelector, 4);
	bufPos += 4;

	/* context-definition-result-list */
	bufPos = BerEncoder_encodeTL(0xa5, 18, buffer, bufPos);
	bufPos = encodeAcceptBer(buffer, bufPos); /* accept for acse */
	bufPos = encodeAcceptBer(buffer, bufPos); /* accept for mms */

	/* encode user data */
	bufPos = encodeUserData(buffer, bufPos, payload, true, self->acseContextId);

	writeBuffer->size = bufPos;
}

static void
createConnectPdu(IsoPresentation* self, ByteBuffer* writeBuffer, ByteBuffer* payload)
{
	int contentLength = 0;

	/* mode-selector */
	contentLength += 5;

	int normalModeLength = 0;

	/* called- and calling-presentation-selector */
	normalModeLength += 12;

	int pclLength = 35;

	normalModeLength += pclLength;

	normalModeLength += encodeUserData(NULL, 0, payload, false, self->acseContextId);

	normalModeLength += 2;

	contentLength += normalModeLength; // + 2;

	contentLength += 1 + BerEncoder_determineLengthSize(normalModeLength);

	uint8_t* buffer = writeBuffer->buffer;
	int bufPos = 0;

	bufPos = BerEncoder_encodeTL(0x31, contentLength, buffer, bufPos);

	/* mode-selector */
	bufPos = BerEncoder_encodeTL(0xa0, 3, buffer, bufPos);
	bufPos = BerEncoder_encodeTL(0x80, 1, buffer, bufPos);
	buffer[bufPos++] = 1; /* 1 = normal-mode */

	/* normal-mode-parameters */
	bufPos = BerEncoder_encodeTL(0xa2, normalModeLength, buffer, bufPos);

	/* calling-presentation-selector */
	bufPos = BerEncoder_encodeTL(0x81, 4, buffer, bufPos);
	memcpy(buffer + bufPos, callingPresentationSelector, 4);
	bufPos += 4;

	/* called-presentation-selector */
	bufPos = BerEncoder_encodeTL(0x82, 4, buffer, bufPos);
	memcpy(buffer + bufPos, calledPresentationSelector, 4);
	bufPos += 4;

	/* presentation-context-id list */
	bufPos = BerEncoder_encodeTL(0xa4, 35, buffer, bufPos);

	/* acse context list item */
	bufPos = BerEncoder_encodeTL(0x30, 15, buffer, bufPos);

	bufPos = BerEncoder_encodeTL(0x02, 1, buffer, bufPos);
	buffer[bufPos++] = 1;

	bufPos = BerEncoder_encodeTL(0x06, 4, buffer, bufPos);
	memcpy(buffer + bufPos, asn_id_as_acse, 4);
	bufPos += 4;

	bufPos = BerEncoder_encodeTL(0x30, 4, buffer, bufPos);
	bufPos = BerEncoder_encodeTL(0x06, 2, buffer, bufPos);
	memcpy(buffer + bufPos, ber_id, 2);
	bufPos += 2;

	/* mms context list item */
	bufPos = BerEncoder_encodeTL(0x30, 16, buffer, bufPos);

	bufPos = BerEncoder_encodeTL(0x02, 1, buffer, bufPos);
	buffer[bufPos++] = 3;

	bufPos = BerEncoder_encodeTL(0x06, 5, buffer, bufPos);
	memcpy(buffer + bufPos, asn_id_mms, 5);
	bufPos += 5;

	bufPos = BerEncoder_encodeTL(0x30, 4, buffer, bufPos);
	bufPos = BerEncoder_encodeTL(0x06, 2, buffer, bufPos);
	memcpy(buffer + bufPos, ber_id, 2);
	bufPos += 2;

	/* encode user data */
	bufPos = encodeUserData(buffer, bufPos, payload, true, self->acseContextId);

	writeBuffer->size = bufPos;
}


static int
parseFullyEncodedData(IsoPresentation* self, uint8_t* buffer, int len, int bufPos)
{
    int presentationSelector = -1;
    bool userDataPresent = false;

    int endPos = bufPos + len;

    if (buffer[bufPos++] != 0x30) {
        if (DEBUG) printf("user-data parse error\n");
        return -1;
    }

    bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, endPos);

    endPos = bufPos + len;

    if (bufPos < 0) {
        if (DEBUG) printf("wrong parameter length\n");
        return -1;
    }

    while (bufPos < endPos) {
        uint8_t tag = buffer[bufPos++];
        int length;

        bufPos = BerDecoder_decodeLength(buffer, &length, bufPos, endPos);

        if (bufPos < 0) {
            if (DEBUG) printf("PRES: wrong parameter length\n");
            return -1;
        }

        switch (tag) {
        case 0x02: /* presentation-context-identifier */
            if (DEBUG)
                printf("presentation-context-identifier\n");
            {
                presentationSelector = BerDecoder_decodeUint32(buffer, length, bufPos);
                self->nextContextId = presentationSelector;
                bufPos += length;
            }
            break;

        case 0xa0:
            if (DEBUG)
               printf("fully-encoded-data\n");

            userDataPresent = true;

            self->nextPayload.buffer = buffer + bufPos;
            self->nextPayload.size = length;

            bufPos += length;
            break;
        default:
            if (DEBUG)
                printf("fed: unknown tag %02x\n", tag);

            bufPos += length;
            break;
        }
    }

    if (!userDataPresent) {
        if (DEBUG) printf("user-data not present\n");
        return -1;
    }

    return bufPos;
}

static int
parsePCDLEntry(IsoPresentation* self, uint8_t* buffer, int len, int bufPos)
{
    int endPos = bufPos + len;

    int contextId = -1;
    bool isAcse = false;
    bool isMms = false;

    while (bufPos < endPos) {
        uint8_t tag = buffer[bufPos++];
        int len;

        bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, endPos);

        switch (tag) {
        case 0x02: /* presentation-context-identifier */
            contextId = BerDecoder_decodeUint32(buffer, len, bufPos);
            bufPos += len;
            break;
        case 0x06: /* abstract-syntax-name */
            if (DEBUG)
                printf("PRES: abstract-syntax-name with len %i\n", len);

            if (len == 5) {
                if (memcmp(buffer + bufPos, asn_id_mms, 5) == 0)
                    isMms = true;
            }
            else if (len == 4) {
                if (memcmp(buffer + bufPos, asn_id_as_acse, 4) == 0)
                    isAcse = true;
            }

            bufPos += len;

            break;
        case 0x30: /* transfer-syntax-name */
            if (DEBUG)
                printf("ignore transfer-syntax-name\n");

            bufPos += len;
            break;
        default:
            if (DEBUG) printf("unknown tag in presentation-context-definition-list-entry\n");
            bufPos += len;
            break;
        }
    }

    if (contextId < 0) {
        if (DEBUG)
            printf("PRES: ContextId not defined!\n");
        return -1;
    }

    if ((isAcse == false) && (isMms == false)) {
        if (DEBUG)
            printf("PRES: not an ACSE or MMS context definition\n");

        return -1;
    }

    if (isMms) {
        self->mmsContextId = (uint8_t) contextId;
        if (DEBUG)
            printf("MMS context id is %i\n", contextId);
    }
    else {
        self->acseContextId  = (uint8_t) contextId;
        if (DEBUG)
            printf("ACSE context id is %i\n", contextId);
    }

    return bufPos;
}

static int
parsePresentationContextDefinitionList(IsoPresentation* self, uint8_t* buffer, int len, int bufPos)
{
    int endPos = bufPos + len;

    while (bufPos < endPos) {
        uint8_t tag = buffer[bufPos++];
        int len;

        bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, endPos);

        switch (tag) {
        case 0x30:
            if (DEBUG) printf("parse pcd entry\n");
            bufPos = parsePCDLEntry(self, buffer, len, bufPos);
            if (bufPos < 0)
                return -1;
            break;
        default:
            if (DEBUG) printf("unknown tag in presentation-context-definition-list\n");
            bufPos += len;
            break;
        }
    }

    return bufPos;
}

static int
parseNormalModeParameters(IsoPresentation* self, uint8_t* buffer, int len, int bufPos)
{
	int endPos = bufPos + len;

	while (bufPos < endPos) {
		uint8_t tag = buffer[bufPos++];
		int len;

		bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, endPos);

		if (bufPos < 0) {
			if (DEBUG) printf("PRES: wrong parameter length\n");
			return -1;
		}

		switch (tag) {
		case 0x81: /* calling-presentation-selector */
			if (DEBUG) printf("calling-pres-sel\n");
			bufPos += len;
			break;
		case 0x82: /* calling-presentation-selector */
			if (DEBUG) printf("calling-pres-sel\n");
			bufPos += len;
			break;
		case 0xa4: /* presentation-context-definition list */
			if (DEBUG) printf("pcd list\n");
			bufPos = parsePresentationContextDefinitionList(self, buffer, len, bufPos);
			break;
		case 0x61: /* user data */
			if (DEBUG) printf("user-data\n");

			bufPos = parseFullyEncodedData(self, buffer, len, bufPos);

			if (bufPos < 0)
			    return -1;

			break;

		default:
			if (DEBUG) printf("unknown tag in normal-mode\n");
			bufPos += len;
			break;
		}
	}

	return bufPos;
}

static IsoPresentationIndication
parseConnectPdu
(
		IsoPresentation* self,
		ByteBuffer* byteBuffer
)
{
	uint8_t* buffer = byteBuffer->buffer;
	int maxBufPos = byteBuffer->size;

	int bufPos = 0;

	uint8_t cpTag = buffer[bufPos++];

	if (cpTag != 0x31) {
		if (DEBUG) printf("PRES: not a CP type\n");
		return PRESENTATION_ERROR;
	}

	int len;

	bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, maxBufPos);

	if (DEBUG) printf("PRES: CPType with len %i\n", len);

	while (bufPos < maxBufPos) {
		uint8_t tag = buffer[bufPos++];

		bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, maxBufPos);

		if (bufPos < 0) {
			if (DEBUG) printf("PRES: wrong parameter length\n");
			return PRESENTATION_ERROR;
		}

		switch (tag) {
		case 0xa0: /* mode-selection */
			{
				if (buffer[bufPos++] != 0x80) {
					if (DEBUG) printf("PRES: mode-value of wrong type!\n");
					return PRESENTATION_ERROR;
				}
				bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, maxBufPos);
				uint32_t modeSelector =  BerDecoder_decodeUint32(buffer, len, bufPos);
				if (DEBUG) printf("PRES: modesel %ui\n", modeSelector);
				bufPos += len;
			}
			break;
		case 0xa2: /* normal-mode-parameters */
			bufPos = parseNormalModeParameters(self, buffer, len, bufPos);

			if (bufPos < 0) {
				if (DEBUG) printf("PRES: error parsing normal-mode-parameters\n");
				return PRESENTATION_ERROR;
			}

			break;
		default: /* unsupported element */
			if (DEBUG) printf("PRES: tag %i not recognized\n", tag);
			bufPos += len;
			break;
		}
	}

	return PRESENTATION_OK;
}

IsoPresentationIndication
IsoPresentation_parseAcceptMessage(IsoPresentation* self, ByteBuffer* byteBuffer)
{
	bool hasModeSelector = false;
	bool hasNormalModeParameters = false;

	uint8_t* buffer = byteBuffer->buffer;
	int maxBufPos = byteBuffer->size;

	int bufPos = 0;

	uint8_t cpTag = buffer[bufPos++];

	if (cpTag != 0x31) {
		if (DEBUG) printf("PRES: not a CPA message\n");
		return PRESENTATION_ERROR;
	}

	int len;

	bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, maxBufPos);

	while (bufPos < maxBufPos) {
			uint8_t tag = buffer[bufPos++];

			bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, maxBufPos);

			if (bufPos < 0) {
				if (DEBUG) printf("PRES: wrong parameter length\n");
				return PRESENTATION_ERROR;
			}

			switch (tag) {
			case 0xa0: /* mode-selector */
				hasModeSelector = true;
				bufPos += len; /* ignore content since only normal mode is allowed */
				break;
			case 0xa2: /* normal-mode-parameters */
				bufPos = parseNormalModeParameters(self, buffer, len, bufPos);

				if (bufPos < 0) {
					if (DEBUG) printf("PRES: error parsing normal-mode-parameters\n");
					return PRESENTATION_ERROR;
				}

				break;
			default:
				if (DEBUG) printf("PRES: CPA unknown tag %i\n", tag);
				bufPos += len;
				break;
			}
	}

	return PRESENTATION_OK;
}


void
IsoPresentation_init(IsoPresentation* session)
{

}


IsoPresentationIndication
IsoPresentation_createUserData(IsoPresentation* self, ByteBuffer* writeBuffer, ByteBuffer* payload)
{
	int bufPos =  ByteBuffer_getSize(writeBuffer);
	uint8_t* buffer = ByteBuffer_getBuffer(writeBuffer);

	int payloadLength = ByteBuffer_getSize(payload);

	int userDataLengthFieldSize = BerEncoder_determineLengthSize(payloadLength);
;
	int pdvListLength = payloadLength + (userDataLengthFieldSize + 4);

	int pdvListLengthFieldSize = BerEncoder_determineLengthSize(pdvListLength);
	int presentationLength = pdvListLength + (pdvListLengthFieldSize + 1);

	bufPos = BerEncoder_encodeTL(0x61, presentationLength, buffer, bufPos);

	bufPos = BerEncoder_encodeTL(0x30, pdvListLength, buffer, bufPos);

	buffer[bufPos++] = (uint8_t) 0x02;
	buffer[bufPos++] = (uint8_t) 0x01;
	buffer[bufPos++] = (uint8_t) self->mmsContextId; /* mms context id */

	bufPos = BerEncoder_encodeTL(0xa0, payloadLength, buffer, bufPos);

	ByteBuffer_setSize(writeBuffer, bufPos);

	ByteBuffer_append(writeBuffer, ByteBuffer_getBuffer(payload), payloadLength);

	return PRESENTATION_OK;
}

IsoPresentationIndication
IsoPresentation_parseUserData(IsoPresentation* self, ByteBuffer* readBuffer)
{
	int length = readBuffer->size;
	uint8_t* buffer = readBuffer->buffer;

	int bufPos = 0;

	if (length < 9)
		return PRESENTATION_ERROR;

	if (buffer[bufPos++] != 0x61)
		return PRESENTATION_ERROR;

	int len;

	bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, length);

	if (buffer[bufPos++] != 0x30)
		return PRESENTATION_ERROR;

	bufPos = BerDecoder_decodeLength(buffer, &len, bufPos, length);

	if (buffer[bufPos++] != 0x02)
		return PRESENTATION_ERROR;

	if (buffer[bufPos++] != 0x01)
		return PRESENTATION_ERROR;

	self->nextContextId = buffer[bufPos++];

	if (buffer[bufPos++] != 0xa0)
		return PRESENTATION_ERROR;;

	int userDataLength;

	bufPos = BerDecoder_decodeLength(buffer, &userDataLength, bufPos, length);

	ByteBuffer_wrap(&(self->nextPayload), buffer + bufPos, userDataLength, userDataLength);

	return PRESENTATION_OK;
}

IsoPresentationIndication
IsoPresentation_parseConnect(IsoPresentation* self, ByteBuffer* buffer)
{
	return parseConnectPdu(self, buffer);
}

void
IsoPresentation_createConnectPdu(IsoPresentation* self, ByteBuffer* buffer, ByteBuffer* payload)
{
    self->acseContextId = 1;
    self->mmsContextId = 3;
	createConnectPdu(self, buffer, payload);
}

void
IsoPresentation_createCpaMessage(IsoPresentation* self, ByteBuffer* buffer, ByteBuffer* payload)
{
	createAcceptPdu(self, buffer, payload);
}
