////////////////////////////////////////////////////////////////////////////////
//
//  File           : cart_driver.c
//  Description    : This is the implementation of the standardized IO functions
//                   for used to access the CRUD storage system.
//
//  Author         : Eric Traister
//  Last Modified  : 12/09/2016
//
////////////////////////////////////////////////////////////////////////////////

// Includes
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

// Project Includes
#include "cart_cache.h"
#include "cart_driver.h"
#include "cart_controller.h"
#include "cmpsc311_log.h"
#include "cmpsc311_util.h"

//
// Implementation
//

// Enumerations
typedef enum Flag {
        YES    =   0,
        NO     =   1
} Flag;

// Structures
typedef struct FileSystem{
        char                filename[CART_MAX_PATH_LENGTH];     // current file's filename being handled
        int16_t             filehandle;                         // file handle for current file
        uint32_t            fileposition;                       // position in file in bytes
        uint32_t            filelength;                         // size of the file in bytes   
        CartridgeIndex      cartIndex;                          // catridge current file exists in
        CartFrameIndex      frameIndex;                         // frame current file exists in
        Flag                openfile;                           // holds the state of file open or not
        Flag                incart;                             // holds state of file being in CART or not 

} FileSystem;
typedef struct FileTable{
        int16_t             filehandle;                         // file handle for current file
        Flag                isused;                             // holds the state of the current frame location used in CART memory or not       
} FileTable;

// Global Structures
FileSystem      *fileSystem = NULL;										// holds each file's state in the CART memory
FileTable       fileTable[CART_MAX_CARTRIDGES][CART_CARTRIDGE_SIZE];    // file allocation table sized 64 x 1024


// Global Variables
int		numFiles = 0;
Flag	cacheInit;
//int		DEBUG = 0;


// My Project Functions
int		load_this_cart(CartridgeIndex cart);
int		check_table_space(int16_t fd, int32_t count);
void *	get_file_pieces(int16_t fd);
int		get_num_pieces(int16_t fd);
int     allocateNewFile(void);			// allocates one new file into the file system heap memory


////////////////////////////////////////////////////////////////////////////////
//
// Function     : create_cart_opcode
// Description  : Creates a new opcode packed register to communicate with memory system
//
// Inputs       : 6 registers: KY1, KY2, Return, CART, FRAME, RESERVED
// Outputs      : 1 packed 64b register opcode
//
////////////////////////////////////////////////////////////////////////////////
CartXferRegister create_cart_opcode(uint64_t reg_KY1, uint64_t reg_KY2,
                                    uint64_t reg_RET, uint64_t reg_CART,
                                    uint64_t reg_FRAME, uint64_t reg_RESV) {
        CartXferRegister opcode;
        uint64_t ky1, ky2, ret, cart, frame, resv;  // uint64_t type for uint64_t CartXferRegister

        // Convert Intel's little endian to big endian for CART HRAM communication
        //  KY1         KY2         RET     CART        FRAME       RESERVED
        //  [63:56]     [55:48]     [47]    [46:31]     [30:15]     [14:0]
    
        ky1     =   (reg_KY1    << 56);
        ky2     =   (reg_KY2    << 48);        // unknown part, space reserved in opcode
        ret     =   (reg_RET    << 47);
        cart    =   (reg_CART   << 31);
        frame   =   (reg_FRAME  << 15);
        resv    =   (reg_RESV);                // unknown part, space reserved in opcode

        // logically OR together the individual opcode parts to obtain 64b big endian opcode needed
        opcode  =   (ky1 | ky2 | ret | cart | frame | resv);
        return (opcode);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : extract_cart_opcode
// Description  : Extracts the specific segment of the 64b opcode
//
// Inputs       : 64b opcode response, register field
// Outputs      : Extracted register field
//
////////////////////////////////////////////////////////////////////////////////
uint64_t extract_cart_opcode(CartXferRegister resp, CartRegisters reg_field) {
    
        CartXferRegister reg_seg;
        reg_seg = resp;

        // Create ability to extract any relevant part of OPCODE sent to CART for checks
        switch(reg_field) {
            case(CART_REG_KY1):
                reg_seg     =   (reg_seg >> 56);
                break;
            case(CART_REG_KY2):
                reg_seg     =   ((reg_seg >> 48) & 0xFF);   // logical right shift 48 bits and logical AND with ... 1111 1111
                break;
            case(CART_REG_RT1):      
                reg_seg     =   ((reg_seg >> 47) & 0x01);   // logical right shift 47 bits and logical AND with ... 0001
                break;
            case(CART_REG_CT1):
                reg_seg     =   ((reg_seg >> 31) & 0xFFFF); // logical right shift 31 bits and logical AND with ... 1111 1111 1111 1111
                break;
            case(CART_REG_FM1):
            	reg_seg     =   ((reg_seg >> 15) & 0xFFFF); // logical right shift 15 bits and logical AND with ... 1111 1111 1111 1111
           	    break;           
            default:                                        // unused: CART_REG_MAXVAL, access to unused/reserved bits
                logMessage(LOG_ERROR_LEVEL,"extract_card_opcode error: wrong reg_field passed for response");
        }
        return (reg_seg);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_poweron
// Description  : Startup up the CART interface, initialize filesystem
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int32_t cart_poweron(void) {

        // Local Variables
        int                 cacheResp = 0;
		int                 i = 0, j = 0;
        CartXferRegister    resp = 0;

// POWERON CACHE

        // Initialize the Cache system
        cacheResp = init_cart_cache();
		
		// Checks
        if(cacheResp != 0){
            logMessage(LOG_ERROR_LEVEL,"\nError initializing the Cache system in cart_poweron\n");
            return (-1);
        }     
        if(++numFiles != 1){
            logMessage(LOG_ERROR_LEVEL,"\nNumber of files in initial cart_poweron exceeds 1\n");
            return (-1);
        }

// POWERON FILE SYSTEM

		// Initialize the file system
        fileSystem  = calloc(numFiles, sizeof(FileSystem));   // number of files in file system originally will be one

        // Setup the file system's data structure to track the state of the user's files  
        for(i = 0; i < numFiles; i++){  
			strncpy(fileSystem[i].filename, "", CART_MAX_PATH_LENGTH);	// Initialize the filename for each instance of FileState struct to '\0'
            fileSystem[i].filehandle    =   -1;          // Unused/invalid file handles will be negative numbers
            fileSystem[i].fileposition  =    0;          // Initialize default file position to zero
            fileSystem[i].filelength    =    0;          // Initialize default file size to zero     
            fileSystem[i].openfile      =   NO;          // Initalize all files to not open status
            fileSystem[i].incart        =   NO;          // Initialze all files to not incast status
	        fileSystem[i].cartIndex     =    0;          // Initialize default cartridge the file exists in to 0
	        fileSystem[i].frameIndex    =    0;          // Initialize default frame the file exists in to 0
        }

        // Setup file table's data structure to keep track of open files inside CART memory 
        for(i = 0; i < CART_MAX_CARTRIDGES; i++){  
            for(j = 0; j < CART_CARTRIDGE_SIZE; j++){
                fileTable[i][j].filehandle  =   -1;      // Unused/invalid file handles will be negative numbers
                fileTable[i][j].isused      =   NO;      // Initialize all table slots in CART memory to not used
            }
        } 

// POWERON CART SYSTEM

        // Initialize the memory system
        resp =  client_cart_bus_request(create_cart_opcode(CART_OP_INITMS, 0, 0, 0, 0, 0), NULL);

        // Check return register
        if(extract_cart_opcode(resp, CART_REG_RT1) != 0){
            logMessage(LOG_ERROR_LEVEL,"\nCartridge Memory System intialization failed\n");
            return (-1);
        }

        // BZERO out the cartridges
        for(i = 0; i < CART_MAX_CARTRIDGES; i++){      
            // Load each cartridge in the CART system
            resp = client_cart_bus_request(create_cart_opcode(CART_OP_LDCART, 0, 0, (i), 0, 0), NULL);
            // Check return register
            if(extract_cart_opcode(resp, CART_REG_RT1) != 0){
                logMessage(LOG_ERROR_LEVEL, "\nError loading cartridge: %u \n", i);
                return (-1);
            }
            // Zero out each cartridge
            resp = client_cart_bus_request(create_cart_opcode(CART_OP_BZERO, 0, 0, 0, 0, 0), NULL);
            // Check return register
            if(extract_cart_opcode(resp, CART_REG_RT1) != 0) {
                logMessage(LOG_ERROR_LEVEL,"\nError zeroing catridge: %u \n", i);
                return (-1);
            }
        }

// POWERON COMPLETED

        // Return successfully
        logMessage(LOG_INFO_LEVEL,"\nCompleted cart_poweron: initialized memory system, cache system, and zeroed out cartridges.\n");
        return(0);

}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_poweroff
// Description  : Shut down the CART interface, close all files
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int32_t cart_poweroff(void) {

	// Local Variables
	int                 cacheResp = 0;
	int                 i = 0, j = 0;
	CartXferRegister    resp = 0;

	// Shut down the cache system
	cacheResp = close_cart_cache();

	// Check cache system shut down as intended
	if (cacheResp != 0) {
		logMessage(LOG_ERROR_LEVEL, "\nError closing the Cache system in cart_poweroff\n");
		return (-1);
	}

	// Zero out file system's data structure for good practice
	for (i = 0; i < numFiles; i++) {
		strncpy(fileSystem[i].filename, "", CART_MAX_PATH_LENGTH);      // Initialize the filename for each instance of FileState struct to '\0'
		fileSystem[i].filehandle = -1;          // Unused/invalid file handles will be negative numbers
		fileSystem[i].fileposition = 0;          // Initialize default file position to zero
		fileSystem[i].filelength = 0;          // Initialize default file size to zero     
		fileSystem[i].openfile = NO;          // Initalize all files to not open status
		fileSystem[i].incart = NO;          // Initialze all files to not incast status
		fileSystem[i].cartIndex = 0;          // Initialize default cartridge the file exists in to 0
		fileSystem[i].frameIndex = 0;          // Initialize default frame the file exists in to 0
	}

	// Zero out file table's data structure for good practice
	for (i = 0; i < CART_MAX_CARTRIDGES; i++) {
		for (j = 0; j < CART_CARTRIDGE_SIZE; j++) {
			fileTable[i][j].filehandle = -1;      // Unused/invalid file handles will be negative numbers
			fileTable[i][j].isused = NO;      // Initialize all table slots in CART memory to not used
		}
	}

	// Execute the CART shutdown opcode
	resp = client_cart_bus_request(create_cart_opcode(CART_OP_POWOFF, 0, 0, 0, 0, 0), NULL);

	// Check return register
	if (extract_cart_opcode(resp, CART_REG_RT1) != 0) {
		logMessage(LOG_ERROR_LEVEL, "\nUnable to shutdown cart system in CART_POWEROFF\n");
		return (-1);
	}

	// Return successfully
	logMessage(LOG_INFO_LEVEL, "\nCompleted cart_poweroff: successfully free'd cache, file, and cart memory systems\n");
	return(0);

}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_open
// Description  : This function opens the file and returns a file handle
//
// Inputs       : path - filename of the file to open
// Outputs      : file handle if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int16_t cart_open(char *path) {

	// Local Variables
	int         allocResp = 0;
	int         i = 0;
	int16_t     filehandle = -1;	// set filehandle to invalid case, in case of error
	int			path_length = 0;

	// Check if path is good
	if (path == NULL) {
		logMessage(LOG_ERROR_LEVEL, "\nNULL path passed into cart_open\n");
		return(-1);
	}
	
	path_length = strlen(path) + 1;

	check_file_system:
	// Iterate through the number of files open in the file system; first cart_open will only have one file
	for (i = 0; i < numFiles; i++) {
		// Error: path is found to be same of an existing filename in the fileSystem struct
		if (strncmp(path, fileSystem[i].filename, path_length) == 0) {
			logMessage(LOG_ERROR_LEVEL, "\n filename: \t %c \t is already in the filesystem \n filehandle: \t %u \n", fileSystem[i].filename, fileSystem[i].filehandle);
			return (-1);
		}

		// Empty slot reached in fileSystem
		if (fileSystem[i].openfile == NO) {
			strncpy(fileSystem[i].filename, path, path_length);    // copy path/filename into corresponding struct member
			fileSystem[i].filehandle = i;          // set new file handle to index for uniqueness
			fileSystem[i].fileposition = 0;          // set new file position to 0
			fileSystem[i].filelength = 0;          // set new file size to 0        
			fileSystem[i].openfile = YES;          // set file open flag to yes
			fileSystem[i].cartIndex = 0;          // set default first cartridge to 0
			fileSystem[i].frameIndex = 0;          // set default first frame to 0           
			filehandle = i;          // set the filehandle to be returned to the index

			return (filehandle);    // Return the new file's filehandle
		}
	}
	// Allocate a new file if code reaches this statement
	allocResp = allocateNewFile();
		
	// Check allocation response to ensure new file added to file system's data structure
	if (allocResp != 0) {
		logMessage(LOG_ERROR_LEVEL, "\nUnable to allocate new file in file system heap memory in cart_open\n");
		return (-1);
	}

	goto check_file_system;

	// Return unique file handle for subsequent operations
	return (filehandle);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_close
// Description  : This function closes the file
//
// Inputs       : fd - the file descriptor
// Outputs      : 0 if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int16_t cart_close(int16_t fd) {

        // Close the file and zero out struct's data members
        if(fileSystem[fd].filehandle >= (int16_t) (0) && fileSystem[fd].openfile == YES){
            *(fileSystem[fd].filename)      =  '\0';              // reset the filename pointer back to null terminator
            fileSystem[fd].filehandle       =    -1;              // reset filehandle to negative value to indicate unused/invalid
            fileSystem[fd].fileposition     =     0;              // reset file position to zero bytes
            fileSystem[fd].filelength       =     0;              // reset file size to zero bytes
            fileSystem[fd].openfile         =    NO;              // reset file open flag to closed
            fileSystem[fd].cartIndex        =     0;              // reset cartridge location to 0
            fileSystem[fd].frameIndex       =     0;              // reset frame location to 0
        }
        // Failed to close file, error condition met
        else{
            logMessage(LOG_ERROR_LEVEL, "\nFailed to close file in cart_close\n");
            return (-1);
        }

	    // Return successfully
	    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_read
// Description  : Reads "count" bytes from the file handle "fh" into the 
//                buffer "buf"
//
// Inputs       : fd - filename of the file to read from
//                buf - pointer to buffer to read into
//                count - number of bytes to read
// Outputs      : bytes read if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int32_t cart_read(int16_t fd, void *buf, int32_t count) {

        // Local Variables
		int					i = 0;	
        int                 return_count = 0;		// returns a legal number of bytes read from file
        int                 length      = 0;		// holds the length of extracted frames
		int					file_occurrences = 0;	// holds the number frames the file exists in CART
		int					file_frames = 0;		// counter to help loop through frames remaining
        int                 iteration   = 0;		// counter to keep track of the loop iteration

		char				*cache_buffer = NULL;
        char                cart_buffer[CART_FRAME_SIZE];	// holds one frame extracted from the CART memory
        char                local_file_buffer[fileSystem[fd].filelength];	// holds local copy of file referenced by fd

        CartXferRegister    resp = 0;	// response instance when interacting with CART system
        CartridgeIndex      the_cart    = fileSystem[fd].cartIndex;	/// might be able to use tempFile
        CartFrameIndex      the_frame   = fileSystem[fd].frameIndex;/// might be able to use tempFile

        Flag                ret_count;	// tracks legal return number of bytes

		FileSystem			*tempFile = NULL;	// temporary/local copy of a data structure instance
		Flag				goodFile = NO;

        // Check illegal bounds for 'count' bytes
        if(count < 0){
            logMessage(LOG_ERROR_LEVEL,"\nEror in CART_READ: count bytes is less than zero\n");
            return (-1);
        }
        // Check illegal file referenced by fd if not valid or was previously open in fileSystem
        if (fileSystem[fd].filehandle < 0 || fileSystem[fd].openfile == NO){
            logMessage(LOG_ERROR_LEVEL, "\nFile is not opened or valid in fileSystem : occurence in CART_READ\n");
            return (-1);
        }
		// Search file system to ensure we have a legal file
		for (i = 0; i < numFiles; i++) {
			if (fileSystem[fd].filehandle == fd)
				goodFile = YES;
		}
		if (goodFile != YES)
			return (-1);

		// Find number of frames the file exists in CART memory
		file_occurrences = get_num_pieces(fd);

		// Find all frames the file exists in CART memory
		tempFile = get_file_pieces(fd);	
		if (tempFile == NULL)
			return(-1);

		// Set the initial cart and load it
		the_cart = tempFile[0].cartIndex;
		load_this_cart(the_cart);

		// Set the initial frame the file exists in
		the_frame = tempFile[0].frameIndex;

		// Set the file frame occurrences counter
		iteration = 0;
		file_frames = file_occurrences;

	read_file_frame:
		// Read the file from a memory system (CACHE or CART) frame by frame into a local file buffer
		while (file_frames > 0) {

			// Always try get_cart_cache first to see if frame exists in cache
			cache_buffer = (char *)get_cart_cache(the_cart, the_frame);

			// File frame exists in cache ==> Read from CACHE memory
			if (cache_buffer != NULL) {
				logMessage(LOG_INFO_LEVEL, "\nFrame found in cache -- Using cache copy instead of CART memory\n");

				// Update the file frame's length
				length = strlen(cache_buffer);

				// Copy the pulled cache file frame into the local file buffer
				memcpy(&local_file_buffer[iteration * CART_FRAME_SIZE], cache_buffer, length);

				// Update the counters
				file_frames--;
				iteration++;

				// Return to before while loop to continue reading file if necessary
				goto read_file_frame;
			}
			// File Frame is not in cache ==> Read from CART memory
			else {
				logMessage(LOG_INFO_LEVEL, "\nFrame not in cache -- continuing to read from CART memory\n");

				// Load the cartridge the next file frame piece exists in
				if (the_cart != tempFile[iteration].cartIndex) {
					the_cart = tempFile[iteration].cartIndex;	// update cartridge to latest read position
					load_this_cart(the_cart);
				}

				// Read the next frame piece the file exists into local cart buffer
				the_frame = tempFile[iteration].frameIndex;		// update frame to latest read position

				resp = client_cart_bus_request(create_cart_opcode(CART_OP_RDFRME, 0, 0, 0, the_frame, 0), cart_buffer);
				// Check return register
				if (extract_cart_opcode(resp, CART_REG_RT1) != 0) {
					logMessage(LOG_ERROR_LEVEL, "\nCartridge reading failed in CART_READ\n");
					return (-1);
				}

				// Update the new file frames's length
				length = strlen((char *)cart_buffer);

				// Copy the pulled file frame from the CART into local file buffer
				memcpy(&local_file_buffer[iteration * CART_FRAME_SIZE], cart_buffer, length);

				// Update the counters
				file_frames--;
				iteration++;

				// Return to before while loop to continue reading file if necessary
				goto read_file_frame;
			}

		}

		// Done reading from memory systems; free heap data
		free(tempFile);
		tempFile = NULL;

        // Setup return_count in case count bytes requests more than available
        return_count = fileSystem[fd].filelength - fileSystem[fd].fileposition;

        // Requesting 'count' bytes will be obtainable from file, file size will remain the same
        if(count <= (fileSystem[fd].filelength - fileSystem[fd].fileposition)){   
            memcpy(buf, &local_file_buffer[fileSystem[fd].fileposition], count);           // Copy the file read from the CART into the buf pointer for 'count' bytes   
            fileSystem[fd].fileposition += count;                                          // Update the fileposition to end of 'count' bytes just read
            ret_count = YES;
        }
        // Requesting 'count' bytes is more than file allows from referenced position, only return to EOF
        else{
			if (fileSystem[fd].filelength - fileSystem[fd].fileposition == 0)
				return (-1);
            memcpy(buf, &local_file_buffer[fileSystem[fd].fileposition], return_count);    // Copy the file read from the CART into the buf pointer up to the EOF 
            fileSystem[fd].fileposition = fileSystem[fd].filelength;                       // Update the fileposition to EOF
            ret_count = NO;
        }        

        // Returns
        if(ret_count == YES)
            return (count);             // return count bytes for normal occurrence
        else
            return (return_count);      // Return number of bytes from fileposition to EOF
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_write
// Description  : Writes "count" bytes to the file handle "fh" from the 
//                buffer  "buf"
//
// Inputs       : fd - filename of the file to write to
//                buf - pointer to buffer to write from
//                count - number of bytes to write
// Outputs      : bytes written if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int32_t cart_write(int16_t fd, void *buf, int32_t count) {

        // Local Variables
        int                 i = 0, j = 0;		// loop counters 
        int                 counter     = 0;	// tracks the number of bytes left to be written
        int                 iteration   = 0;	// updates the file properties based on write order
        int                 the_count = count;
		int					buf_size = 0;		// determines size of buffer for estimated local file buffer

        char                cart_buffer[CART_FRAME_SIZE] = 
                            { [0 ... CART_FRAME_SIZE - 1] = '\0'};                      // holds a frame that will be written into CART memory
		char				*local_file_buffer = NULL;									// holds the entire file pulled from CART memory

        CartXferRegister    resp = 0;													// CART response for checking
        CartridgeIndex      the_cart = fileSystem[fd].cartIndex;                        // uppdated in new file write to first location available
        CartFrameIndex      the_frame = fileSystem[fd].frameIndex;                      // uppdated in new file write to first location available
        
		int					cacheResp = 0;
		int					checkSpace = 0;

		FileSystem			*tempFile = NULL;
		
		Flag				goodFile = NO;


        // Check illegal bounds for 'count' bytes (less than 0)
        if(count < 0){
            logMessage(LOG_ERROR_LEVEL,"\nEror in CART_WRITE:\tcount bytes is less than zero\n");
            return (-1);
        }
        // Check illegal fd if not valid (negative number file handle or closed file flag)
        if(fileSystem[fd].filehandle < 0 || fileSystem[fd].openfile == NO){
            logMessage(LOG_ERROR_LEVEL, "\nFile is not opened or valid in fileSystem : occurence in CART_WRITE\n");
            return (-1);
        }
		// Search file system to ensure we have a legal file
		for (i = 0; i < numFiles; i++) {
			if (fileSystem[fd].filehandle == fd)
				goodFile = YES;
		}
		if (goodFile != YES)
			return (-1);

		// Create a local file buffer large enough to hold the file and new data to be written
		buf_size = strlen((char *)buf);

		if (fileSystem[fd].filelength > 0)
			local_file_buffer = calloc(buf_size + fileSystem[fd].filelength, sizeof(char));
		else
			local_file_buffer = calloc(count, sizeof(char));

        // File not previously written to CART memory ==> FIRST WRITE
        if(fileSystem[fd].incart == NO){

            // Clear the buffer for good practice
            for(i = 0; i < CART_FRAME_SIZE; i++)
                cart_buffer[i] = '\0'; 

            // Update the counters to default values
            counter = count;
            iteration = 0;

		search_file_table:
            // Get the next frame that is available and jump to cart_info_found
            for(i = 0; i < CART_MAX_CARTRIDGES; i++){
                for(j = 0; j < CART_CARTRIDGE_SIZE; j++){
                    // Set the first frame's location that is available in CART memory
                    if(fileTable[i][j].isused == NO){
                        the_cart = i;
                        the_frame = j;
                        goto cart_info_found;
                    }
                }
            }
        
		cart_info_found: 
			// Write remaining (counter) bytes to CART memory
			while(counter > 0){
                
                // (1) - Update the local cart_buffer that is going to write exactly 1 frame for every loop iteration
                if(counter <= CART_FRAME_SIZE){
                    memcpy(&cart_buffer, &((char *) buf)[iteration * CART_FRAME_SIZE], counter);            // count bytes or counter bytes left
                    counter -= counter;
                }
                else{
                    memcpy(&cart_buffer, &((char *) buf)[iteration * CART_FRAME_SIZE], CART_FRAME_SIZE);    // frame sized worth of bytes
                    counter -= CART_FRAME_SIZE;
                }

                // (2) - Load the cartridge that was previously found
				load_this_cart(the_cart);    

		// WRITE THROUGH TO CART MEMORY FIRST

                // (3) - Write to the first frame available that was previously found
                resp =  client_cart_bus_request(create_cart_opcode(CART_OP_WRFRME, 0, 0, 0, the_frame, 0), cart_buffer);
                if(extract_cart_opcode(resp, CART_REG_RT1) != 0){
                    logMessage(LOG_ERROR_LEVEL,"Cartridge loading failed in CART_WRITE");
                    return (-1);
                }                
				
		// WRITE TO CACHE FOR FASTER TEMPORAL READ ACCESSES

					// Add frame to the cache
					cacheResp = put_cart_cache(the_cart, the_frame, cart_buffer);
					if (cacheResp != 0)
						return (-1);

                // (4) - On the FIRST WRITE to file, set the first cart/frame locations
                if(iteration == 0){
                    fileSystem[fd].cartIndex					= the_cart;
                    fileSystem[fd].frameIndex					= the_frame;
					fileSystem[fd].incart						= YES;
					fileTable[the_cart][the_frame].filehandle	= fd;
					fileTable[the_cart][the_frame].isused		= YES;
                }
                // (5) - On successive writes, set the next cart/frame
                else{
                    if(the_cart == 0 || the_frame == 0)
                        return (-1);
                    if(the_cart == CART_MAX_CARTRIDGES || the_frame == CART_CARTRIDGE_SIZE)
                        return (-1);

					fileTable[the_cart][the_frame].filehandle	 = fd;
					fileTable[the_cart][the_frame].isused		 = YES;
                }
                
                // (6) - Update the file properties
                fileSystem[fd].fileposition	= count;    // since this is first write, file position will be equal to count
                fileSystem[fd].filelength	= count;    // since this is first write, file length will be equal to count

                // (7) - Update loop iteration
                iteration++;

                // (8) - Get the next cart/frame (while loop will exit before data is used if not needed)
				goto search_file_table;

            }

        }
        // File already exists in CART memory ==> SUCCESSIVE WRITES
        else{

			// Clear the buffer for good practice
			for (i = 0; i < CART_FRAME_SIZE; i++)
				cart_buffer[i] = '\0';

            // Update the counters to default values
            counter = fileSystem[fd].filelength;
            iteration = 0;

			// Determine if there is space in the CART memory to fulfill the write request
			checkSpace = check_table_space(fd, count);

			// Check the table space result
			if (checkSpace != 0) {
				logMessage(LOG_ERROR_LEVEL, "\nNot enough space in CART memory to fulfill write request!!!\n");
				return (-1);
			}

			// Find all frames the file exists in CART memory
			tempFile = get_file_pieces(fd);	
			if (tempFile == NULL)
				return(-1);

			// Set the initial cart and load it
			the_cart	= tempFile[0].cartIndex;
			load_this_cart(the_cart);

			// Set the initial frame the file exists in
			the_frame	= tempFile[0].frameIndex;

			// Read/extract file from CART memory to do appending
			while(counter > 0){

				// (1) - Ensure we are loading from the correct cart retrieved from the file table
				if (tempFile[iteration].cartIndex != the_cart) {
					the_cart = tempFile[iteration].cartIndex;
					load_this_cart(the_cart);
				}

				// If there is a next frame, set it, otherwise while loop will end if not
				the_frame = tempFile[iteration].frameIndex;

                // (2) - Read a frame size amount of the file into the cart_buffer
                resp =  client_cart_bus_request(create_cart_opcode(CART_OP_RDFRME, 0, 0, 0, the_frame, 0), cart_buffer);
                if(extract_cart_opcode(resp, CART_REG_RT1) != 0){
                    logMessage(LOG_ERROR_LEVEL,"Cartridge loading failed in CART_WRITE");
                    return (-1);
                }    

                // (3) - Set the isused flag to NO as this frame may be overwritten when the file is appended
                fileTable[the_cart][the_frame].isused = NO;

                // (4) - Copy the cart_buffer extracted frame by frame into the local_file_buffer
				if (counter >= CART_FRAME_SIZE) {
					memcpy(&local_file_buffer[iteration * CART_FRAME_SIZE], cart_buffer, CART_FRAME_SIZE);
				}
				else {
					memcpy(&local_file_buffer[iteration * CART_FRAME_SIZE], cart_buffer, counter);
				}
                
                // (5) - Update the counters
                counter -= CART_FRAME_SIZE;
                iteration++;
            }

            // Now append the local_file_buffer
            memcpy(&local_file_buffer[fileSystem[fd].fileposition], (char *) buf, the_count);

            // Update the file properties
			// Write request is more bytes than the file size so increase filelength
			if (fileSystem[fd].filelength < fileSystem[fd].fileposition + buf_size) {
				fileSystem[fd].filelength = strlen(local_file_buffer);
				fileSystem[fd].fileposition = fileSystem[fd].filelength;
			}
			else {
				fileSystem[fd].filelength = strlen(local_file_buffer);
				fileSystem[fd].fileposition = fileSystem[fd].fileposition + the_count;
			}            

			// Update and reset the iterations and counter to the number of bytes to be written
            counter = fileSystem[fd].filelength;    
			iteration = 0;

            // Get the next frame that is available to write to from fileTable
            for(i = 0; i < CART_MAX_CARTRIDGES; i++){
                for(j = 0; j < CART_CARTRIDGE_SIZE; j++){
                    // (1) - Set the first frame's location that is available in CART memory
                    if(fileTable[i][j].isused == NO){
                        the_cart = i;
                        the_frame = j;
                        goto cart_write_back;
                    }
                }
            }
            
		cart_write_back: 
			// Write back the file when cart/frame to use is defined
			while(counter > 0){

                // (1) - Clear the buffer for good practice
                for(i = 0; i < CART_FRAME_SIZE; i++)
                    cart_buffer[i] = '\0'; 

                // (2) - Update the local cart_buffer that is going to write exactly 1 frame for every loop iteration
                if(counter <= CART_FRAME_SIZE){
                    memcpy(&cart_buffer, &(local_file_buffer)[iteration * CART_FRAME_SIZE], counter);            // count bytes or counter bytes left
                    counter -= counter;
                }
                else{
                    memcpy(&cart_buffer, &(local_file_buffer)[iteration * CART_FRAME_SIZE], CART_FRAME_SIZE);    // frame sized worth of bytes
                    counter -= CART_FRAME_SIZE;
                }

                // (3) - Load the cartridge that was previously found
				load_this_cart(the_cart);

		// WRITE THROUGH TO CART MEMORY FIRST

                // (4) - Write to the first frame available that was previously found
                resp =  client_cart_bus_request(create_cart_opcode(CART_OP_WRFRME, 0, 0, 0, the_frame, 0), cart_buffer);
                if(extract_cart_opcode(resp, CART_REG_RT1) != 0){
                    logMessage(LOG_ERROR_LEVEL,"Cartridge loading failed in CART_WRITE");
                    return (-1);
                }    

		// WRITE TO CACHE FOR FASTER TEMPORAL READ ACCESSES

					// Add frame to the cache
					cacheResp = put_cart_cache(the_cart, the_frame, cart_buffer);
					if (cacheResp != 0)
						return (-1);

                // (5) - On the FIRST WRITE to file, set the first cart/frame locations
                if(iteration == 0){
                    fileSystem[fd].cartIndex  = the_cart;
                    fileSystem[fd].frameIndex = the_frame;
					fileTable[the_cart][the_frame].filehandle = fd;
					fileTable[the_cart][the_frame].isused = YES;
                }
                // (6) - On successive writes, set the next cart/frame
                else{
                    if(the_cart == 0 && the_frame == 0)
                        return (-1);
                    if(the_cart == CART_MAX_CARTRIDGES || the_frame == CART_CARTRIDGE_SIZE)
                        return (-1);

						fileTable[the_cart][the_frame].filehandle	= fd;
						fileTable[the_cart][the_frame].isused		= YES;
                   
                }

                // (7) - Update loop iteration
                iteration++;

                // (8) - Get the next cart/frame (while loop will exit before data is used if not needed)
                for(i = 0; i < CART_MAX_CARTRIDGES; i++){
                    for(j = 0; j < CART_CARTRIDGE_SIZE; j++){
                        // Set the first frame's location that is available in CART memory
                        if(fileTable[i][j].isused == NO){
                            the_cart  = i;
                            the_frame = j;
                            goto cart_write_back;
                        }
                    }
                }

            }
        }

		// Free the local file buffer from the heap
		if (local_file_buffer != NULL) {
			free(local_file_buffer);
			local_file_buffer = NULL;
		}

		// Free the temporary data structure from the heap
		free(tempFile);
		tempFile = NULL;

        // Return successfully
        return (the_count);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_seek
// Description  : Seek to specific point in the file
//
// Inputs       : fd - filename of the file to write to
//                loc - offfset of file in relation to beginning of file
// Outputs      : 0 if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int32_t cart_seek(int16_t fd, uint32_t loc) {

    // STEP 1 - Check illegal bounds for 'loc' bytes
    if(loc < 0 || loc > fileSystem[fd].filelength){
        logMessage(LOG_ERROR_LEVEL, "\nIllegal attempt to change fileposition location in CART_SEEK\n");
        return (-1);
    }

    // STEP 2 - Set the seek position to the current file's fileposition
    fileSystem[fd].fileposition = loc;

    // STEP 3 - Return successfully
    return (0);
}


//
// MY FUNCTIONS
//

int load_this_cart(CartridgeIndex cart) {
	CartXferRegister resp;

	resp = client_cart_bus_request(create_cart_opcode(CART_OP_LDCART, 0, 0, cart, 0, 0), NULL);

	if (extract_cart_opcode(resp, CART_REG_RT1) != 0) {
		logMessage(LOG_ERROR_LEVEL, "\nCartridge loading failed for cart: %u !\n", cart);
		return (-1);
	}

	return(0);
}

int check_table_space(int16_t fd, int32_t count) {

	// Local Variables
	int			i = 0, j = 0, counter = 0;
	uint32_t	length = 0;
	double		new_frames_needed = 0.0;

	// First handle obvious case where no new frames in CART will be needed
	if (fileSystem[fd].filelength >= fileSystem[fd].fileposition + count)
		return (0);
	// Determine additional frames needed and search file table to see if enough space exists
	else {

		// Set parameters to check when scanning file table
		length				= fileSystem[fd].filelength + count;
		new_frames_needed	= ceil((double)length / (double)CART_FRAME_SIZE);

		// Search the file table for number of frames available
		for (i = 0; i < CART_MAX_CARTRIDGES; i++) {
			for (j = 0; j < CART_CARTRIDGE_SIZE; j++) {
				if (fileTable[i][j].isused == NO) {
					counter++;
				}
			}
		}

		// If more frames are needed than available space in CART, return failure
		if (new_frames_needed > counter)
			return (-1);

	}

	// Return successfully
	return (0);
}

void * get_file_pieces(int16_t fd) {

	// Local Variables
	FileSystem	*tempFile = NULL;
	int i = 0, j = 0;
	int	counter = 0, pieces = 0;

	// Check to make sure file contains some frames
	if (fileSystem[fd].filelength < 1) {
		logMessage(LOG_ERROR_LEVEL, "\nError in get_file_pieces() : file does not contain any frames\n");
		return (NULL);
	}

	// Find out how many occurrences/pieces the file exists in the file table
	for (i = 0; i < CART_MAX_CARTRIDGES; i++) {
		for (j = 0; j < CART_CARTRIDGE_SIZE; j++) {
			if (fileTable[i][j].filehandle == fd && fileTable[i][j].isused == YES) {
					pieces++;
			}
		}
	}

	// Allocate space for temporary file 'array'
	tempFile = calloc(pieces, sizeof(FileSystem));

	// Loop through pieces in CART memory to obtain file existance structure
	while (pieces > 0) {
		for (i = 0; i < CART_MAX_CARTRIDGES; i++) {
			for (j = 0; j < CART_CARTRIDGE_SIZE; j++) {
				if (fileTable[i][j].filehandle == fd && fileTable[i][j].isused == YES) {
					tempFile[counter].cartIndex = i;
					tempFile[counter].frameIndex = j;
					counter++;
					pieces--;
				}
			}
		}
	}

	// Return successfully
	return (tempFile);
}

int get_num_pieces(int16_t fd) {

	// Local Variables
	int i = 0, j = 0;
	int	pieces = 0;

	// Check to make sure file contains some frames
	if (fileSystem[fd].filelength < 1) {
		logMessage(LOG_ERROR_LEVEL, "\nError in get_file_pieces() : file does not contain any frames\n");
		return (-1);
	}

	// Find out how many occurrences/pieces the file exists in the file table
	for (i = 0; i < CART_MAX_CARTRIDGES; i++) {
		for (j = 0; j < CART_CARTRIDGE_SIZE; j++) {
			if (fileTable[i][j].filehandle == fd && fileTable[i][j].isused == YES) {
				pieces++;
			}
		}
	}

	// Return successfully
	return (pieces);
}

int allocateNewFile(void){
    
    // Local Variables
    int i = 0;
    int origFileCount = numFiles;

    // Allocate one new file into file system heap memory
    numFiles++;
    fileSystem  = realloc(fileSystem, sizeof(FileSystem)*numFiles);

    // Initialize new memory allocated for the additional one file added
    for(i = origFileCount; i < numFiles; i++){
        strncpy(fileSystem[i].filename, "", CART_MAX_PATH_LENGTH);      // Initialize the filename for each instance of FileState struct to '\0'
        fileSystem[i].filehandle    =   -1;          // Unused/invalid file handles will be negative numbers
        fileSystem[i].fileposition  =    0;          // Initialize default file position to zero
        fileSystem[i].filelength    =    0;          // Initialize default file size to zero     
        fileSystem[i].openfile      =   NO;          // Initalize all files to not open status
        fileSystem[i].incart        =   NO;          // Initialze all files to not incast status
        fileSystem[i].cartIndex     =    0;          // Initialize default cartridge the file exists in to 0
        fileSystem[i].frameIndex    =    0;          // Initialize default frame the file exists in to 0    
    }

    // Return successfully
    return (0);
}