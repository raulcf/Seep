#include "gpucontext.h"

#include "openclerrorcode.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

gpuContextP gpu_context (
	cl_device_id device, 
	cl_context  context, 
	cl_program  program,
	int        _kernels,
	int         _inputs,
	int        _outputs) 
{
	gpuContextP q = (gpuContextP) malloc (sizeof(gpu_context_t));
	if (! q) {
		fprintf(stderr, "fatal error: out of memory\n");
		exit(1);	
	}
	/* Initialise OpenCL execution context */
	q->device = device;
	q->context = context;
	q->program = program; 
	q->kernel.count = _kernels;
	q->kernelInput.count = _inputs;
	q->kernelOutput.count = _outputs;
	/* Create command queues */
	int error;
	q->queue[0] = clCreateCommandQueue (
		q->context, 
		q->device, 
		CL_QUEUE_PROFILING_ENABLE, 
		&error);
	if (! q->queue[0]) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}
	q->queue[1] = clCreateCommandQueue (
		q->context, 
		q->device, 
		CL_QUEUE_PROFILING_ENABLE, 
		&error);
	if (! q->queue[1]) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}
	q->scheduled = 0; /* No read or write events scheduled */
	q->readCount = 0;
	q->writeCount = 0;
	return q;
}

void gpu_context_free (gpuContextP q) {
	if (q) {
		int i;
		/* Free input(s) */
		for (i = 0; i < q->kernelInput.count; i++)
			freeInputBuffer(q->kernelInput.inputs[i], q->queue[0]);
		/* Free output(s) */
		for (i = 0; i < q->kernelOutput.count; i++)
			freeOutputBuffer(q->kernelOutput.outputs[i], q->queue[0]);
		/* Free kernel(s) */
		for (i = 0; i < q->kernel.count; i++) {
			clReleaseKernel(q->kernel.kernels[i]->kernel[0]);
			clReleaseKernel(q->kernel.kernels[i]->kernel[1]);
			free (q->kernel.kernels[i]);
		}
		if (q->queue[0])
			clReleaseCommandQueue(q->queue[0]);
		if (q->queue[1])
			clReleaseCommandQueue(q->queue[1]);
		free(q);
	}
}

void gpu_context_setKernel (gpuContextP q, int ndx,
		const char *name, void (*callback)(cl_kernel, gpuContextP, int *), int *args) {
	if (ndx < 0 || ndx >= q->kernel.count)
		return;
	int error = 0;
	int i;
	q->kernel.kernels[ndx] = (aKernelP) malloc (sizeof(a_kernel_t));
	for (i = 0; i < 2; i++) {
		q->kernel.kernels[ndx]->kernel[i] = clCreateKernel (q->program, name, &error);
		if (! q->kernel.kernels[ndx]->kernel[i]) {
			fprintf(stderr, "opencl error (%d): %s (%s)\n", 
				error, getErrorMessage(error), __FUNCTION__);
			exit (1);
		} else {
			(*callback) (q->kernel.kernels[ndx]->kernel[i], q, args);
		}
	}
	return;
}

void gpu_context_setInput (gpuContextP q, int ndx, void *buffer, int size) {
	if (ndx < 0 || ndx >= q->kernelInput.count)
		return;
	q->kernelInput.inputs[ndx] = getInputBuffer (q->context, q->queue[0],
		buffer, size);
}

void gpu_context_setOutput (gpuContextP q, int ndx, void *buffer, int size, 
	int writeOnly, int doNotMove, int bearsMark, int readEvent) {
	
	if (ndx < 0 || ndx >= q->kernelOutput.count)
		return;
	q->kernelOutput.outputs[ndx] = getOutputBuffer (q->context, q->queue[0],
		buffer, size, writeOnly, doNotMove, bearsMark, readEvent);
}

#ifdef GPU_PROFILE
static unsigned first = 0;
static cl_ulong reference = 0;

static float normalise (cl_ulong p, cl_ulong q) {
	float result = ((float) (p - q) / 1000.);
	return result;
}

static void printEventProfilingInfo (cl_event event, const char *acronym) {
	cl_ulong q, s, x, f; /* queued, submitted, start, and end timestamps */
	float   _q,_s,_x,_f;
	int error = 0;
	error |= clGetEventProfilingInfo(
		event, 
		CL_PROFILING_COMMAND_QUEUED, 
		sizeof(cl_ulong),
		&q, 
		NULL);
	error |= clGetEventProfilingInfo(
		event, 
		CL_PROFILING_COMMAND_SUBMIT, 
		sizeof(cl_ulong), 
		&s, 
		NULL);
	error |= clGetEventProfilingInfo(
		event, 
		CL_PROFILING_COMMAND_START, 
		sizeof(cl_ulong), 
		&x, 
		NULL);
	error |= clGetEventProfilingInfo(
		event, 
		CL_PROFILING_COMMAND_END, 
		sizeof(cl_ulong), 
		&f, 
		NULL);
	if (! first) {
		reference = q;
		first = 1;
	}
	_q = normalise(q, reference);
	_s = normalise(s, reference);
	_x = normalise(x, reference);
	_f = normalise(f, reference);
	fprintf(stdout, "[PRF] %5s\tq %10.1f\ts %10.1f\t x %10.1f\t f %10.1f t %10.1f\n", acronym, _q, _s, _x, _f, ((float) (f - x)) / 1000.);
}
#endif

#ifdef GPU_VERBOSE
static int getEventReferenceCount (cl_event event) {
	int error = 0;
	cl_uint count = 0;
	error = clGetEventInfo (
		event,
		CL_EVENT_REFERENCE_COUNT,
		sizeof(cl_uint),
		(void *) &count,
		NULL);
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		return -1;
	} else
		return (int) count;
}

static char *getEventCommandStatus (cl_event event) {
	int error = 0;
	cl_int status;
	error = clGetEventInfo (
		event,
		CL_EVENT_COMMAND_EXECUTION_STATUS,
		sizeof(cl_int),
		(void *) &status,
		NULL);
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		return getCommandExecutionStatus(-1);
	} else
		return getCommandExecutionStatus(status);
}

static char *getEventCommandType (cl_event event) {
	int error = 0;
	cl_command_type type;
	error = clGetEventInfo (
		event,
		CL_EVENT_COMMAND_TYPE,
		sizeof(cl_command_type),
		(void *) &type,
		NULL);
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		return getCommandType(-1);
	} else
		return getCommandType(type);
}
#endif

#ifdef GPU_PROFILE
void gpu_context_profileQuery (gpuContextP q) {
	/*
	 * This method should be called after we wait for the
	 * the read event. 
	 *
	 * If the latter returns with no errors (CL_SUCCESS), 
	 * then the query kernels have been executed success-
	 * fully as well.
	 */
	char msg[64];
	int i;
	for (i = 0; i < q->kernel.count; i++) {
		memset(msg, 0, 64);
		sprintf(msg, "K%02d", i);
		printEventProfilingInfo(q->exec_event[i], msg);
		clReleaseEvent(q->exec_event[i]);
	}
}
#endif

void gpu_context_waitForReadEvent (gpuContextP q) {
	if (q->scheduled < 1 || q->readCount < 1)
		return;
	dbg("[DBG] read : %2d references, command %30s status %20s \n",
			getEventReferenceCount (q->read_event),
			getEventCommandType    (q->read_event),
			getEventCommandStatus  (q->read_event)
			);
	/* Wait for read event */
	int error = clWaitForEvents(1, &(q->read_event));
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}
#ifdef GPU_PROFILE
	printEventProfilingInfo(q->write_event, "W");
	gpu_context_profileQuery (q);
	printEventProfilingInfo(q->read_event, "R");
#endif
	q->readCount -= 1;
	clReleaseEvent(q->read_event);
	return ;
}

/*
void gpu_context_waitForExecEvent (gpuContextP q) {
	if (q->scheduled < 1)
		return;
	int error = clWaitForEvents(1, &(q->exec_event));
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}
	clReleaseEvent(q->exec_event);
	return ;
}
*/

void gpu_context_waitForWriteEvent (gpuContextP q) {
	if (q->scheduled < 1  || q->writeCount < 1)
		return;
	dbg("[DBG] write: %2d references, command %30s status %20s \n",
			getEventReferenceCount (q->write_event),
			getEventCommandType    (q->write_event),
			getEventCommandStatus  (q->write_event)
			);
	/* Wait for write event */
	int error = clWaitForEvents(1, &(q->write_event));
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}
	q->writeCount -= 1;
	clReleaseEvent(q->write_event);
	return ;
}

void gpu_context_flush (gpuContextP q) {
	int error = 0;
	error |= clFlush (q->queue[0]);
	error |= clFlush (q->queue[1]);
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}
}

void gpu_context_finish (gpuContextP q) {
	if (q->scheduled < 1)
		return;
	/* There are tasks scheduled */
	int error = 0;
	error |= clFinish (q->queue[0]);
	error |= clFinish (q->queue[1]);
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}
}

void gpu_context_submitTask (gpuContextP q, size_t *threads, size_t *threadsPerGroup) {
	int i;
	int error = 0;
	/* Write */
	for (i = 0; i < q->kernelInput.count; i++) {
		if (i == q->kernelInput.count - 1) {
			error |= clEnqueueWriteBuffer (
				q->queue[0],
				q->kernelInput.inputs[i]->device_buffer,
				CL_FALSE,
				0,
				q->kernelInput.inputs[i]->size,
				q->kernelInput.inputs[i]->mapped_buffer,
				0, NULL, &q->write_event);
		} else {
			error |= clEnqueueWriteBuffer (
				q->queue[0],
				q->kernelInput.inputs[i]->device_buffer,
				CL_FALSE,
				0,
				q->kernelInput.inputs[i]->size,
				q->kernelInput.inputs[i]->mapped_buffer,
				0, NULL, NULL);
		}
		if (error != CL_SUCCESS) {
			fprintf(stderr, "opencl error (%d): %s (%s)\n", 
				error, getErrorMessage(error), __FUNCTION__);
			exit (1);
		}
	}
	q->writeCount += 1;
	
	/* Execute */
	for (i = 0; i < q->kernel.count; i++) {
		error |= clEnqueueNDRangeKernel (
			q->queue[0],
			q->kernel.kernels[i]->kernel[0],
			1,
			NULL,
			&(threads[i]),
			&(threadsPerGroup[i]),
			0, NULL, NULL);
	}
	
	/* Execute and get event notification */
	/*
	error = clEnqueueNDRangeKernel (
		q->queue[0],
		q->kernel.kernels[0]->kernel[0],
		1,
		NULL,
		&threads,
		&threadsPerGroup,
		0, NULL, &q->exec_event);
	*/

	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}

	/* Read */
	for (i = 0; i < q->kernelOutput.count; i++) {
		if (i == q->kernelOutput.count - 1) {
			error |= clEnqueueReadBuffer (
				q->queue[0],
				q->kernelOutput.outputs[i]->device_buffer,
				CL_FALSE,
				0,
				q->kernelOutput.outputs[i]->size,
				q->kernelOutput.outputs[i]->mapped_buffer,
				0, NULL, &q->read_event);
		} else {
			error |= clEnqueueReadBuffer (
				q->queue[0],
				q->kernelOutput.outputs[i]->device_buffer,
				CL_FALSE,
				0,
				q->kernelOutput.outputs[i]->size,
				q->kernelOutput.outputs[i]->mapped_buffer,
				0, NULL, NULL);
		}
	}
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}
	q->readCount += 1;
	
	/* Flush command queues */ /* gpu_context_flush (q) happens at gpu_query_exec */

	q->scheduled = 1;

	return;
}

void gpu_context_writeInput (gpuContextP q,
		void (*callback)(gpuContextP, JNIEnv *, jobject, int, int, int),
		JNIEnv *env, jobject obj, int qid) {
	int idx;
	for (idx = 0; idx < q->kernelInput.count; idx++)
		(*callback) (q, env, obj, qid, idx, 0);
	return;
}

void gpu_context_readOutput (gpuContextP q,
		void (*callback)(gpuContextP, JNIEnv *, jobject, int, int, int),
		JNIEnv *env, jobject obj, int qid) {
	int idx;
	/* Find mark */
	int mark = 0;
	for (idx = 0; idx < q->kernelOutput.count; idx++) {
		if (q->kernelOutput.outputs[idx]->bearsMark) {
			dbg("[DBG] output %d bears mark\n", idx);
			int N = q->kernelOutput.outputs[idx]->size / sizeof(int);
			int *values = (int *) (q->kernelOutput.outputs[idx]->mapped_buffer);
			int j;
			for (j = N - 1; j >= 0; j--) {
				if (values[j] != 0) {
					mark = values[j];
					break;
				}
			}
			dbg("[DBG] mark is %d\n", mark);
		}
	}
	/* mark = 0; */
	for (idx = 0; idx < q->kernelOutput.count; idx++)
		(*callback) (q, env, obj, qid, idx, mark);
	return;
}

void gpu_context_moveDirectInputBuffers (gpuContextP q, int *start, int *end) {
	int i;
	int error = 0;
	/* Write */
	for (i = 0; i < q->kernelInput.count; i++) {
		/*
		 * Make sure that if an input buffer wraps
		 * (in case of a circular buffer), we move
		 * data to GPU memory in two parts.
		 */
		if (start[i] >= end[i]) {
			fprintf(stderr, "error: invalid buffer pointers (%s)\n", __FUNCTION__);
			exit(-1);
		}
		int theSize = end - start;
		dbg("[DBG] write input %d: start %13d end %13d (%13d bytes)\n",
				i, start[i], end[i], theSize);
		/*
		 * Make sure that we do not overflow the
		 * GPU memory buffer.
		 */
		if (theSize > q->kernelInput.inputs[i]->size) {
			fprintf(stderr, "error: invalid buffer pointers (%s)\n", __FUNCTION__);
			exit(-1);
		}
		if (i == q->kernelInput.count - 1) {
			error |= clEnqueueWriteBuffer (
				q->queue[0],
				q->kernelInput.inputs[i]->device_buffer,
				CL_FALSE,
				0,
				theSize,
				(q->kernelInput.inputs[i]->mapped_buffer + start[i]),
				0, NULL, &q->write_event);
		} else {
			error |= clEnqueueWriteBuffer (
				q->queue[0],
				q->kernelInput.inputs[i]->device_buffer,
				CL_FALSE,
				0,
				theSize,
				(q->kernelInput.inputs[i]->mapped_buffer + start[i]),
				0, NULL, NULL);
		}
		if (error != CL_SUCCESS) {
			fprintf(stderr, "opencl error (%d): %s (%s)\n",
				error, getErrorMessage(error), __FUNCTION__);
			exit (1);
		}
	}
	q->writeCount += 1;
	q->scheduled = 1;

	return;
}

void gpu_context_moveInputBuffers (gpuContextP q) {
	int i;
	int error = 0;
	/* Write */
	for (i = 0; i < q->kernelInput.count; i++) {
		if (i == q->kernelInput.count - 1) {
			error |= clEnqueueWriteBuffer (
				q->queue[0],
				q->kernelInput.inputs[i]->device_buffer,
				CL_FALSE,
				0,
				q->kernelInput.inputs[i]->size,
				q->kernelInput.inputs[i]->mapped_buffer,
				0, NULL, &q->write_event);
		} else {
			error |= clEnqueueWriteBuffer (
				q->queue[0],
				q->kernelInput.inputs[i]->device_buffer,
				CL_FALSE,
				0,
				q->kernelInput.inputs[i]->size,
				q->kernelInput.inputs[i]->mapped_buffer,
				0, NULL, NULL);
		}
		if (error != CL_SUCCESS) {
			fprintf(stderr, "opencl error (%d): %s (%s)\n",
				error, getErrorMessage(error), __FUNCTION__);
			exit (1);
		}
	}
	q->writeCount += 1;
	q->scheduled = 1;

	return;
}

void gpu_context_moveOutputBuffers (gpuContextP q) {
	int i;
	int error = 0;
	/* Read */
	for (i = 0; i < q->kernelOutput.count; i++) {
		
		if (q->kernelOutput.outputs[i]->doNotMove)
			continue;
		
		if (q->kernelOutput.outputs[i]->readEvent) {
			error |= clEnqueueReadBuffer (
				q->queue[0],
				q->kernelOutput.outputs[i]->device_buffer,
				CL_FALSE,
				0,
				q->kernelOutput.outputs[i]->size,
				q->kernelOutput.outputs[i]->mapped_buffer,
				0, NULL, &q->read_event);
		} else {
			error |= clEnqueueReadBuffer (
				q->queue[0],
				q->kernelOutput.outputs[i]->device_buffer,
				CL_FALSE,
				0,
				q->kernelOutput.outputs[i]->size,
				q->kernelOutput.outputs[i]->mapped_buffer,
				0, NULL, NULL);
		}
	}
	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}
	q->readCount += 1;
	q->scheduled = 1;

	return;
}

void gpu_context_submitKernel (gpuContextP q, size_t *threads, size_t *threadsPerGroup) {
	int i;
	int error = 0;

	/* Execute */
	for (i = 0; i < q->kernel.count; i++) {
		dbg("[DBG] submit kernel %d: %10zu threads %10zu threads/group\n", 
			i, threads[i], threadsPerGroup[i]);
#ifdef GPU_PROFILE
		error |= clEnqueueNDRangeKernel (
			q->queue[0],
			q->kernel.kernels[i]->kernel[0],
			1,
			NULL,
			&(threads[i]),
			&(threadsPerGroup[i]),
			0, NULL, &(q->exec_event[i]));
#else
		error |= clEnqueueNDRangeKernel (
			q->queue[0],
			q->kernel.kernels[i]->kernel[0],
			1,
			NULL,
			&(threads[i]),
			&(threadsPerGroup[i]),
			0, NULL, NULL);
#endif

	}

	/* Execute and get event notification */
	/*
	error = clEnqueueNDRangeKernel (
		q->queue[0],
		q->kernel.kernels[0]->kernel[0],
		1,
		NULL,
		&threads,
		&threadsPerGroup,
		0, NULL, &q->exec_event);
	*/

	if (error != CL_SUCCESS) {
		fprintf(stderr, "opencl error (%d): %s (%s)\n", 
			error, getErrorMessage(error), __FUNCTION__);
		exit (1);
	}

	return;
}
