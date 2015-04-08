package uk.ac.imperial.lsds.seep.multi;

import java.lang.reflect.Field;
import java.nio.ByteBuffer;

import sun.misc.Unsafe;

@SuppressWarnings("restriction")
public class TheGPU {
	
	private static final int MAX_QUERIES = 16;
	private static final int MAX_BUFFERS = 30;
	
	private static final String library = "/home/akolious/seep/seep-system/clib/libGPU.so";
	
	private static final TheGPU instance = new TheGPU ();
	
	private static Unsafe theUnsafe;
	
	private static Unsafe getUnsafeMemory () {
		try {
			Field theUnsafe = Unsafe.class.getDeclaredField("theUnsafe");
			theUnsafe.setAccessible(true);
			return (Unsafe) theUnsafe.get(null);
		} catch (Exception e) {
			throw new AssertionError(e);
		}
	}
	
	static {
		try {
			System.load (library);
		} catch (final UnsatisfiedLinkError e) {
			System.err.println(e.getMessage());
			System.err.println("error: failed to load GPU library");
			System.exit(1);
		}
		theUnsafe = getUnsafeMemory ();
	}
	
	public static TheGPU getInstance () { return instance; }
	
	private byte [][][] inputArray;
	private int  [][] start;
	private int  [][] end;
	
	private byte [][][] outputArray;
	
	public TheGPU () {
		inputArray  = new byte [MAX_QUERIES][MAX_BUFFERS][];
		
		start = new int [MAX_QUERIES][MAX_BUFFERS];
		end   = new int [MAX_QUERIES][MAX_BUFFERS];
		
		outputArray = new byte [MAX_QUERIES][MAX_BUFFERS][];
	}
	
	public void setInputBuffer (int qid, int ndx, byte [] inputArray) {
		setInputBuffer (qid, ndx, inputArray, 0, inputArray.length);
	}
	
	public void setInputBuffer (int qid, int ndx, byte [] inputArray, int start, int end) {
		/* Check bounds */
		if (qid < 0 || qid >= MAX_QUERIES)
			throw new IllegalArgumentException ("error: invalid query id");
		if (ndx < 0 || ndx >= MAX_BUFFERS)
			throw new IllegalArgumentException ("error: invalid buffer id");
		
		this.inputArray[qid][ndx] = inputArray;
		this.start[qid][ndx] = start;
		this.end[qid][ndx] = end;
	}
	
	public void setOutputBuffer (int qid, int ndx, byte [] outputArray) {
		this.outputArray[qid][ndx] = outputArray;
	}
	
	public void inputDataMovementCallback (int qid, int ndx, long inputAddr, int size, int offset) {
		
		/* System.out.println(String.format("[DBG] copy input: q %d ndx %d size %d (%d) offset %d", 
			qid, ndx, size, end[qid][ndx] - start[qid][ndx], offset)); */
		
		theUnsafe.copyMemory (
				inputArray[qid][ndx], 
				Unsafe.ARRAY_BYTE_BASE_OFFSET + start[qid][ndx], 
				null, 
				inputAddr, 
				end[qid][ndx] - start[qid][ndx]
			);
		
		/*
		if (end > start) {
			theUnsafe.copyMemory (
				this.inputArray, 
				Unsafe.ARRAY_BYTE_BASE_OFFSET + start, 
				null, 
				inputAddr, 
				end - start
			);
		} else {
			//
			// TODO: copy in two parts: 
			// 1) from start, `size - start` bytes; and
			// 2) from 0, `end` bytes
			//
			System.err.println("Fatal error.");
			System.exit(1);
		}
		*/
	}
	
	public void outputDataMovementCallback (int qid, int ndx, long outputAddr, int size, int offset) {
		// System.out.println(String.format("output data movement qid %d ndx %d size %d", qid, ndx, size));
		
		// System.out.println(String.format("[DBG] copy output from q %d ndx %d size %d offset %d", qid, ndx, size, offset));
		
		theUnsafe.copyMemory(
				null, 
				outputAddr, 
				outputArray[qid][ndx], 
				Unsafe.ARRAY_BYTE_BASE_OFFSET + offset, 
				size
			);
	}
	
	public native int init (int N);
	public native int getQuery (String source, int kernels, int inputs, int outputs);
	public native int setInput (int queryId, int index, int size);
	public native int setInput (int queryId, int index, ByteBuffer buffer, int size);
	public native int setOutput (int queryId, int index, int size, int writeOnly);
	public native int setOutput (int queryId, int index, ByteBuffer buffer, int size, int writeOnly);
	public native int execute (int queryId, int threads, int threadsPerGroup);
	
	public native int executeCustom (int queryId, int threads, int threadsPerGroup, int _threads, int _threadsPerGroup);
	
	public native int free ();
	/* Operator-specific function calls */
	public native int setKernelDummy     (int queryId, int [] args);
	public native int setKernelProject   (int queryId, int [] args);
	public native int setKernelSelect    (int queryId, int [] args);
	public native int setKernelReduce    (int queryId, int [] args);
	public native int setKernelAggregate (int queryId, int [] args);
	
	/* Thread affinity functions */
	public native int getNumCores ();
	public native int bind (int cpu);
	public native int unbind ();
	public native int getCpuId ();
	
	/* Data movement tests */
	public native int testJNIDataMovement (int queryId);
	
	public native int copyInputBuffers (int queryId);
	public native int copyOutputBuffers (int queryId);
	
	public native int moveInputBuffers (int queryId);
	public native int moveOutputBuffers (int queryId);
	public native int moveInputAndOutputBuffers (int queryId);
	
	public native int testDataMovement (int queryId);
	public native int testOverlap (int queryId, int threads, int threadsPerGroup);
	
	public native long getBufferAddress(int index, int size);
	public native long getBufferAddress(int queryId, int index, int size);
}