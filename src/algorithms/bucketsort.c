
/**
* @file bucketsort.c
*
* @brief This file contains a set of functions used to sort atomic elements (integers) by using a parallel version of the Bucket Sort
*
* @author Nicola Desogus
* @version 0.0.01
*/

#include <limits.h>
#include <string.h>
#include <mpi.h>
#include "../sorting.h"
#include "../common.h"
#include "../dal_internals.h"

#ifndef SPD_PIANOSA
	#define MPI_DST MPI_LONG
#else
	#define MPI_DST MPI_LONG_LONG
#endif

#define MIN(a,b) ( (a)<(b) ? (a) : (b) )
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )

/**
* @brief Gets the number of element to be inserted in each small bucket
*
* @param[in] 	data       		The data object containing data to be distributed
* @param[in] 	n     			The number of buckets
* @param[out] 	lengths    		The array that will contain the small bucket lengths
*/
void getSendCounts( Data *data, const int n, dal_size_t *lengths )
{
	dal_size_t i, j, k;
	const int range = ((dal_size_t)INT_MAX+1) / n;	//Range of elements in each bucket

	/* Initializing the lengths array */
	memset( lengths, 0, n*sizeof(dal_size_t) );

	/* Computing the number of integers to be sent to each process */
	switch( data->medium ) {
		case File: {
			/* Memory buffer */
			Data buffer;
			DAL_init( &buffer );
			SPD_ASSERT( DAL_allocBuffer( &buffer, DAL_dataSize(data) ), "not enough memory..." );

			dal_size_t blocks = DAL_BLOCK_COUNT(data, &buffer);
			dal_size_t r, readCount = 0;			

			for( i=0; i<blocks; i++ ) {
				r = DAL_dataCopyOS( data, i*DAL_dataSize(&buffer), &buffer, 0, MIN(DAL_dataSize(&buffer), DAL_dataSize(data)-readCount) );
				readCount += r;

				for ( k=0; k<r; k++ ) {
					j = buffer.array.data[k] / range;
					SPD_ASSERT( j >= 0 && j < n, "Something went wrong: j should be within [0,%d], but it's "DST, n-1, j );
					lengths[j]++;
				}
			}
			SPD_ASSERT( readCount == DAL_dataSize(data), DST" elements have been read, while Data size is "DST, readCount, DAL_dataSize(data) );

			DAL_destroy( &buffer );
			break;
		}
		case Array: {
			for ( i=0; i<data->array.size; i++ ) {
				j = data->array.data[i] / range;
				lengths[j]++;
			}
			break;
		}
		default:
			DAL_UNSUPPORTED( data );
	}
}

/**
* @brief Sorts input data by using a parallel version of Bucket Sort
*
* @param[in] ti        The TestInfo Structure
* @param[in] data      Data to be sorted
*/
void bucketSort( const TestInfo *ti, Data *data )
{
	const int			root = 0;                           //Rank (ID) of the root process
	const int			id = GET_ID( ti );                  //Rank (ID) of the process
	const int			n = GET_N( ti );                    //Number of processes
	const dal_size_t	M = GET_M( ti );                    //Number of data elements
	const dal_size_t	local_M = GET_LOCAL_M( ti );        //Number of elements assigned to each process

	dal_size_t 			sendCounts[n], recvCounts[n];		//Number of elements in send/receive buffers
	dal_size_t			sdispls[n], rdispls[n];				//Send/receive buffer displacements
	dal_size_t			i, j, k;

	PhaseHandle 		scatterP, sortingP, computationP, localP, bucketsP, gatherP;

/***************************************************************************************************************/
/********************************************* Scatter Phase ***************************************************/
/***************************************************************************************************************/
	scatterP = startPhase( ti, "scattering" );

	/* Computing the number of elements to be sent to each process and relative displacements */
	for ( k=0, i=0; i<n; i++ ) {
		sdispls[i] = k;
		sendCounts[i] = M/n + (i < M%n);
		k += sendCounts[i];
	}
	/* Scattering data */
	DAL_scatterv( data, sendCounts, sdispls, root );

	stopPhase( ti, scatterP );
/*--------------------------------------------------------------------------------------------------------------*/

	sortingP = startPhase( ti, "sorting" );
	computationP = startPhase( ti, "computation" );

/***************************************************************************************************************/
/*********************************************** Local Phase ***************************************************/
/***************************************************************************************************************/

	localP = startPhase( ti, "sequential sort" );

	/* Sorting local data */
	sequentialSort( ti, data );

	stopPhase( ti, localP );
/*--------------------------------------------------------------------------------------------------------------*/


/***************************************************************************************************************/
/**************************************** Buckets Construction Phase *******************************************/
/***************************************************************************************************************/

	bucketsP = startPhase( ti, "buckets construction" );

	/* Computing the number of integers to be sent to each process */
	getSendCounts( data, n, sendCounts );

	stopPhase( ti, computationP );
	/* Informing all processes on the number of elements that will receive */
	MPI_Alltoall( sendCounts, 1, MPI_DST, recvCounts, 1, MPI_DST, MPI_COMM_WORLD );

	/* Computing the displacements */
	for ( j=0, k=0, i=0; i<n; i++ ) {
		/* Computing the displacements relative to localData from which to take the outgoing data destined for each process */
		sdispls[i] = k;
		k += sendCounts[i];

		/* Computing the displacements relative to localBucket at which to place the incoming data from each process  */
		rdispls[i] = j;
		j += recvCounts[i];
	}
	/* Sending data to the appropriate processes */
	DAL_alltoallv( data, sendCounts, sdispls, recvCounts, rdispls );
	resumePhase( ti, computationP );

	/* Sorting local bucket */
	sequentialSort( ti, data );

	stopPhase( ti, bucketsP );
/*--------------------------------------------------------------------------------------------------------------*/

	stopPhase( ti, computationP );
	stopPhase( ti, sortingP );

/***************************************************************************************************************/
/********************************************** Ghater Phase ***************************************************/
/***************************************************************************************************************/
	gatherP = startPhase( ti, "gathering" );

	/* Gathering the lengths of the all buckets */
	MPI_Gather( &j, 1, MPI_DST, recvCounts, 1, MPI_DST, root, MPI_COMM_WORLD );

	/* Computing displacements relative to the output array at which to place the incoming data from each process  */
	if ( id == root ) {
		for ( k=0, i=0; i<n; i++ ) {
			rdispls[i] = k;
			k += recvCounts[i];
		}
	}
	/* Gathering sorted data */
	DAL_gatherv( data, recvCounts, rdispls, root );

	stopPhase( ti, gatherP );
/*--------------------------------------------------------------------------------------------------------------*/
}

void mainSort( const TestInfo *ti, Data *data )
{
	bucketSort( ti, data );
}

void sort( const TestInfo *ti )
{
	Data data;
	DAL_init( &data );
	bucketSort( ti, &data );
	DAL_destroy( &data );
}
