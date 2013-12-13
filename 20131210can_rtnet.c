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
//#include <netpacket/packet.h> ////Esto estaba comentado
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "config.h"

#ifdef RTCAN_SOCKET
	#include </usr/rtnet/include/rtnet.h>
   #include <native/task.h>
	#include <linux/kernel.h>
	//#include <linux/net.h>
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

/////////TODO	
	#include <signal.h>
	#include <pthread.h>
///////////////////
	#ifndef PF_CAN
	#define PF_CAN 29
	#endif
	#ifndef AF_CAN
	#define AF_CAN PF_CAN
	#endif
	#define CAN_IFNAME     "rteth0\0"
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

// Se agregan los include de rtcan.h

#ifdef __KERNEL__

	#include <linux/net.h>
	#include <linux/socket.h>
	#include <linux/if.h>

#else /* !__KERNEL__ */

	#include <net/if.h>

#endif /* !__KERNEL__ */

#include <rtdm/rtdm.h>


//-----------------------------------------------------------------------------------------
//Se importan de rtcan.h todos los typedef necearios para la compilacion
//-----------------------------------------------------------------------------------------

/** Type of CAN id (see @ref CAN_xxx_MASK and @ref CAN_xxx_FLAG) */
typedef uint32_t can_id_t;
typedef uint32_t canid_t;


/**
 * Raw CAN frame
 *
 * Central structure for receiving and sending CAN frames.
 */
typedef struct can_frame {
	/** CAN ID of the frame
	 *
	 *  See @ref CAN_xxx_FLAG "CAN ID flags" for special bits.
	 */
	can_id_t can_id;

	/** Size of the payload in bytes */
	uint8_t can_dlc;

	/** Payload data bytes */
	uint8_t data[8] __attribute__ ((aligned(8)));
} can_frame_t;

/** Type of CAN error mask */
typedef can_id_t can_err_mask_t;

/*!
 * @anchor CAN_xxx_MASK @name CAN ID masks
 * Bit masks for masking CAN IDs
 * @{ */

/** Bit mask for extended CAN IDs */
#define CAN_EFF_MASK  0x1FFFFFFF

/** Bit mask for standard CAN IDs */
#define CAN_SFF_MASK  0x000007FF

/** @} */

/*!
 * @anchor CAN_xxx_FLAG @name CAN ID flags
 * Flags within a CAN ID indicating special CAN frame attributes
 * @{ */
/** Extended frame */
#define CAN_EFF_FLAG  0x80000000
/** Remote transmission frame */
#define CAN_RTR_FLAG  0x40000000
/** Error frame (see @ref Errors), not valid in struct can_filter */
#define CAN_ERR_FLAG  0x20000000
/** Invert CAN filter definition, only valid in struct can_filter */
#define CAN_INV_FILTER CAN_ERR_FLAG

/** @} */

/*!
 * @anchor CAN_PROTO @name Particular CAN protocols
 * Possible protocols for the PF_CAN protocol family
 *
 * Currently only the RAW protocol is supported.
 * @{ */
/** Raw protocol of @c PF_CAN, applicable to socket type @c SOCK_RAW */
#define CAN_RAW  1
/** @} */
//-----------------------------------------------------------------------------------------
//Fin de la importancion de rtcan.h 
//-----------------------------------------------------------------------------------------

// Variables globales

#define PROTO_CANOoRTN 0x1234
static unsigned char buffer[256];
/*
{0x34, 0x46, 0x08, 0x50, 0xD1, 0x79,
	0x34, 0x46, 0x08, 0x50, 0xD1, 0x79,
	0x90, 0x23,
	0x00, 0x00, 0x00, 0x00};
*/
int sock;
//char *mac="38:46:08:50:d1:79";
char *mac="f8:1a:67:03:eb:84";
//-----------------------------------------------------------------------------//

/*********functions which permit to communicate with the board****************/
UNS8
canReceive_driver (CAN_HANDLE fd0, Message * m)
{
 int res;
  struct can_frame frame;

	printf("\nRecibiendo... \n");
	printf("fd0: %d\n", (int)fd0);

  res = CAN_RECV (sock, &frame, sizeof (frame), 0);
  if (res < 0)
    {
      fprintf (stderr, "Recv failed: %s\n", strerror (CAN_ERRNO (res)));
      return 1;
    }

  m->cob_id = frame.can_id & CAN_EFF_MASK;
  m->len = frame.can_dlc;
  if (frame.can_id & CAN_RTR_FLAG)
    m->rtr = 1;
  else
    m->rtr = 0;
  memcpy (m->data, frame.data, 8);

#if defined DEBUG_MSG_CONSOLE_ON
  MSG("in : ");
  print_message(m);
#endif

	printf("Recibido!!\n");
	printf("fd0: %d\n", (int)fd0);

  return 0;
}


/***************************************************************************/
UNS8
canSend_driver (CAN_HANDLE fd0, Message const *m)
{
	int i;
  int res;
  struct can_frame frame;
  frame.can_id = m->cob_id;
  if (frame.can_id >= 0x800)
    frame.can_id |=  CAN_EFF_FLAG;
  frame.can_dlc = m->len;
  if (m->rtr)
    frame.can_id |= CAN_RTR_FLAG;
  else
    memcpy (frame.data, m->data, 8);


#if defined DEBUG_MSG_CONSOLE_ON
  MSG("out : ");
 print_message(m);
#endif

  
	//res = CAN_SEND((int)fd0, buffer, 256, 0);
	res = CAN_SEND(sock, buffer, 256, 0);

	if (res < 0)
	 {
	   fprintf (stderr, "Send failed: %s\n", strerror (CAN_ERRNO (res)));
	 }
	printf("Trasmitió: %i bytes\n", res);
	printf("fd0: %d\n", (int)fd0);

/*
  res = CAN_SEND ((int)fd0, &frame, sizeof (frame), 0);

  if (res < 0)
    {
      fprintf (stderr, "Send failed, PORQUE?: %s\n", strerror (CAN_ERRNO (res)));
      return 1;
    }
*/
	//printf("Trasmitió: %i bytes\n", res);
	//printf("fd0: %d\n", (int)fd0);
  return 0;
}

/***************************************************************************/

int
TranslateBaudRate (char* optarg)
{
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

	printf("Baudrate establecido...\n");

	return 0x0000;
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

	const char * cc = board->busname;
	printf("Board: %d\n", atoi(cc));

	if ((sock = CAN_SOCKET(PF_PACKET, SOCK_RAW, htons(PROTO_CANOoRTN))) < 0) {
		perror("socket cannot be created");
		return (CAN_HANDLE) sock;
	}
//TODO	
printf("Socket: %d\n", sock);

	strncpy(ifr.ifr_name, CAN_IFNAME, sizeof(CAN_IFNAME));
	

	if (CAN_IOCTL(sock, SIOCGIFINDEX, &ifr) < 0) {
		perror("cannot get interface index");
		close(sock);
		return (CAN_HANDLE) sock;
	}
   

	addr.sll_family   = AF_PACKET;
	addr.sll_protocol = htons(PROTO_CANOoRTN);
	addr.sll_ifindex  = ifr.ifr_ifindex;

	if (CAN_BIND(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("cannot bind to local ip/port");
		close(sock);
		return (CAN_HANDLE) sock;
	}

	memset(eth->ether_dhost, 0xFF, ETH_ALEN);

	eth->ether_shost[0] = 0xf8;
	eth->ether_shost[1] = 0x1a;
	eth->ether_shost[2] = 0x67;
	eth->ether_shost[3] = 0x03;
	eth->ether_shost[4] = 0xe4;
	eth->ether_shost[5] = 0x8f;
	eth->ether_type = htons(PROTO_CANOoRTN);
   
 

/*/TODO
    int i=0;
    while (i<100) {
		  printf("Hasta aca joya\n");
        len = CAN_SEND(sock, buffer, sizeof(buffer),0);
        if (len < 0)
            break;

        printf("Sent frame of %zd bytes\n", len);

        nanosleep(&delay, NULL);
     i++;
    }
*/
   printf("La interfaz usada por el raw socket es: %s\n", ifr.ifr_name);
	return (CAN_HANDLE) (sock+1);//antes era null
}

/***************************************************************************/
int canClose_driver ( CAN_HANDLE fd0)
{
 
	// This call also leaves primary mode, required for socket cleanup.*/ 
	printf("shutting down\n");

	//Note: The following loop is no longer required since Xenomai 2.4. 
	while ((close(sock) < 0) && (errno == EAGAIN)) {
		printf("socket busy - waiting...\n");
		sleep(1);
	}

	return 0;
}
