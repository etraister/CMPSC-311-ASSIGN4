////////////////////////////////////////////////////////////////////////////////
//
//  File           : cart_cache.c
//  Description    : This is the implementation of the cache for the CART
//                   driver.
//
//  Author         : Eric Traister
//  Last Modified  : 11/14/2016
//
////////////////////////////////////////////////////////////////////////////////

// Includes
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

// Project includes
#include "cart_cache.h"
#include "cart_driver.h"
#include "cart_controller.h"
#include "cmpsc311_log.h"
#include "cmpsc311_util.h"

// Defines
#define CACHE_MAX_OPEN_FILES 128

// Enumerations
typedef enum Flag {
	YES = 0,
	NO = 1
} Flag;

// Structures
typedef struct CacheTable{
		CartFrame			cachedFrame;		// holds the cached frame
		uint32_t			cacheHandle;		// holds a unique identifier for cart/frame 

		CartridgeIndex		cartIndex;			// tracks where the frame belongs in CART memory
		CartFrameIndex		frameIndex;			// tracks where the frame belongs in CART memory

		Flag				isUsed;				// holds state if the frame is in the cache
		int					lru;				// holds lruCounter, lower number is LRU
} CacheTable;

// Global Structures
CacheTable		*cacheMemory = NULL;

// Global Variables
CartFrame		last_cached_frame;	// holds the last cached frame if needed when deleting from cache
uint32_t		cacheSize = 0;		// holds the size of the cache in number of frames
Flag			cacheInit = NO;		// holds the state if cache is initialized or not
int				lruCounter = 0;		// updates every access to maintain cache policy

// Local Project Functions
void * delete_cart_cache(CartridgeIndex cart, CartFrameIndex blk);

// My Project Functions
uint32_t create_cache_tag(CartridgeIndex cart, CartFrameIndex frame);

//
// Functions
//


////////////////////////////////////////////////////////////////////////////////
//
// Function     : set_cart_cache_size
// Description  : Set the size of the cache (must be called before init)
//
// Inputs       : max_frames - the maximum number of items your cache can hold
// Outputs      : 0 if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int set_cart_cache_size(uint32_t max_frames) {

	// Illegal cache size requested (less than 0 or greater than cache size limit)
	if(max_frames < 0 ||  max_frames > CACHE_MAX_OPEN_FILES){
		logMessage(LOG_ERROR_LEVEL, "\nIllegal cache size requested: %u \n", max_frames);
		return(-1);
	}

	// Illegal cache of size 0 requested (?)
	if(max_frames == 0){
		logMessage(LOG_ERROR_LEVEL, "\nIllegal cache size requested of SIZE 0: %u \n", max_frames);
		return(-1);
	}

	// Set the cache size after performing checks
	cacheSize = max_frames;

	// Return successfully
	return (0);
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : init_cart_cache
// Description  : Initialize the cache and note maximum frames
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int init_cart_cache(void) {

	// Local Variables;
	int		i = 0;

	// Check to make sure we aren't initializing the cache multiple times (without cart_poweroff to clean up)
	if(cacheInit == YES){
		logMessage(LOG_ERROR_LEVEL, "\nAttempt to initialize the cache more than one time without cart_poweroff!\n");
		return(-1);			
	}

	// If frame memory is not NULL, cache existed before
	if(cacheMemory != NULL){
		logMessage(LOG_ERROR_LEVEL, "\ncacheMemory is not NULL and might have to change cache size dynamically\n");
		return(-1);		
	}

	// Allocate enough space to hold frame memory
	cacheMemory = calloc(cacheSize, sizeof(CacheTable));

	// Zero out data members of cache memory structure anyway
	for(i = 0; i < cacheSize; i++){
		strncpy(cacheMemory[i].cachedFrame, "", sizeof(CartFrame));
		cacheMemory[i].cacheHandle	=		0;
		cacheMemory[i].cartIndex	=		0;
		cacheMemory[i].frameIndex	=		0;
		cacheMemory[i].isUsed		=		NO;		
		cacheMemory[i].lru			=		-1;
	}

	// Set cache initialized flag to YES
	cacheInit = YES;

	// Return successfully
	return (0);
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : close_cart_cache
// Description  : Clear all of the contents of the cache, cleanup
//
// Inputs       : none
// Outputs      : o if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int close_cart_cache(void) {

	// Local Variables;
	int		i = 0;

	// Zero out data members anyway
	for(i = 0; i < cacheSize; i++){
		strncpy(cacheMemory[i].cachedFrame, "", sizeof(CartFrame));
		cacheMemory[i].cacheHandle = 0;
		cacheMemory[i].cartIndex = 0;
		cacheMemory[i].frameIndex = 0;
		cacheMemory[i].isUsed = NO;
		cacheMemory[i].lru = -1;
	}

	// Free the heap memory
	free(cacheMemory);
	cacheMemory = NULL;

	// Check to make sure we successfully free'd the heap data
	if(cacheMemory != NULL){
		logMessage(LOG_ERROR_LEVEL, "\ncacheMemory is not NULL when it should have been free'd in close_cart_cache !\n");
		return(-1);			
	}

	// Return successfully
	return (0);
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : put_cart_cache
// Description  : Put an object into the frame cache
//
// Inputs       : cart - the cartridge number of the frame to cache
//                frm - the frame number of the frame to cache
//                buf - the buffer to insert into the cache
// Outputs      : 0 if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int put_cart_cache(CartridgeIndex cart, CartFrameIndex frm, void *buf) {

	// Local Variables
	int			i = 0;
	int			*resp;
	CacheTable	temp;
	Flag		completed = NO;

	// Always update lruCounter with every cache access
	lruCounter++;

	// Create the cache tag for requested cart and frame coupling in CART system
	temp.cacheHandle = create_cache_tag(cart, frm);

	// Scan cache for space, make space for incoming frame according to cache policy if necessary
	while (completed == NO) {
	search:
		// Search to see if space exists in the cache to write to
		for (i = 0; i < cacheSize; i++) {
			// Found a frame in the cache that is not in use so put it here
			if (cacheMemory[i].isUsed == NO) {
				strncpy(cacheMemory[i].cachedFrame, (char *)buf, sizeof(CartFrame));
				cacheMemory[i].cacheHandle	= temp.cacheHandle;
				cacheMemory[i].cartIndex	= cart;
				cacheMemory[i].frameIndex	= frm;
				cacheMemory[i].isUsed		= YES;
				cacheMemory[i].lru			= lruCounter;
				goto finished;
			}
		}

		if (cacheSize == 0)
			return (0);

		// No space exists so eject the LRU entry
		temp.lru = cacheMemory[0].lru;
		temp.cartIndex = cacheMemory[0].cartIndex;
		temp.frameIndex = cacheMemory[0].frameIndex;

		// Search if an entry in the cache is less frequently used before ejecting
		for (i = 0; i < cacheSize; i++) {
			if (cacheMemory[i].lru < temp.lru) {
				temp.lru = cacheMemory[i].lru;
				temp.cartIndex = cacheMemory[i].cartIndex;
				temp.frameIndex = cacheMemory[i].frameIndex;
			}
		}

		// Eject LRU entry in cache to make room for new frame
		resp = (int *)delete_cart_cache(temp.cartIndex, temp.frameIndex);
		if (resp == NULL)
			return (-1);

		// Put new frame entry into cache with newest lruCounter
		goto search;
	
	finished:	
		logMessage(LOG_INFO_LEVEL, "\nSuccessfully completed cache placement in put_cart_cache\n");
		completed = YES;
	}

	// Return successfully
	return (0);
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : get_cart_cache
// Description  : Get an frame from the cache (and return it)
//
// Inputs       : cart - the cartridge number of the cartridge to find
//                frm - the  number of the frame to find
// Outputs      : pointer to cached frame or NULL if not found
//
////////////////////////////////////////////////////////////////////////////////
void * get_cart_cache(CartridgeIndex cart, CartFrameIndex frm) {

	// Local Variables
	int			i = 0;
	uint32_t	temp = 0;

	// Always update lruCounter with every cache access
	lruCounter++;

	// Create the cache tag for requested cart and frame coupling in CART system
	temp = create_cache_tag(cart, frm);

	// Search to see if it exists in the cache
	for (i = 0; i < cacheSize; i++) {
		// Requested frame exists in cache
		if (cacheMemory[i].cacheHandle == temp) {
			cacheMemory[i].lru = lruCounter;		// update the frame's lruCounter to newest
			return (cacheMemory[i].cachedFrame);	// return a pointer to the cached frame
		}
	}

	// Does not exist in cache if this statement is reached
	return (NULL);
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : delete_cart_cache
// Description  : Remove a frame from the cache (and return it)
//
// Inputs       : cart - the cart number of the frame to remove from cache
//                blk - the frame number of the frame to remove from cache
// Outputs      : pointe buffer inserted into the object
//
////////////////////////////////////////////////////////////////////////////////
void * delete_cart_cache(CartridgeIndex cart, CartFrameIndex blk) {

	// Local Variables
	int			i = 0;
	CacheTable	temp;

	// Always update lruCounter with every cache access
	lruCounter++;

	// Create the cache tag for requested cart and frame coupling in CART system
	temp.cacheHandle = create_cache_tag(cart, blk);

	// Check to make sure requested cart and frame is valid and in the cache
	for (i = 0; i < cacheSize; i++) {
		// Requested frame exists in cache so wipe it
		if (cacheMemory[i].cacheHandle == temp.cacheHandle) {

			memcpy(last_cached_frame, cacheMemory[i].cachedFrame, CART_FRAME_SIZE);

			strncpy(cacheMemory[i].cachedFrame, "", sizeof(CartFrame));
			cacheMemory[i].cacheHandle	= 0;
			cacheMemory[i].cartIndex	= 0;
			cacheMemory[i].frameIndex	= 0;
			cacheMemory[i].isUsed		= NO;
			cacheMemory[i].lru			= -1;

			// Return successfully the deleted frame if needed
			return (last_cached_frame);
		}
	}

	// Cart and frame not found in the cache
	logMessage(LOG_ERROR_LEVEL, "\nError: bad cart/frame passed to delete_cart_cache : not found in cache\n");
	return (NULL);
}



//
// Unit test
//

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cartCacheUnitTest
// Description  : Run a UNIT test checking the cache implementation
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure
//
////////////////////////////////////////////////////////////////////////////////
int cartCacheUnitTest(void) {


	// Return successfully
	logMessage(LOG_OUTPUT_LEVEL, "Cache unit test completed successfully.");
	return(0);
}

//
// My functions
//

uint32_t create_cache_tag(CartridgeIndex cart, CartFrameIndex frame) {
	uint32_t cacheTag;
	uint32_t theCart = (uint32_t)cart;
	uint32_t theFrame = (uint32_t)frame;

	//  theCart    theFrame    
	//  [31:16]     [15:0]

	theCart = (theCart << 16);
	cacheTag = (theCart | theFrame);

	return (cacheTag);
}