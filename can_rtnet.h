//-----------------------------------------------------------------------------------------
//Se importan de rtcan.h todos los typedef necearios para la compilacion
//-----------------------------------------------------------------------------------------

// Formato de trama CANopen empaquetada en Ethernet:
// Campo de 16bits para COB-ID de los cuales sólo los 11LSb son válidos (despl.14-15 dentro de trama Ethernet)
// Campo de 24bits cuyos 2MSb son los bits SRR (substitute-remote-req.) e IDE y sus 18LSb son COB-ID-b. (despl.16-18)
// Luego un campo 8 bits cuyos 3MSb son RTR, r1 y r0 y sus 4LSb son DLC (data-length). (despl.19)
// Luego de 0 a 8 bytes de datos. (despl.20-...)

// TODO corregir estructura de trama para compatibilizar con rtcan.h
// TODO (también hay que corregir firmware XMOS)
typedef struct can_frame {
	// 16 bits para cob-id, sólo los 11LSb válidos
	uint16_t can_id;
	// 8bits para SRR,IDE y 2 bits superiores de cob-id-b
	uint8_t	can_id_ext_h
	// 16bits para 16LSb de cob-id-b
	uint16_t can_id_ext_l
	// 8bits para RTR,r1,r0 y Data-Length
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

// identificación de protocolo CANopen sobre RTnet
#define PROTO_CANopenRTnet 0x9023

