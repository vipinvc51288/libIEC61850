/*
 *  server_example3.c
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

#include "iec61850_server.h"
#include "iso_server.h"
#include "acse.h"
#include "thread.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

#include "static_model.h"

/* import IEC 61850 device model created from SCL-File */
extern IedModel iedModel;

static int running = 0;
static IedServer iedServer = NULL;

void sigint_handler(int signalId)
{
	running = 0;
}

bool
checkHandler(void* parameter, MmsValue* ctlVal, bool test, bool interlockCheck)
{
    printf("check handler called!\n");

    if (interlockCheck)
        printf("  with interlock check bit set!\n");

    if (parameter == IEDMODEL_GenericIO_GGIO1_SPCSO1)
        return true;

    if (parameter == IEDMODEL_GenericIO_GGIO1_SPCSO2)
        return true;

    if (parameter == IEDMODEL_GenericIO_GGIO1_SPCSO3)
        return true;

    if (parameter == IEDMODEL_GenericIO_GGIO1_SPCSO4)
        return true;
}

void
controlHandler(void* parameter, MmsValue* value, bool test)
{
    printf("received control command %i %i: ", value->type, value->value.boolean);

    if (value->value.boolean)
        printf("on\n");
    else
        printf("off\n");

    if (parameter == IEDMODEL_GenericIO_GGIO1_SPCSO1)
        IedServer_updateAttributeValue(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO1_stVal, value);

    if (parameter == IEDMODEL_GenericIO_GGIO1_SPCSO2)
        IedServer_updateAttributeValue(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO2_stVal, value);

    if (parameter == IEDMODEL_GenericIO_GGIO1_SPCSO3)
        IedServer_updateAttributeValue(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO3_stVal, value);

    if (parameter == IEDMODEL_GenericIO_GGIO1_SPCSO4)
        IedServer_updateAttributeValue(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO4_stVal, value);
}

int main(int argc, char** argv) {

	iedServer = IedServer_create(&iedModel);

	IedServer_setControlHandler(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO1, (ControlHandler) controlHandler,
	        IEDMODEL_GenericIO_GGIO1_SPCSO1);

	IedServer_setControlHandler(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO2, (ControlHandler) controlHandler,
	            IEDMODEL_GenericIO_GGIO1_SPCSO2);

	/* this is optional - performs operative checks */
	IedServer_setPerformCheckHandler(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO2, checkHandler,
	        IEDMODEL_GenericIO_GGIO1_SPCSO2);

	IedServer_setControlHandler(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO3, (ControlHandler) controlHandler,
	            IEDMODEL_GenericIO_GGIO1_SPCSO3);

	IedServer_setControlHandler(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO4, (ControlHandler) controlHandler,
	            IEDMODEL_GenericIO_GGIO1_SPCSO4);

	/* this is optional - performs operative checks */
    IedServer_setPerformCheckHandler(iedServer, IEDMODEL_GenericIO_GGIO1_SPCSO4, checkHandler,
            IEDMODEL_GenericIO_GGIO1_SPCSO4);

	/* MMS server will be instructed to start listening to client connections. */
	IedServer_start(iedServer, 102);

	if (!IedServer_isRunning(iedServer)) {
		printf("Starting server failed! Exit.\n");
		IedServer_destroy(iedServer);
		exit(-1);
	}

	running = 1;

	signal(SIGINT, sigint_handler);

	while (running) {
		Thread_sleep(1);
	}

	/* stop MMS server - close TCP server socket and all client sockets */
	IedServer_stop(iedServer);

	/* Cleanup - free all resources */
	IedServer_destroy(iedServer);
} /* main() */
