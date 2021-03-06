
#include <limits.h>
#include "common.h"
#include "dal_internals.h"

#define DEBUG

/* Keep C++ compilers from getting confused */
#if defined(__cplusplus)
extern "C" {
#endif

//efficiently computes the logarithm base 2 of a positive integer
inline int _log2 ( unsigned int n )
{
    unsigned int toRet = 0;
    int m = n - 1;
    while (m > 0) {
		m >>= 1;
		toRet++;
    }
    return toRet;
}
inline int _pow2( int n ) {
	int i, r = 1;
	for( i = 0; i < n; ++ i ) {
		r *= 2;
	}
	return r;
}

//computes the logarithm base k of a positive integer
inline int _logk ( unsigned int n, unsigned int k )
{
    return _log2 ( n ) / _log2 ( k );
}


//compare two integers
inline int compare (const void * a, const void * b)
{
  return ( *(int*)a - *(int*)b );
}

//returns 1 if x is a power of two, 0 otherwise
inline int isPowerOfTwo( int x )
{
    return x > 0 && (x & (x - 1)) == 0;
}














/***************************************************************************************************************/
/******************************************* Sequential Sort ***************************************************/
/***************************************************************************************************************/

typedef struct {
	int val;
	int run_index;
} Min_val;
typedef struct {
	int size;
	Min_val *elements;

} Heap;
void HeapInit( Heap *h, int maxElements ) {
	h->elements = (Min_val*)malloc( (maxElements+1) * sizeof(Min_val) );
	h->size = 0;
	h->elements[0].val = -INT_MAX;
}
void HeapDestroy( Heap *h ) {
	free( h->elements );
}
void HeapPush( Heap *h, int val, int index ) {
	int i;
	for ( i=++h->size; h->elements[i/2].val > val; i/=2)
        h->elements[i] = h->elements[i/2];
    h->elements[i].val = val;
	h->elements[i].run_index = index;
}
Min_val HeapTop( Heap *h ) {
	return h->elements[1];
}
void HeapPop( Heap *h ) {
	if ( ! h->size )
		return;
	Min_val *min = h->elements+1;
	Min_val *last = h->elements+(h->size--);
	int i, child;

	for ( i=1; i*2 <= h->size; i=child ) {
		child = i * 2;

		if ( child < h->size && h->elements[child+1].val < h->elements[child].val )
			child++;
		if ( last->val > h->elements[child].val )
			h->elements[i] = h->elements[child];
		else
			break;
	}
	h->elements[i] = *last;
}

#define MIN(a,b) ( (a)<(b) ? (a) : (b) )
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )

void fileKMerge( Data *run_devices, const int k, Data *output_device )
{
	SPD_ASSERT( output_device->medium == File || output_device->medium == Array, "output_device must be pre-allocated!" );

	/* Memory buffer */
	Data buffer;
	DAL_init( &buffer );
	DAL_allocBuffer( &buffer, DAL_allowedBufSize() );
	const dal_size_t bufferedRunSize = DAL_dataSize(&buffer) / (k + 1); //Size of a single buffered run (+1 because of the output buffer)

	/* TODO: Handle this case */
	SPD_ASSERT( bufferedRunSize > 0 , "fileKMerge function doesn't allow a number of runs greater than memory buffer size" );

	/* Runs Buffer */
	int* runs = buffer.array.data;

	/* Output buffer */
	int* output = buffer.array.data+k*bufferedRunSize;

	/* Indexes and Offsets for the k buffered runs */
	dal_size_t *run_indexes = (dal_size_t*) calloc( sizeof(dal_size_t), k );
	dal_size_t *run_offsets = (dal_size_t*) malloc( k * sizeof(dal_size_t) );
	dal_size_t *run_buf_sizes = (dal_size_t*) malloc( k * sizeof(dal_size_t) );

	/* The auxiliary heap struct */
	Heap heap;
	HeapInit( &heap, k );

	dal_size_t j;
	/* Initializing the buffered runs and the heap */
	for ( j=0; j < k; j++ ) {
		run_buf_sizes[j] = DAL_dataCopyOS( &run_devices[j], 0, &buffer, j*bufferedRunSize, MIN(bufferedRunSize, DAL_dataSize(&run_devices[j])) );
		run_offsets[j] = run_buf_sizes[j];

		HeapPush( &heap, runs[j*bufferedRunSize], j );
	}

	/* Merging the runs */
	dal_size_t outputSize = 0;
	dal_size_t outputOffset = 0;
	dal_size_t i;
    for ( i=0; i<DAL_dataSize(output_device); i++ ) {
        Min_val min = HeapTop( &heap );
        HeapPop( &heap );

		//the run index
		j = min.run_index;
		dal_size_t remainingSize = DAL_dataSize(&run_devices[j])-run_offsets[j];

        if ( ++(run_indexes[j]) <  run_buf_sizes[j] )							//If there are others elements in the buffered run
			HeapPush( &heap, runs[j*bufferedRunSize+run_indexes[j]], j );		//pushes a new element in the heap
		else if ( remainingSize > 0 ) {											//else, if the run has not been read completely
			run_buf_sizes[j] = DAL_dataCopyOS( &run_devices[j], run_offsets[j], &buffer, j*bufferedRunSize, MIN(remainingSize, bufferedRunSize) );
			run_offsets[j] += run_buf_sizes[j];
			run_indexes[j] = 0;
			HeapPush( &heap, runs[j*bufferedRunSize], j );
		}

        output[outputSize++] = min.val;

		if ( outputSize == bufferedRunSize || i==DAL_dataSize(output_device)-1 ) {					//If the output buffer is full
			outputOffset += DAL_dataCopyOS( &buffer, k*bufferedRunSize, output_device, outputOffset, outputSize );
			outputSize = 0;
		}
    }
	/* Freeing memory */
	HeapDestroy( &heap );
    DAL_destroy( &buffer );
	free( run_indexes );
	free( run_offsets );
	free( run_buf_sizes );
}

void destroyRuns( Data *run_devices, int k )
{
	int i;
	for ( i=0; i<k; i++ )
		DAL_destroy( &run_devices[i] );
}

void fileSort( Data *data )
{
	const dal_size_t dataSize = DAL_dataSize( data );

	if ( dataSize < 2 )
		return;

	/* Memory buffer */
	Data buffer;
	DAL_init( &buffer );
 	SPD_ASSERT( DAL_allocBuffer( &buffer, dataSize ), "not enough memory..." );

	const dal_size_t runSize = DAL_dataSize( &buffer );				//Single run size
	const int k = dataSize / runSize + (dataSize % runSize > 0);	//Number of run_

	if ( k < 2 ) {
		DAL_dataCopy( data, &buffer);
		qsort( buffer.array.data, dataSize, sizeof(int), compare );
		DAL_dataCopy( &buffer, data );
		DAL_destroy( &buffer );
	}
	else {
		/* Data that will contain temporary runs */
		Data run_devices[k];
		dal_size_t readSize = 0;
		dal_size_t count = 0;
		dal_size_t i;
		/* Sorting single runs */
		for( i=0; i<k; i++ ) {
			count = MIN( runSize, dataSize-i*runSize );

			readSize = DAL_dataCopyOS( data, i*runSize, &buffer, 0, count );

			DAL_init( &run_devices[i] );
			SPD_ASSERT( DAL_allocFile( &run_devices[i], readSize ), "couldn't create a temporary file for the "DST"-th run", i );

			qsort( buffer.array.data, readSize, sizeof(int), compare );

			DAL_dataCopyOS( &buffer, 0, &run_devices[i], 0, readSize );
		}
		DAL_destroy( &buffer );
		fileKMerge( run_devices, k, data );		//k-way merge
		destroyRuns( run_devices, k );
	}
}

/// sequential sort function
void sequentialSort( const TestInfo *ti, Data *data )
{
	switch( data->medium ) {
		case File: {
			fileSort( data );
			break;
		}
		case Array: {
			qsort( data->array.data, DAL_dataSize(data), sizeof(int), compare );
			break;
		}
		default:
			DAL_UNSUPPORTED( data );
	}
}
/***************************************************************************************************************/














/**
* @brief Gets the index of the bucket in which to insert ele
*
* @param[in] ele        The element to be inserted within a bucket
* @param[in] splitters  The array of splitters
* @param[in] length     The number of splitters
*
* @return The index of the bucket in which to insert ele
*/
int getBucketIndex( const int *ele, const int *splitters, const int length )
{
	int low = 0;
	int high = length;
	int cmpResult = 0;
	int mid;

	while ( high > low ) {
		mid = (low + high) >> 1;
		cmpResult = compare( ele, &splitters[mid] );

		if ( cmpResult == 0 )
			return mid;

		if ( cmpResult > 0 )
			low = mid + 1;
		else
			high = mid - 1;
	}
	cmpResult = compare( ele, &splitters[low] );

	if ( cmpResult > 0 && low < length)
		return (low + 1);
	return low;
}

/**
* @brief Chooses n-1 equidistant elements of the input data array as splitters
*
* @param[in] 	array 				Data from which extract splitters
* @param[in] 	length 				Length of the data array
* @param[in] 	n 					Number of parts in which to split data
* @param[out] 	newSplitters	 	Chosen splitters
*/
void chooseSplitters( int *array, const dal_size_t length, const int n, int *newSplitters )
{
	int i, j, k;

	if ( length >= n )
		/* Choosing splitters (n-1 equidistant elements of the data array) */
		for ( i=0, k=j=length/n; i<n-1; i++, k+=j )
			newSplitters[i] = array[k];
	else {
		/* Choosing n-1 random splitters */
		for ( i=0; i<n-1; i++ )
			newSplitters[i] = array[rand()%length];
		qsort( newSplitters, n-1, sizeof(int), compare );
	}
}

/**
* @brief Chooses n-1 equidistant elements of the input data object as splitters
*
* @param[in] 	data 				Data from which extract splitters
* @param[in] 	n 					Number of parts in which to split data
* @param[out] 	newSplitters	 	Chosen splitters
*/
void chooseSplittersFromData( Data *data, const int n, int *newSplitters )
{
	Data stub;
	DAL_init( &stub );
	stub.medium = Array;
	stub.array.size = n-1;
	stub.array.data = newSplitters;

	dal_size_t i, j, k;

	if ( DAL_dataSize(data) >= n )
		/* Choosing splitters (n-1 equidistant elements of the data array) */
		for ( i=0, k=j=DAL_dataSize(data)/n; i<n-1; i++, k+=j )
			DAL_dataCopyO( data, k, &stub, i );
	else {
		/* Choosing n-1 random splitters */
		for ( i=0; i<n-1; i++ )
			DAL_dataCopyO( data, rand()%DAL_dataSize(data), &stub, i );
		qsort( newSplitters, n-1, sizeof(int), compare );
	}

}

/* Keep C++ compilers from getting confused */
#if defined(__cplusplus)
}
#endif
