/*
This file is part of CanFestival, a library implementing CanOpen Stack.

Copyright (C): Fernando Mederos and Daniel Garcia Carreras

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
#include <stddef.h>
#include <errno.h>
#include <config.h>
#include <sys/mman.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>

// mensajes de depuración... comentar para desactivar
#define	DEBUG_MSG_CONSOLE_ON

#ifdef RTCAN_SOCKET
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

#include <can_driver.h>
#include <rtdm/rtcan.h>
#include <rtdm/rtdm.h>

// MAC destino siempre Broadcast, los nodos se identificaran por el node id de canopen
// Ethertype de tramas CANopen sobre Ethernet
#define PROTO_CANopenRTnet 0x9023
// prototipo de cabecera ethernet
static unsigned char buffer_header[14];


#define SWAP(a, b, t)   ((t) = (a), (a) = (b), (b) = (t))

void byterev(unsigned char *val, int size)
{
  int t;
         
  switch (size) {
  case 2:
    SWAP(val[0], val[1], t);
    break;
  case 4:
    SWAP(val[0], val[3], t);
    SWAP(val[1], val[2], t);
    break;
  }
}


/*********functions which permit to communicate with the board****************/
UNS8
canReceive_driver (CAN_HANDLE fd0, Message * m)
{
	int res;
	int sock;
	int i;
	unsigned char buffer[64];
	//can_frame es payload del paquete ethernet, viene luego del header ethernet
	struct can_frame *frame = (struct can_frame *)(ETH_HLEN+(void *)buffer);

    // convertimos de CAN_HANDLE a socket
    // CAN_HANDLE = socket+1
    sock = (int)fd0 -1;

#ifdef DEBUG_MSG_CONSOLE_ON
    printf("\nRecibiendo... \n");
    printf("Socket: %d\n", sock);
#endif
    // esta función bloquea hasta recibir una trama
	// flags: MSG_PEEK, MSG_DONTWAIT
    res = CAN_RECV (sock, buffer, sizeof(buffer), 0);
    if (res < 0)
    {
      fprintf (stderr, "Recv failed: %s\n", strerror (CAN_ERRNO (res)));
      return 1;
    }

	// la trama ethernet usa almacenamiento little-endian, hay que invertir
	byterev((unsigned char *)&((*frame).can_id) , 4);

    m->cob_id = (*frame).can_id & CAN_EFF_MASK;
    m->len = (*frame).can_dlc;
    if ((*frame).can_id & CAN_RTR_FLAG)
      m->rtr = 1;
    else
      m->rtr = 0;
    memcpy (m->data, (*frame).data, m->len);

#if defined DEBUG_MSG_CONSOLE_ON
	printf("Recibidos %d bytes!\n",res);
	printf("Socket: %d\n", sock);
	printf("COB-id:%4.4x\n" , m->cob_id);
	printf("Len:%2.2x\n" , m->len);
	printf("Data");
	for(i = 0 ; i < m->len ; i++)
		printf(":%2.2x", m->data[i]);
	printf("\n");

//    MSG("in : ");
//    print_message(m);
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
  unsigned char buffer[64];

  //frame va a ser payload del paquete, hay que ubicarlo luego del header ethernet
  struct can_frame *frame = (struct can_frame *)(ETH_HLEN+(void *)buffer);

  // inicializamos buffer en 0
  memset(buffer,0x00,64);

  // copiamos header prototipo al principio de la trama
  for(i = 0 ; i < ETH_HLEN ; i++)
    buffer[i]=buffer_header[i];

  // armamos trama en formato CANopenRTnet a partir de tipo message de CANfestival
  // pasamos el COB-id
  (*frame).can_id = m->cob_id;
  // ponemos flag EFF
  if ( m->cob_id >= 0x800)
    (*frame).can_id |= CAN_EFF_FLAG;
  // ponemos dlc
  (*frame).can_dlc = m->len;
  // ponemos flag RTR o datos
  if (m->rtr)
    // Si es solicitud de respuesta no lleva datos
    (*frame).can_id |= CAN_RTR_FLAG;
  else
    // Si NO es solicitud de respuesta entonces SI lleva datos...
    memcpy ((*frame).data, m->data, m->len);

// Tener en cuenta alineación de campo data[] de can_frame.
// rtcan.h define el campo con alineación a 8 bytes, esto agrega 3 bytes sin
// utilizar en la trama, eliminando el atributo de alineación se evita esto:
//	uint8_t data[8] __attribute__ ((aligned(8)));
//	uint8_t data[8];


  // el orden de los bytes en la trama debe ser little-endian, esta arquitectura escribe en big-endian...
  // hay que invertir orden del campo cob-id, el dlc no porque es 1 solo byte
  byterev((unsigned char *)&((*frame).can_id),4);

  // convertimos CAN_HANDLE a socket
  sock = (int)fd0 -1;

#if defined DEBUG_MSG_CONSOLE_ON
//TODO
   printf("COBID: %x\n", m->cob_id);
 	//printf("Frame CANID: %x\n", (*frame).can_id);

//TODO
	//printf("M DATA: %x\n", m->data);
 	//printf("Frame DATA: %x\n", (*frame).data);

	//MSG("out : ");
	//print_message(m);

	printf("Buffer");
	for(i = 0 ; i < 64 ; i++)
		printf(":%2.2x", buffer[i]);
	printf("\n");

	printf("Socket: %d\n", sock);
#endif

  res = CAN_SEND(sock, buffer, sizeof(buffer), 0);
  //res = CAN_SEND (sock, &frame, sizeof (frame), 0);
  if (res < 0){
    fprintf (stderr, "Send failed: %s\n", strerror (CAN_ERRNO (res)));
    return 1;
  }

#if defined DEBUG_MSG_CONSOLE_ON
  printf("Trasmitió: %i bytes\n", res);
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
  int res;
  int sock;
  int i;
  // se crea eth como puntero a cabecera ethernet y se la apunta a comienzo de cabecera prototipo buffer_header
  struct ether_header *eth = (struct ether_header *)buffer_header;

  // abrimos socket
  if ((sock = CAN_SOCKET(PF_PACKET, SOCK_RAW, htons(PROTO_CANopenRTnet))) < 0) {
    perror("socket cannot be created");
    return (CAN_HANDLE) sock;
  }
    
  // armamos ifr para solicitar indice del interfaz
  strncpy(ifr.ifr_name, CAN_IFNAME, sizeof(CAN_IFNAME));
	
  // Se obtiene MAC del intefaz (de esta computadora)
  if (CAN_IOCTL(sock, SIOCGIFHWADDR, &ifr) < 0) {
    perror("cannot get the interface Hardware Address");
    CAN_CLOSE(sock);
    return (CAN_HANDLE) sock;
  }
 
  //se copia MAC obtenida al prototipo de header ethernet (que apunta a buffer_header)
  for (i = 0 ; i < ETH_ALEN ; i++)
    eth->ether_shost[i]=(unsigned char)ifr.ifr_hwaddr.sa_data[i];

  if (CAN_IOCTL(sock, SIOCGIFINDEX, &ifr) < 0) {
    perror("cannot get interface index");
    CAN_CLOSE(sock);
    return (CAN_HANDLE) sock;
  }

#if defined DEBUG_MSG_CONSOLE_ON
  printf("Board: %s\n", board->busname);
  printf("Socket: %d\n", sock);
  printf("The name of the interface is: %s\n", ifr.ifr_name);
  printf("IFindex: %d\n", ifr.ifr_ifindex);
  printf("MAC ADDR: %s\n", ifr.ifr_hwaddr.sa_data);
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
  // destino ethernet fijo a Broadcast, los nodos se identificaran por el node id de canopen
  memset(eth->ether_dhost, 0xFF, ETH_ALEN);
  eth->ether_type = htons(PROTO_CANopenRTnet);

  // convertimos de socket a CAN_HANDLE
  // CAN_HANDLE = socket+1
  //various systems assign socket=0 and CanFestival dose not accept an CAN_HANDLE=0.
  //This will lead to an error with the timers module
  //So we make the following "correction"
  return (CAN_HANDLE)(sock+1);
}

/***************************************************************************/
int canClose_driver (CAN_HANDLE fd0)
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
