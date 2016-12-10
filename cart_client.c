////////////////////////////////////////////////////////////////////////////////
//
//  File          : cart_client.c
//  Description   : This is the client side of the CART communication protocol.
//
//   Author       : Eric Traister
//  Last Modified : 12/09/2016
//
////////////////////////////////////////////////////////////////////////////////

// Include Files
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// Project Include Files
#include "cart_driver.h"
#include "cart_network.h"
#include "cart_controller.h"
#include "cmpsc311_log.h"
#include "cmpsc311_util.h"

//  Global data
int client_socket = -1;
int                cart_network_shutdown = 0;   // Flag indicating shutdown
unsigned char     *cart_network_address = NULL; // Address of CART server
unsigned short     cart_network_port = 0;       // Port of CART serve
unsigned long      CartControllerLLevel = 0; // Controller log level (global)
unsigned long      CartDriverLLevel = 0;     // Driver log level (global)
unsigned long      CartSimulatorLLevel = 0;  // Driver log level (global)

// Enumerations
typedef enum Flag {
	YES = 0,
	NO = 1
} Flag;

// Global Variables
Flag	initialized = NO;

// Functions

uint64_t extract_cart_opcode(CartXferRegister resp, CartRegisters reg_field) {

	CartXferRegister reg_seg;
	reg_seg = resp;

	// Create ability to extract any relevant part of OPCODE sent to CART for checks
	switch (reg_field) {
	case(CART_REG_KY1) :
		reg_seg = (reg_seg >> 56);
		break;
	case(CART_REG_KY2) :
		reg_seg = ((reg_seg >> 48) & 0xFF);   // logical right shift 48 bits and logical AND with ... 1111 1111
		break;
	case(CART_REG_RT1) :
		reg_seg = ((reg_seg >> 47) & 0x01);   // logical right shift 47 bits and logical AND with ... 0001
		break;
	case(CART_REG_CT1) :
		reg_seg = ((reg_seg >> 31) & 0xFFFF); // logical right shift 31 bits and logical AND with ... 1111 1111 1111 1111
		break;
	case(CART_REG_FM1) :
		reg_seg = ((reg_seg >> 15) & 0xFFFF); // logical right shift 15 bits and logical AND with ... 1111 1111 1111 1111
		break;
	default:                                        // unused: CART_REG_MAXVAL, access to unused/reserved bits
		logMessage(LOG_ERROR_LEVEL, "extract_card_opcode error: wrong reg_field passed for response");
	}
	return (reg_seg);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : client_cart_bus_request
// Description  : This the client operation that sends a request to the CART
//                server process.   It will:
//
//                1) if INIT make a connection to the server
//                2) send any request to the server, returning results
//                3) if CLOSE, will close the connection
//
// Inputs       : reg - the request reqisters for the command
//                buf - the block to be read/written from (READ/WRITE)
// Outputs      : the response structure encoded as needed
////////////////////////////////////////////////////////////////////////////////
CartXferRegister client_cart_bus_request(CartXferRegister reg, void *buf) {

	// Local Variables
	int					socket_fd = client_socket;
	uint8_t				request;

	CartXferRegister	value = 0;
	CartXferRegister	response = 0;

	struct	sockaddr_in caddr;
	char *ip = CART_DEFAULT_IP;
	cart_network_port = CART_DEFAULT_PORT;


	// Obtain the network request
	request = extract_cart_opcode(reg, CART_REG_KY1);

	// Check if socket is open
	if (socket_fd != -1) {
		logMessage(LOG_ERROR_LEVEL, "\nSocket already open. Aborting...\n");
		return (-1);
	}

	// Handle the request
	switch (request) {
		case(CART_OP_INITMS) :
	
			while (initialized == NO) {

				logMessage(LOG_INFO_LEVEL, "\nInitialize Server Connection\n");

				caddr.sin_family = AF_INET;
				caddr.sin_port = htons(cart_network_port);
				if (inet_aton(ip, &caddr.sin_addr) == 0) {
					logMessage(LOG_ERROR_LEVEL, "\nUnable to obtain address\n");
					return(-1);
				}

				logMessage(LOG_INFO_LEVEL, "\nCreating socket...\n");

				socket_fd = socket(PF_INET, SOCK_STREAM, 0);
				if (socket_fd == -1) {
					logMessage(LOG_ERROR_LEVEL, "\nError on socket creation\n");
					return(-1);
				}

				logMessage(LOG_INFO_LEVEL, "\nConnecting to server...\n");

				if (connect(socket_fd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1) {
					logMessage(LOG_ERROR_LEVEL, "\nError on socket connect\n");
					return (-1);
				}

				initialized = YES;
				logMessage(LOG_INFO_LEVEL, "\nInitialization Complete\n");

			}

			// CLIENT WRITE

			// Send network reg using network byte order conversion
			value = htonll64(reg);	
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError writing network data\n");
				return(-1);
			}
			// Send network data's length as well
			value = htonll64(0);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError sending buf length\n");
				return(-1);
			}

			// CLIENT READ

			// Read network reg using network byte order conversion
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			response = ntohll64(value);
			// Read network's data length being received
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			value = ntohll64(value);
			// Only initialization so should return zero
			if (value != 0) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading from network\n");
				return (-1);
			}

			// Return successfully
			return (response);	
			break;	// Shouldn't need to reach here

		case(CART_OP_BZERO) :

			// CLIENT WRITE

			// Send network reg using network byte order conversion
			value = htonll64(reg);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError writing network data\n");
				return(-1);
			}
			// Send network data's length as well
			value = htonll64(0);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError sending buf length\n");
				return(-1);
			}

			// CLIENT READ

			// Read network reg using network byte order conversion
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			response = ntohll64(value);
			// Read network's data length being received
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			value = ntohll64(value);
			// Only zeroing the cartridges so should return zero
			if (value != 0) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading from network\n");
				return (-1);
			}

			// Return successfully
			return (response);
			break;	// Shouldn't need to reach here

		case(CART_OP_LDCART) :

			// CLIENT WRITE

			// Send network reg using network byte order conversion
			value = htonll64(reg);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError writing network data\n");
				return(-1);
			}
			// Send network data's length as well
			value = htonll64(0);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError sending buf length\n");
				return(-1);
			}

			// CLIENT READ

			// Read network reg using network byte order conversion
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			response = ntohll64(value);
			// Read network's data length being received
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			value = ntohll64(value);
			// Only loading the cartridges so should return zero
			if (value != 0) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading from network\n");
				return (-1);
			}

			// Return successfully
			return (response);
			break;	// Shouldn't need to reach here

		case(CART_OP_RDFRME) :

			// CLIENT WRITE

			// Send network reg using network byte order conversion
			value = htonll64(reg);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError writing network data\n");
				return(-1);
			}
			// Send network data's length as well
			value = htonll64(0);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError sending buf length\n");
				return(-1);
			}

			// CLIENT READ

			// Read network reg using network byte order conversion
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			response = ntohll64(value);
			// Read network's data length being received
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			value = ntohll64(value);
			// Read a frame sized buffer from the network
			if (value > 0) {
				if (read(socket_fd, buf, CART_FRAME_SIZE) != CART_FRAME_SIZE) {
					logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
					return(-1);
				}
			}

			// Return successfully
			return (response);
			break;	// Shouldn't need to reach here

		case(CART_OP_WRFRME) :

			// CLIENT WRITE

			// Send network reg using network byte order conversion
			value = htonll64(reg);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError writing network data\n");
				return(-1);
			}
			// Send network data's length as well
			value = htonll64(CART_FRAME_SIZE);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError sending buf length\n");
				return(-1);
			}
			// Send buffered data over network
			if (read(socket_fd, buf, CART_FRAME_SIZE) != CART_FRAME_SIZE) {
				logMessage(LOG_ERROR_LEVEL, "\nError sending network data\n");
				return(-1);
			}

			// CLIENT READ

			// Read network reg using network byte order conversion
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			response = ntohll64(value);
			// Read network's data length being received
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			value = ntohll64(value);
			// Read a frame sized buffer from the network
			if (value > 0) {
				if (read(socket_fd, buf, CART_FRAME_SIZE) != CART_FRAME_SIZE) {
					logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
					return(-1);
				}
			}

			// Return successfully
			return (response);
			break;	// Shouldn't need to reach here

		case(CART_OP_POWOFF) :

			// CLIENT WRITE

			// Send network reg using network byte order conversion
			value = htonll64(reg);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError writing network data\n");
				return(-1);
			}
			// Send network data's length as well
			value = htonll64(0);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError sending buf length\n");
				return(-1);
			}

			// CLIENT READ

			// Read network reg using network byte order conversion
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			response = ntohll64(value);
			// Read network's data length being received
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			value = ntohll64(value);
			// Read a frame sized buffer from the network
			if (value > 0) {
				if (read(socket_fd, buf, CART_FRAME_SIZE) != CART_FRAME_SIZE) {
					logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
					return(-1);
				}
			}

			// Power off the server connection
			close(socket_fd);
			socket_fd = -1;
			logMessage(LOG_INFO_LEVEL, "Successfully powered off server");

			// Return successfully
			return (response);
			break;	// Shouldn't need to reach here

		default:
			
			logMessage(LOG_ERROR_LEVEL, "\n No need to reach this area ? \n");

			// CLIENT WRITE

			// Send network reg using network byte order conversion
			value = htonll64(reg);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError writing network data\n");
				return(-1);
			}
			// Send network data's length as well
			value = htonll64(0);
			if (write(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError sending buf length\n");
				return(-1);
			}

			// CLIENT READ

			// Read network reg using network byte order conversion
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			response = ntohll64(value);
			// Read network's data length being received
			if (read(socket_fd, &value, sizeof(value)) != sizeof(value)) {
				logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
				return(-1);
			}
			value = ntohll64(value);
			// Read a frame sized buffer from the network
			if (value > 0) {
				if (read(socket_fd, buf, CART_FRAME_SIZE) != CART_FRAME_SIZE) {
					logMessage(LOG_ERROR_LEVEL, "\nError reading network data\n");
					return(-1);
				}
			}

			// Return successfully
			return (response);
			break;	// Shouldn't need to reach here

	}
}
