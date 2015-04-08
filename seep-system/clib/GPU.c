#include "GPU.h"

/* #include "uk_ac_imperial_lsds_streamsql_op_gpu_TheGPU.h" */
#include "uk_ac_imperial_lsds_seep_multi_TheGPU.h"
#include <jni.h>

#include "utils.h"
#include "debug.h"

#include "gpuquery.h"
#include "openclerrorcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <CL/cl.h>

#include <unistd.h>
#include <sched.h>

/* Lock memory pages */
#include <sys/mman.h>
#include <errno.h>

static cl_platform_id platform = NULL;
static cl_device_id device = NULL;
static cl_context context = NULL;

static int Q; /* Number of queries */
static int freeIndex;
static gpuQueryP queries [MAX_QUERIES];

static int poolIndex;
static outputBufferP outputs [BUFFER_POOL];

void callback_setKernelDummy     (cl_kernel, gpuContextP, int *);
void callback_setKernelProject   (cl_kernel, gpuContextP, int *);
void callback_setKernelReduce    (cl_kernel, gpuContextP, int *);
void callback_setKernelSelect    (cl_kernel, gpuContextP, int *);
void callback_setKernelCompact   (cl_kernel, gpuContextP, int *);
void callback_setKernelAggrScan  (cl_kernel, gpuContextP, int *);
void callback_setKernelAggrMerge (cl_kernel, gpuContextP, int *);
void callback_setKernelAggregate (cl_kernel, gpuContextP, int *);

void callback_writeInput (gpuContextP, JNIEnv *, jobject, int, int, int);
void callback_readOutput (gpuContextP, JNIEnv *, jobject, int, int, int);

static void setPlatform () {
	int error = 0;
	cl_uint count = 0;
	error = clGetPlatformIDs (1, &platform, &count);
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
		exit (1);
	}
	dbg("Obtained 1/%u platforms available\n", count);
	return;
}

static void setDevice () {
	int error = 0;
	cl_uint count = 0;
	error = clGetDeviceIDs (platform, CL_DEVICE_TYPE_GPU, 1, &device, &count);
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
		exit (1);
	}
	dbg("Obtained 1/%u devices available\n", count);
	return;
}

static void setContext () {
	int error = 0;
	context = clCreateContext (
		0,
		1,
		&device,
		NULL,
		NULL,
		&error);
	if (! context) {
		fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
		exit (1);
	}
	return ;
}

void gpu_init (int _queries) { /* Initialise `n` queries */
	setPlatform ();
	setDevice ();
	setContext ();
	Q = _queries; /* Number of queries */
	freeIndex = 0;
	int i;
	for (i = 0; i < MAX_QUERIES; i++)
		queries[i] = NULL;
	/* Setup output buffer pool */
	poolIndex = 0;
	for (i = 0; i < BUFFER_POOL; i++)
		outputs[i] = NULL;
	return;
}

void gpu_free () {
	int i;
	for (i = 0; i < MAX_QUERIES; i++)
		if (queries[i])
			gpu_query_free (queries[i]);
	if (context)
		clReleaseContext (context);
	return;
}

int gpu_getQuery (const char *source, int _kernels, int _inputs, int _outputs, JNIEnv *env) {
	int ndx = freeIndex++;
	if (ndx < 0 || ndx >= Q)
		return -1;
	queries[ndx] = gpu_query_new (device, context,
			source, _kernels, _inputs, _outputs);

	gpu_query_init (queries[ndx], env, ndx);

	fprintf(stderr, "[GPU] _getQuery returns %d (%d/%d)\n", ndx, freeIndex, Q);
	return ndx;
}

int gpu_setInput  (int qid, int ndx, void *buffer, int size) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_setInput (p, ndx, buffer, size);
}

int gpu_setOutput (int qid, int ndx, void *buffer, int size, int writeOnly) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_setOutput (p, ndx, buffer, size, writeOnly);
}

outputBufferP gpu_getOutput (int ndx, int size) {
	if (ndx < 0 || ndx >= BUFFER_POOL)
		return NULL;
	outputs[ndx] = pinOutputBuffer (context, size);
	return outputs[ndx];
}

int gpu_setKernel (int qid, int ndx, const char *name,
		void (*callback)(cl_kernel, gpuContextP, int *), int *args) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_setKernel (p, ndx, name, callback, args);
}

int gpu_exec (int qid, size_t threads, size_t threadsPerGroup,
		queryOperatorP operator, JNIEnv *env, jobject obj) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_exec (p, threads, threadsPerGroup, operator, env, obj, qid);
}

int gpu_custom_exec (int qid, size_t threads, size_t threadsPerGroup,
		queryOperatorP operator, JNIEnv *env, jobject obj, size_t _threads, size_t _threadsPerGroup) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	// printf("[DBG] In gpu_custom_exec...\n");
	return gpu_query_custom_exec (p, threads, threadsPerGroup, operator, env, obj, qid, _threads, _threadsPerGroup);
}

int gpu_testOverlap (int qid, size_t threads, size_t threadsPerGroup,
		queryOperatorP operator, JNIEnv *env, jobject obj) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_testOverlap (p, threads, threadsPerGroup, operator, env, obj, qid);
}

int gpu_testJNIDataMovement (int qid, queryOperatorP operator, JNIEnv *env, jobject obj) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_testJNIDataMovement (p, operator, env, obj, qid);
}

int gpu_testDataMovement (int qid, queryOperatorP operator, JNIEnv *env, jobject obj) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_testDataMovement (p, operator, env, obj, qid);
}

int gpu_copyInputBuffers (int qid, queryOperatorP operator, JNIEnv *env, jobject obj) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_copyInputBuffers (p, operator, env, obj, qid);
}

int gpu_copyOutputBuffers (int qid, queryOperatorP operator, JNIEnv *env, jobject obj) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_copyOutputBuffers (p, operator, env, obj, qid);
}

int gpu_moveInputBuffers (int qid, queryOperatorP operator, JNIEnv *env, jobject obj) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_moveInputBuffers (p, operator, env, obj, qid);
}

int gpu_moveOutputBuffers (int qid, queryOperatorP operator, JNIEnv *env, jobject obj) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_moveOutputBuffers (p, operator, env, obj, qid);
}

int gpu_moveInputAndOutputBuffers (int qid, queryOperatorP operator, JNIEnv *env, jobject obj) {
	if (qid < 0 || qid >= Q)
		return -1;
	gpuQueryP p = queries[qid];
	return gpu_query_moveInputAndOutputBuffers (p, operator, env, obj, qid);
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_init
(JNIEnv *env, jobject obj, jint N) {

	(void) env;
	(void) obj;

	gpu_init (N);

	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_getQuery
(JNIEnv *env, jobject obj, jstring source, jint _kernels, jint _inputs, jint _outputs) {

	(void) obj;

	const char *_source = (*env)->GetStringUTFChars (env, source, NULL);
	int qid = gpu_getQuery (_source, _kernels, _inputs, _outputs, env);
	(*env)->ReleaseStringUTFChars (env, source, _source);

	return qid;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setInput__III
(JNIEnv *env, jobject obj, jint qid, jint ndx, jint size) {

	(void) env;
	(void) obj;
	
	return gpu_setInput (qid, ndx, NULL, size);
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setInput__IILjava_nio_ByteBuffer_2I
(JNIEnv *env, jobject obj, jint qid, jint ndx, jobject byteBuffer, jint size) {
	
	(void) obj;
	
	void *buffer = (void *) (*env)->GetDirectBufferAddress(env, byteBuffer);
	if (mlock(buffer + 4080, size) != 0) {
		fprintf(stderr, "error: failed to pin input buffer (qid %d ndx %d)\n", qid, ndx);
		int e = errno;
		if (e == EAGAIN) { fprintf(stderr, "EAGAIN\n");
		} else
		if (e == ENOMEM) { fprintf(stderr, "ENOMEM\n");
		} else
		if (e == EPERM)  { fprintf(stderr,  "EPERM\n");
		} else
		if (e == EINVAL) { fprintf(stderr, "EINVAL\n");
		}
		exit (1);
	}
	/* Array in now pinned */
	return gpu_setInput (qid, ndx, buffer + 4080, size);
}

/*
JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setInput__II_3BI
(JNIEnv *env, jobject obj, jint qid, jint ndx, jbyteArray array, jint size) {
	(void) obj;
	jboolean isCopy;
	jbyte *buffer = (jbyte *) (*env)->GetPrimitiveArrayCritical(env, array, &isCopy);
	if (isCopy) {
		fprintf(stderr, "warning: input buffer is a copy\n");
		fflush(stderr);
	}
	return gpu_setInput (qid, ndx, buffer, size);
}
*/

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setOutput__IIII
(JNIEnv *env, jobject obj, jint qid, jint ndx, jint size, jint writeOnly) {

	(void) env;
	(void) obj;

	return gpu_setOutput (qid, ndx, NULL, size, writeOnly);
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setOutput__IILjava_nio_ByteBuffer_2II
(JNIEnv *env, jobject obj, jint qid, jint ndx, jobject byteBuffer, jint size, jint writeOnly) {

	(void) obj;

	void *buffer = (void *) (*env)->GetDirectBufferAddress(env, byteBuffer);
	if (mlock(buffer + 4080, size) != 0) {
		fprintf(stderr, "error: failed to pin output buffer (qid %d ndx %d)\n", qid, ndx);
		int e = errno;
		if (e == EAGAIN) { fprintf(stderr, "EAGAIN\n");
		} else
		if (e == ENOMEM) { fprintf(stderr, "ENOMEM\n");
		} else
		if (e == EPERM)  { fprintf(stderr,  "EPERM\n");
		} else
		if (e == EINVAL) { fprintf(stderr, "EINVAL\n");
		}
		exit (1);
	}
	/* Array in now pinned */
	return gpu_setOutput (qid, ndx, buffer + 4080, size, writeOnly);
}

JNIEXPORT jlong JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_getBufferAddress__II
  (JNIEnv *env, jobject obj, jint ndx, jint size) {

	(void) env;
	(void) obj;

	outputBufferP b = gpu_getOutput (ndx, size);

	return ((unsigned long) b->pinned_buffer);
}

JNIEXPORT jlong JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_getBufferAddress__III
  (JNIEnv *env, jobject obj, jint qid, jint ndx, jint size) {

	(void) env;
	(void) obj;
	(void) qid;
	(void) ndx;

	(void) size;

	return -1;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_free
(JNIEnv *env, jobject obj) {

	(void) env;
	(void) obj;

	gpu_free ();

	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_execute
(JNIEnv *env, jobject obj, jint qid, jint threads, jint threadsPerGroup) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_exec (qid, (size_t) threads, (size_t) threadsPerGroup, operator, env, obj);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_executeCustom
(JNIEnv *env, jobject obj, jint qid, jint threads, jint threadsPerGroup, jint _threads, jint _threadsPerGroup) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_custom_exec (qid, (size_t) threads, (size_t) threadsPerGroup, operator, env, obj, (size_t) _threads, (size_t) _threadsPerGroup);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_testOverlap
(JNIEnv *env, jobject obj, jint qid, jint threads, jint threadsPerGroup) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_testOverlap (qid, (size_t) threads, (size_t) threadsPerGroup, operator, env, obj);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_testJNIDataMovement
(JNIEnv *env, jobject obj, jint qid) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_testJNIDataMovement (qid, operator, env, obj);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_testDataMovement
(JNIEnv *env, jobject obj, jint qid) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_testDataMovement (qid, operator, env, obj);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_copyInputBuffers
(JNIEnv *env, jobject obj, jint qid) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_copyInputBuffers (qid, operator, env, obj);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_copyOutputBuffers
(JNIEnv *env, jobject obj, jint qid) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_copyOutputBuffers (qid, operator, env, obj);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_moveInputBuffers
(JNIEnv *env, jobject obj, jint qid) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_moveInputBuffers (qid, operator, env, obj);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_moveOutputBuffers
(JNIEnv *env, jobject obj, jint qid) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_moveOutputBuffers (qid, operator, env, obj);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_moveInputAndOutputBuffers
(JNIEnv *env, jobject obj, jint qid) {

	/* Create operator */
	queryOperatorP operator = (queryOperatorP) malloc (sizeof(query_operator_t));
	if (! operator) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);
	}
	/* Currently, we assume the same execution pattern for all queries */
	operator->writeInput = callback_writeInput;
	operator->readOutput = callback_readOutput;
	operator->execKernel = NULL;
	gpu_moveInputAndOutputBuffers (qid, operator, env, obj);
	/* Free operator */
	if (operator)
		free (operator);
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setKernelDummy
(JNIEnv *env, jobject obj, jint qid, jintArray _args) {

	(void) env;
	(void) obj;

	(void) _args;

	gpu_setKernel (qid, 0, "dummyKernel", &callback_setKernelDummy, NULL);

	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setKernelProject
(JNIEnv *env, jobject obj, jint qid, jintArray _args) {

	(void) obj;

	jsize argc = (*env)->GetArrayLength(env, _args);
	if (argc != 4) /* # projection kernel constants */
		return -1;
	jint *args = (*env)->GetIntArrayElements(env, _args, 0);
	/* Object `int []` pinned */
	gpu_setKernel (qid, 0, "projectKernel", &callback_setKernelProject, args);
	(*env)->ReleaseIntArrayElements(env, _args, args, 0);
	/* Object `int []` released */
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setKernelSelect
(JNIEnv *env, jobject obj, jint qid, jintArray _args) {

	(void) obj;

	jsize argc = (*env)->GetArrayLength(env, _args);
	if (argc != 5) /* # selection kernel constants */
		return -1;
	jint *args = (*env)->GetIntArrayElements(env, _args, 0);
	/* Object `int []` pinned */
	gpu_setKernel (qid, 0,  "selectKernel2",  &callback_setKernelSelect, args);
	gpu_setKernel (qid, 1, "compactKernel2", &callback_setKernelCompact, args);
	(*env)->ReleaseIntArrayElements(env, _args, args, 0);
	/* Object `int []` released */
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setKernelAggregate
(JNIEnv *env, jobject obj, jint qid, jintArray _args) {

	(void) obj;

	jsize argc = (*env)->GetArrayLength(env, _args);
	if (argc != 6) /* # selection kernel constants */
		return -1;
	jint *args = (*env)->GetIntArrayElements(env, _args, 0);
	/* Object `int []` pinned */

	/* Old version of the code
	gpu_setKernel (qid, 0,  "aggregateKernel",  &callback_setKernelAggrScan, args);
	gpu_setKernel (qid, 1, "compactKernel", &callback_setKernelAggrMerge, args);
	*/

	gpu_setKernel (qid, 0,  "aggregateKernel", &callback_setKernelAggregate, args);
	gpu_setKernel (qid, 1,  "scanKernel",      &callback_setKernelAggregate, args);
	gpu_setKernel (qid, 2,  "compactKernel",   &callback_setKernelAggregate, args);

	(*env)->ReleaseIntArrayElements(env, _args, args, 0);
	/* Object `int []` released */
	return 0;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_setKernelReduce
(JNIEnv *env, jobject obj, jint qid, jintArray _args) {

	(void) obj;

	jsize argc = (*env)->GetArrayLength(env, _args);
	if (argc != 3) /* # reduction kernel constants */
		return -1;
	jint *args = (*env)->GetIntArrayElements(env, _args, 0);
	/* Object `int []` pinned */
	gpu_setKernel (qid, 0, "reduceKernel", &callback_setKernelReduce, args);
	(*env)->ReleaseIntArrayElements(env, _args, args, 0);
	/* Object `int []` released */
	return 0;
}

void callback_setKernelDummy (cl_kernel kernel, gpuContextP context, int *constants) {

	(void) constants;

	int error = 0;
	error |= clSetKernelArg (
		kernel,
		0, /* First argument */
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[0]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		1, /* Second argument */
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[0]->device_buffer));
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
		exit (1);
	}
	return;
}

void callback_setKernelProject (cl_kernel kernel, gpuContextP context, int *constants) {
	/*
	 * Projection kernel signature
	 *
	 * __kernel void projectKernel (
	 * const int tuples,
	 * const int bytes,
	 * __global const uchar *input,
	 * __global uchar *output,
	 * __local uchar *_input,
	 * __local uchar *_output
	 * )
	 */

	/* Get all constants */
	int       tuples = constants[0];
	int        bytes = constants[1];
	int  _input_size = constants[2]; /* Local buffer memory sizes */
	int _output_size = constants[3];

	int error = 0;
	/* Set constant arguments */
	error |= clSetKernelArg (kernel, 0, sizeof(int), (void *) &tuples);
	error |= clSetKernelArg (kernel, 1, sizeof(int), (void *)  &bytes);
	/* Set I/O byte buffers */
	error |= clSetKernelArg (
		kernel,
		2, /* 3rd argument */
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[0]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		3, /* 4th argument */
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[0]->device_buffer));
	/* Set local memory */
	error |= clSetKernelArg (kernel, 4, (size_t)  _input_size, (void *) NULL);
	error |= clSetKernelArg (kernel, 5, (size_t) _output_size, (void *) NULL);

	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
		exit (1);
	}
	return;
}

void callback_setKernelReduce (cl_kernel kernel, gpuContextP context, int *constants) {

	/*
	 * Reduction kernel signature
	 *
	 * __kernel void reduce (
	 * const int tuples,
	 * const int bytes,
	 * __global const uchar *input,
	 * __global const int *startPointers,
	 * __global const int *endPointers,
	 * __global uchar *output,
	 * __local float *scratch
	 * )
	 */

	/* Get all constants */
	int         tuples = constants[0];
	int          bytes = constants[1];
	int  _scratch_size = constants[2]; /* Local buffer memory size */

	int error = 0;
	/* Set constant arguments */
	error |= clSetKernelArg (kernel, 0, sizeof(int), (void *) &tuples);
	error |= clSetKernelArg (kernel, 1, sizeof(int), (void *)  &bytes);
	/* Set I/O byte buffers */
	error |= clSetKernelArg (
		kernel,
		2, /* 3rd argument */
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[0]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		3, /* 4th argument */
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[1]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		4, /* 5th argument */
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[2]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		5, /* 6th argument */
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[0]->device_buffer));
	/* Set local memory */
	error |= clSetKernelArg (kernel, 6, (size_t)  _scratch_size, (void *) NULL);

	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
		exit (1);
	}
	return;
}

void callback_setKernelSelect (cl_kernel kernel, gpuContextP context, int *constants) {
	/*
		 * Selection kernel signature
		 *
		 * __kernel void selectKernel (
		 * const int size,
		 * const int tuples,
		 * const int _bundle,
		 * const int bundles,
		 * __global const uchar *input,
		 * __global int *flags,
		 * __global int *offsets,
		 * __global int *groupOffsets
		 * __global uchar *output,
		 * __local int *buffer
		 * )
		 */

		/* Get all constants */
		int        size = constants[0];
		int      tuples = constants[1];
		int     _bundle = constants[2];
		int     bundles = constants[3];
		int buffer_size = constants[4]; /* Local buffer memory size */

		int error = 0;
		/* Set constant arguments */
		error |= clSetKernelArg (kernel, 0, sizeof(int), (void *)    &size);
		error |= clSetKernelArg (kernel, 1, sizeof(int), (void *)  &tuples);
		error |= clSetKernelArg (kernel, 2, sizeof(int), (void *) &_bundle);
		error |= clSetKernelArg (kernel, 3, sizeof(int), (void *) &bundles);
		/* Set I/O byte buffers */
		error |= clSetKernelArg (
			kernel,
			4, /* 5th argument */
			sizeof(cl_mem),
			(void *) &(context->kernelInput.inputs[0]->device_buffer));
		error |= clSetKernelArg (
			kernel,
			5, /* 6th argument */
			sizeof(cl_mem),
			(void *) &(context->kernelOutput.outputs[0]->device_buffer));
		error |= clSetKernelArg (
			kernel,
			6, /* 7th argument */
			sizeof(cl_mem),
			(void *) &(context->kernelOutput.outputs[1]->device_buffer));
		error |= clSetKernelArg (
	                kernel,
	                7, /* 7th argument */
	                sizeof(cl_mem),
	                (void *) &(context->kernelOutput.outputs[2]->device_buffer));
		error |= clSetKernelArg (
			kernel,
			8, /* 8th argument */
			sizeof(cl_mem),
			(void *) &(context->kernelOutput.outputs[3]->device_buffer));
		/* Set local memory */
		error |= clSetKernelArg (kernel, 9, (size_t) buffer_size, (void *) NULL);

		if (error != CL_SUCCESS) {
			fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
			exit (1);
		}
		return;
}

void callback_setKernelCompact (cl_kernel kernel, gpuContextP context, int *constants) {

	/* The configuration of this kernel is identical to the select kernel. */
	callback_setKernelSelect (kernel, context, constants);
}

void callback_setKernelAggrScan (cl_kernel kernel, gpuContextP context, int *constants) {

	/* __kernel void aggregateKernel (
	 * const int tuples,
	 * const int _table_,
	 * const int __stash_x,
	 * const int __stash_y,
	 * const int max_iterations,
	 *
	 * __global const uchar* input,
	 * __global const int* window_ptrs_,
	 * __global const int* _window_ptrs,
	 * __global const int* x,
	 * __global const int* y,
	 *
	 * __global uchar* contents,
	 * __global int* stashed,
	 * __global int* failed,
	 * __global int* attempts,
	 * __global int* indices,
	 * __global int* offsets,
	 * __global uchar* output,
	 * __local int* buffer
	 * );
	 * */

	/* Get all constants */
	int         tuples = constants[0];
	int        _table_ = constants[1];
	int      __stash_x = constants[2];
	int      __stash_y = constants[3];
	int max_iterations = constants[4];
	int   _buffer_size = constants[5];

	int error = 0;
	/* Set constant arguments */
	error |= clSetKernelArg (kernel, 0, sizeof(int), (void *)          &tuples);
	error |= clSetKernelArg (kernel, 1, sizeof(int), (void *)         &_table_);
	error |= clSetKernelArg (kernel, 2, sizeof(int), (void *)       &__stash_x);
	error |= clSetKernelArg (kernel, 3, sizeof(int), (void *)       &__stash_y);
	error |= clSetKernelArg (kernel, 4, sizeof(int), (void *)  &max_iterations);
	/* Set I/O byte buffers */
	error |= clSetKernelArg (
		kernel,
		5,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[0]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		6,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[1]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		7,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[2]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		8,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[3]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		9,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[4]->device_buffer));

	error |= clSetKernelArg (
		kernel,
		10,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[0]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		11,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[1]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		12,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[2]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		13,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[3]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		14,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[4]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		15,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[5]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		16,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[6]->device_buffer));

	/* Set local memory */
	error |= clSetKernelArg (kernel, 17, (size_t) _buffer_size, (void *) NULL);

	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
		exit (1);
	}
	return;
}

void callback_setKernelAggrMerge (cl_kernel kernel, gpuContextP context, int *constants) {
	int _table_ = constants[1];
	int error = 0;
	/* Set constant arguments */
	error |= clSetKernelArg (kernel, 0, sizeof(int), (void *) &_table_);
	error |= clSetKernelArg (
		kernel,
		1,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[0]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		2,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[4]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		3,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[5]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		4,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[6]->device_buffer));
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
		exit (1);
	}
	return;
}

void callback_setKernelAggregate (cl_kernel kernel, gpuContextP context, int *constants) {

	/* __kernel void aggregateKernel (
	 * const int tuples,
	 * const int _table_,
	 * const int __stash_x,
	 * const int __stash_y,
	 * const int max_iterations,
	 *
	 * __global const uchar* input,
	 * __global const int* window_ptrs_,
	 * __global const int* _window_ptrs,
	 * __global const int* x,
	 * __global const int* y,
	 *
	 * __global uchar* contents,
	 * __global int* stashed,
	 * __global int* failed,
	 * __global int* attempts,
	 * __global int* indices,
	 * __global int* offsets,
	 * __global int* part,
	 * __global uchar* output,
	 * __local int* x
	 * );
	 * */

	/* Get all constants */
	int         tuples = constants[0];
	int        _table_ = constants[1];
	int      __stash_x = constants[2];
	int      __stash_y = constants[3];
	int max_iterations = constants[4];
	int   _buffer_size = constants[5];

	int error = 0;
	/* Set constant arguments */
	error |= clSetKernelArg (kernel, 0, sizeof(int), (void *)          &tuples);
	error |= clSetKernelArg (kernel, 1, sizeof(int), (void *)         &_table_);
	error |= clSetKernelArg (kernel, 2, sizeof(int), (void *)       &__stash_x);
	error |= clSetKernelArg (kernel, 3, sizeof(int), (void *)       &__stash_y);
	error |= clSetKernelArg (kernel, 4, sizeof(int), (void *)  &max_iterations);
	/* Set I/O byte buffers */
	error |= clSetKernelArg (
		kernel,
		5,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[0]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		6,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[1]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		7,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[2]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		8,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[3]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		9,
		sizeof(cl_mem),
		(void *) &(context->kernelInput.inputs[4]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		10,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[0]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		11,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[1]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		12,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[2]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		13,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[3]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		14,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[4]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		15,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[5]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		16,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[6]->device_buffer));
	error |= clSetKernelArg (
		kernel,
		17,
		sizeof(cl_mem),
		(void *) &(context->kernelOutput.outputs[7]->device_buffer));

	/* Set local memory */
	error |= clSetKernelArg (kernel, 18, (size_t) _buffer_size, (void *) NULL);

	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s\n", error, getErrorMessage(error));
		exit (1);
	}
	return;
}

void callback_writeInput (gpuContextP context,
		JNIEnv *env, jobject obj, int qid, int ndx, int offset) {

	jclass class = (*env)->GetObjectClass (env, obj);
	jmethodID method = (*env)->GetMethodID (env, class,
			"inputDataMovementCallback",  "(IIJII)V");
	if (! method) {
		fprintf(stderr, "JNI error: failed to acquire write method pointer\n");
		exit(1);
	}
	/* Copy data across the JNI boundary */
	(*env)->CallVoidMethod (
			env, obj, method,
			qid,
			ndx,
			(long) (context->kernelInput.inputs[ndx]->mapped_buffer),
			context->kernelInput.inputs[ndx]->size,
			offset);

	(*env)->DeleteLocalRef(env, class);
	return ;
}

void callback_readOutput (gpuContextP context,
		JNIEnv *env, jobject obj, int qid, int ndx, int offset) {
	// fprintf(stderr, "callback_readOutput\n");
	jclass class = (*env)->GetObjectClass (env, obj);
	jmethodID method = (*env)->GetMethodID (env, class,
			"outputDataMovementCallback", "(IIJII)V");
	if (! method) {
		fprintf(stderr, "JNI error: failed to acquire read method pointer\n");
		exit(1);
	}
	if (! context->kernelOutput.outputs[ndx]->writeOnly)
		return ;
	/* Copy data across the JNI boundary */
	(*env)->CallVoidMethod (
			env, obj, method,
			qid,
			ndx,
			(long) (context->kernelOutput.outputs[ndx]->mapped_buffer),
			context->kernelOutput.outputs[ndx]->size,
			offset);

	(*env)->DeleteLocalRef(env, class);
	return;
}

/* Thread affinity library calls */

static cpu_set_t fullSet;

static cpu_set_t *getFullSet (void) {
	static int init = 0;
	if (init == 0) {
		int i;
		int ncores = sysconf(_SC_NPROCESSORS_ONLN);
		CPU_ZERO (&fullSet);
		for (i = 0; i < ncores; i++)
			CPU_SET (i, &fullSet);
		init = 1;
	}
	return &fullSet;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_getNumCores
(JNIEnv *env, jobject obj) {

	(void) env;
	(void) obj;
	
	int ncores = 0;
	ncores = sysconf(_SC_NPROCESSORS_ONLN);
	
	return ncores;
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_bind
(JNIEnv *env, jobject obj, jint core) {
	
	(void) env;
	(void) obj;
	
	cpu_set_t set;
	CPU_ZERO (&set);
	CPU_SET  (core, &set);
	
	return sched_setaffinity (0, sizeof(set), &set);
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_unbind
(JNIEnv *env, jobject obj) {

	(void) env;
	(void) obj;
	
	return sched_setaffinity (0, sizeof (cpu_set_t), getFullSet());
}

JNIEXPORT jint JNICALL Java_uk_ac_imperial_lsds_seep_multi_TheGPU_getCpuId
(JNIEnv *env, jobject obj) {

	(void) env;
	(void) obj;
	
	int core = -1;
	cpu_set_t set;
	
	int error = sched_getaffinity (0, sizeof (set), &set);
	if (error < 0)
		return core; /* -1 */
	for (core = 0; core < CPU_SETSIZE; core++) {
        	if (CPU_ISSET (core, &set))
			break;
	}
	return core;
}
