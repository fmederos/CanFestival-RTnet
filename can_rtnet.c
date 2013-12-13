/*
This file is part of CanFestival, a library implementing CanOpen Stack.

Copyright (C): Edouard TISSERANT and Francis DUPIN

See COPYING file for copyrights details.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>		/* for NULL */
#include <errno.h>

#include <sys/mman.h>
//#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "config.h"

// mensajes de depuración... comentar para desactivar
#define	DEBUG_MSG_CONSOLE_ON

#ifdef RTCAN_SOCKET
	#include "/usr/rtnet/include/rtnet.h"
	#include <native/task.h>
	#include <linux/kernel.h>
	#include <linux/net.h>
	#include <linux/if_packet.h>
	#define CAN_IFNAME     "rteth0"
	#define CAN_SOCKET     rt_dev_socket
	#define CAN_CLOSE      rt_dev_close
	#define CAN_RECV       rt_dev_recv
	#define CAN_SEND       rt_dev_send
	#define CAN_BIND       rt_dev_bind
	#define CAN_IOCTL      rt_dev_ioctl
	#define CAN_SETSOCKOPT rt_dev_setsockopt
	#define CAN_ERRNO(err) (-err)
#else

	#include <unistd.h>
	#include <sys/socket.h>
	#include <sys/ioctl.h>
	#include "net/if.h"
	#ifndef PF_CAN
	#define PF_CAN 29
	#endif
	#ifndef AF_CAN
	#define AF_CAN PF_CAN
	#endif
	#define CAN_IFNAME     "rteth0"
	#define CAN_SOCKET     socket
	#define CAN_CLOSE      close
	#define CAN_RECV       recv
	#define CAN_SEND       send
	#define CAN_BIND       bind
	#define CAN_IOCTL      ioctl
	#define CAN_ERRNO(err) errno
	#define CAN_SETSOCKOPT setsockopt
#endif

#include "can_driver.h"
#include "rtcan.h"
#include <rtdm/rtdm.h>
#include "can_rtnet.h"

static unsigned char buffer[64];
// TODO : llenar este array con la shost del interfaz
char *mac="38:46:08:50:d1:79";		// xmos
//char *mac="f8:1a:67:03:eb:84";	// carmona
//char *mac="F8:1A:67:03:E4:8F";	// cnc

/*********functions which permit to communicate with the board****************/
UNS8
canReceive_driver (CAN_HANDLE fd0, Message * m)
{
	int res;
	int sock;
	struct can_frame frame;

	// convertimos de CAN_HANDLE a socket
	// CAN_HANDLE = socket+1
	sock = (int)fd0 -1;

#ifdef DEBUG_MSG_CONSOLE_ON
	printf("\nRecibiendo... \n");
	printf("Socket: %d\n", sock);
#endif

	// esta función bloquea hasta recibir trama
  res = CAN_RECV (sock, &frame, sizeof (frame), 0);
  if (res < 0)
    {
      fprintf (stderr, "Recv failed: %s\n", strerror (CAN_ERRNO (res)));
      return 1;
    }

	// TODO: interpretar frame de acuerdo al formato CANopenRTnet (can_rtnet.h)
	// TODO: diferenciar caso extended COB-id (señalizado por segundo MSb de can_id_ext_h)
	// armamos estructura message para CANfestival con los datos recibidos
	m->cob_id = frame.can_id & CAN_EFF_MASK;
	m->len = frame.can_dlc;
	if (frame.can_id & CAN_RTR_FLAG)
		m->rtr = 1;
	else
		m->rtr = 0;
	memcpy (m->data, frame.data, 8);

#if defined DEBUG_MSG_CONSOLE_ON
	printf("Recibido!!\n");
	printf("Socket: %d\n", sock);

	MSG("in : ");
	print_message(m);
#endif

  return 0;
}


/***************************************************************************/
UNS8
canSend_driver (CAN_HANDLE fd0, Message const *m)
{
	int i;
	int res;
	int sock;
	struct can_frame frame;

	// armamos trama en formato CANopenRTnet a partir de tipo message de CANfestival
	frame.can_id = m->cob_id;
	if (frame.can_id >= 0x800)
		frame.can_id |=  CAN_EFF_FLAG;
	frame.can_dlc = m->len;
	if (m->rtr)
		frame.can_id |= CAN_RTR_FLAG;
	else
		memcpy (frame.data, m->data, 8);

	// convertimos CAN_HANDLE a socket
	sock = (int)fd0 -1;

#if defined DEBUG_MSG_CONSOLE_ON
	MSG("out : ");
	print_message(m);
#endif
  
	res = CAN_SEND(sock, buffer, sizeof(buffer), 0);
	//res = CAN_SEND (sock, &frame, sizeof (frame), 0);
	if (res < 0)
	{
		fprintf (stderr, "Send failed: %s\n", strerror (CAN_ERRNO (res)));
		return 1;
	}

#if defined DEBUG_MSG_CONSOLE_ON
	printf("Trasmitió: %i bytes\n", res);
	printf("Socket: %d\n", sock);
#endif

  return 0;
}

/***************************************************************************/

int
TranslateBaudRate (char* optarg)
{
	// confirmamos baudrate establecido sin hacer nada...
	if(!strcmp( optarg, "1M")) return (int)1000;
	if(!strcmp( optarg, "500K")) return (int)500;
	if(!strcmp( optarg, "250K")) return (int)250;
	if(!strcmp( optarg, "125K")) return (int)125;
	if(!strcmp( optarg, "100K")) return (int)100;
	if(!strcmp( optarg, "50K")) return (int)50;
	if(!strcmp( optarg, "20K")) return (int)20;
	if(!strcmp( optarg, "10K")) return (int)10;
	if(!strcmp( optarg, "5K")) return (int)5;
	if(!strcmp( optarg, "none")) return 0;

#if defined DEBUG_MSG_CONSOLE_ON
	printf("Baudrate establecido...\n");
#endif

	return 0;
}


UNS8 canChangeBaudRate_driver( CAN_HANDLE fd, char* baud)
{
	printf("canChangeBaudRate not yet supported by this driver\n");
	return 0;
}

/***************************************************************************/
CAN_HANDLE 
canOpen_driver (s_BOARD *board)
{
	ssize_t len;
	struct sockaddr_ll addr;
	struct ifreq ifr;
	struct timespec delay = { 1, 0 };
	struct ether_header *eth = (struct ether_header *)buffer;
	int res;
	int sock;

	if ((sock = CAN_SOCKET(PF_PACKET, SOCK_RAW, htons(PROTO_CANopenRTnet))) < 0) {
		perror("socket cannot be created");
		return (CAN_HANDLE) sock;
	}

	// armamos ifr para solicitar indice del interfaz
	strncpy(ifr.ifr_name, CAN_IFNAME, sizeof(CAN_IFNAME));

	// TODO
	// TODO: obtener source MAC del IOctl

	if (CAN_IOCTL(sock, SIOCGIFINDEX, &ifr) < 0) {
		perror("cannot get interface index");
		close(sock);
		return (CAN_HANDLE) sock;
	}

#if defined DEBUG_MSG_CONSOLE_ON
	printf("Board: %s\n", board->busname);
   printf("Socket: %d\n", sock);
	printf("La interfaz usada por el raw socket es: %s\n", ifr.ifr_name);
   printf("IFindex: %d\n", ifr.ifr_ifindex);
#endif

	// armamos socket link-layer para asignar puerto y protocolo al interfaz
	addr.sll_family   = PF_PACKET;//AF_PACKET;
	addr.sll_protocol = htons(PROTO_CANopenRTnet);
	addr.sll_ifindex  = ifr.ifr_ifindex;

	if (CAN_BIND(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("cannot bind to local ip/port");
		close(sock);
		return (CAN_HANDLE) sock;
	}

	// llenamos campos fijos de la trama (source host address y protocolo)
	memset(eth->ether_dhost, 0xFF, ETH_ALEN);

//char *mac="f8:1a:67:03:eb:84";	// carmona
	eth->ether_shost[0] = 0xf8;
	eth->ether_shost[1] = 0x1a;
	eth->ether_shost[2] = 0x67;
	eth->ether_shost[3] = 0x03;
	eth->ether_shost[4] = 0xeb;
	eth->ether_shost[5] = 0x84;
/*
	eth->ether_shost[0] = 0xf8;
	eth->ether_shost[1] = 0x1a;
	eth->ether_shost[2] = 0x67;
	eth->ether_shost[3] = 0x03;
	eth->ether_shost[4] = 0xe4;
	eth->ether_shost[5] = 0x8f;
*/
	eth->ether_type = htons(PROTO_CANopenRTnet);
   
	return (CAN_HANDLE)(sock+1);
}

/***************************************************************************/
int canClose_driver ( CAN_HANDLE fd0)
{
 	int sock = (int)fd0-1;
	// This call also leaves primary mode, required for socket cleanup.*/ 
	printf("shutting down\n");

	//Note: The following loop is no longer required since Xenomai 2.4. 
	while ((CAN_CLOSE(sock) < 0) && (errno == EAGAIN)) {
		printf("socket busy - waiting...\n");
		sleep(1);
	}

	return 0;
}
