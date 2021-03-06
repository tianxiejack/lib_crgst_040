

#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <glew.h>
#include <glut.h>
#include <cuda.h>
#include <cuda_gl_interop.h>
//#include <cuda_runtime.h>
#include "cuda_runtime_api.h"
#include "osa_image_queue.h"

#define IMG_INF_FLAG_PBOMAP	(0x8000)

int cuMap(OSA_BufInfo* info)
{
	OSA_assert(info != NULL && info->memtype == memtype_glpbo);
	{
		OSA_assert(info->resource != NULL);
		OSA_assert((info->flags & IMG_INF_FLAG_PBOMAP) == 0);
		cudaError_t ret;
		size_t size;
		ret = cudaGraphicsMapResources(1, (struct cudaGraphicsResource **)&info->resource, 0);
		if (ret != cudaSuccess) {
				fprintf(stderr, "cudaGraphicsMapResources failed line[%d] errno = %d!", __LINE__, (int)cudaGetLastError());
				OSA_assert(0);
		}
		ret = cudaGraphicsResourceGetMappedPointer(&info->physAddr, &size, (struct cudaGraphicsResource *)info->resource);
		if (ret != cudaSuccess) {
				fprintf(stderr, "cudaGraphicsResourceGetMappedPointer failed line[%d] errno = %d!", __LINE__, (int)cudaGetLastError());
				OSA_assert(0);
		}
		OSA_assert(info->size == size);
		info->flags |= IMG_INF_FLAG_PBOMAP;
	}
	return OSA_SOK;
}

int cuUnmap(OSA_BufInfo* info)
{
	OSA_assert(info != NULL && info->memtype == memtype_glpbo);
	{
		OSA_assert(info->resource != NULL);
		OSA_assert((info->flags & IMG_INF_FLAG_PBOMAP) == IMG_INF_FLAG_PBOMAP);
		cudaError_t ret;
		ret = cudaGraphicsUnmapResources(1, (struct cudaGraphicsResource **)&info->resource, 0);
		if (ret != cudaSuccess) {
				fprintf(stderr, "cudaGraphicsUnmapResources failed line[%d] errno = %d!", __LINE__, (int)cudaGetLastError());
				OSA_assert(0);
		}
		info->physAddr = NULL;
		info->flags &= ~IMG_INF_FLAG_PBOMAP;
	}

	return OSA_SOK;
}

int image_queue_create(OSA_BufHndl *hndl, int nBuffers, size_t buffsize, int memtype)
{
	int ret = OSA_SOK;
	cudaError_t custat;
	int i;
	unsigned int page_size=getpagesize();
	OSA_BufCreate bufInit;
	GLuint pbos[OSA_BUF_NUM_MAX] = {0,};

	if(memtype == memtype_default)
		memtype = memtype_cudev;

	memset(&bufInit, 0, sizeof(bufInit));
	memset(pbos, 0, sizeof(pbos));
	bufInit.numBuf = nBuffers;
	for(i=0; i<bufInit.numBuf; i++)
	{
		switch(memtype)
		{
		case memtype_normal:
			bufInit.bufVirtAddr[i] = memalign(page_size,buffsize);
			OSA_assert(bufInit.bufVirtAddr[i] != NULL);
			break;
		case memtype_cuhost:
			custat = cudaHostAlloc(&bufInit.bufVirtAddr[i], buffsize, cudaHostAllocDefault);
			OSA_assert(custat == cudaSuccess && bufInit.bufVirtAddr[i]!= NULL);
			break;
		case memtype_cudev:
			custat = cudaMalloc(&bufInit.bufPhysAddr[i], buffsize);
			OSA_assert(custat == cudaSuccess && bufInit.bufPhysAddr[i]!= NULL);
			break;
		case memtype_cumap:
			custat = cudaMallocManaged (&bufInit.bufPhysAddr[i], buffsize, cudaMemAttachGlobal);
			OSA_assert(custat == cudaSuccess && bufInit.bufPhysAddr[i]!= NULL);
			bufInit.bufVirtAddr[i] = bufInit.bufPhysAddr[i];
			break;
		case memtype_glpbo:
		{
			glGenBuffers(1, &pbos[i]);
			OSA_assert(pbos[i] != 0);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[i]);
			glBufferData(GL_PIXEL_UNPACK_BUFFER, buffsize, NULL, GL_DYNAMIC_COPY);//GL_STATIC_DRAW);//GL_DYNAMIC_DRAW);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}
			break;
		case memtype_null:
			bufInit.bufVirtAddr[i] = malloc(buffsize);
			OSA_assert(bufInit.bufVirtAddr[i] != NULL);
			break;
		default:
			break;
		}
	}

	ret = OSA_bufCreate(hndl, &bufInit);
	OSA_assert(ret == OSA_SOK);

	for(i=0; i<bufInit.numBuf; i++)
	{
		hndl->bufInfo[i].size = buffsize;
		hndl->bufInfo[i].bufferId = i;
		hndl->bufInfo[i].memtype = memtype;
		hndl->bufInfo[i].pbo = pbos[i];
		if(memtype == memtype_glpbo){
			ret = cudaGraphicsGLRegisterBuffer((struct cudaGraphicsResource **)&hndl->bufInfo[i].resource, hndl->bufInfo[i].pbo, cudaGraphicsMapFlagsWriteDiscard);
			image_queue_getEmpty(hndl);
		}
		OSA_assert(ret == cudaSuccess);
	}

	if(memtype == memtype_glpbo)
		hndl->bMap = true;

	return ret;
}
int image_queue_delete(OSA_BufHndl *hndl)
{
	int ret = OSA_SOK;
	OSA_assert(hndl != NULL);

	for(int i=0; i<hndl->numBuf; i++)
	{
		switch(hndl->bufInfo[i].memtype)
		{
		case memtype_normal:
			free(hndl->bufInfo[i].virtAddr);
			break;
		case memtype_cuhost:
			cudaFreeHost(hndl->bufInfo[i].virtAddr);
			break;
		case memtype_cudev:
		case memtype_cumap:
			cudaFree(hndl->bufInfo[i].physAddr);
			break;
		case memtype_glpbo:
			cudaGraphicsUnregisterResource((struct cudaGraphicsResource *)hndl->bufInfo[i].resource);
			glDeleteBuffers(1, &hndl->bufInfo[i].pbo);
			break;
		case memtype_null:
		default:
			break;
		}
		hndl->bufInfo[i].virtAddr = hndl->bufInfo[i].physAddr = NULL;
	}

	ret = OSA_bufDelete(hndl);

	return ret;
}
OSA_BufInfo* image_queue_getEmpty(OSA_BufHndl *hndl)
{
	OSA_BufInfo* info = NULL;
	int bufId = -1;

	int ret = OSA_bufGetEmpty(hndl, &bufId, OSA_TIMEOUT_NONE);

	if(ret == OSA_SOK && bufId != -1){
		info = OSA_bufGetBufInfo(hndl, bufId);
	}

	return info;
}
int image_queue_putFull(OSA_BufHndl *hndl, OSA_BufInfo* info)
{
	OSA_assert(info != NULL);

	return OSA_bufPutFull(hndl, info->bufferId);
}
OSA_BufInfo* image_queue_getFull(OSA_BufHndl *hndl)
{
	OSA_BufInfo* info = NULL;
	int bufId = -1;

	int ret = OSA_bufGetFull(hndl, &bufId, OSA_TIMEOUT_NONE);

	if(ret == OSA_SOK && bufId != -1){
		info = OSA_bufGetBufInfo(hndl, bufId);
	}

	return info;
}
int image_queue_putEmpty(OSA_BufHndl *hndl, OSA_BufInfo* info)
{
	OSA_assert(info != NULL);

	return OSA_bufPutEmpty(hndl, info->bufferId);
}

OSA_BufInfo* image_queue_peekFull(OSA_BufHndl *hndl)
{
	Int32 bufId;
	OSA_BufInfo *info = NULL;
	int iret = OSA_quePeek(&hndl->fullQue, &bufId);

	if(iret == OSA_SOK){
		info = &hndl->bufInfo[bufId];
	}

	return info;
}

OSA_BufInfo* image_queue_peekEmpty(OSA_BufHndl *hndl)
{
	Int32 bufId;
	OSA_BufInfo *info = NULL;
	int iret = OSA_quePeek(&hndl->emptyQue, &bufId);

	if(iret == OSA_SOK){
		info = &hndl->bufInfo[bufId];
	}

	return info;
}

int image_queue_switchEmpty(OSA_BufHndl *hndl)
{
	int bufId;
	int ret;

	ret = OSA_bufSwitchEmpty(hndl, &bufId);

	return ret;
}

int image_queue_switchFull(OSA_BufHndl *hndl)
{
	int bufId;
	int ret;

	ret = OSA_bufSwitchFull(hndl, &bufId);

	return ret;
}

int image_queue_emptyCount(OSA_BufHndl *hndl)
{
	return OSA_bufGetEmptyCount(hndl);
}

int image_queue_fullCount(OSA_BufHndl *hndl)
{
	return OSA_bufGetFullCount(hndl);
}

