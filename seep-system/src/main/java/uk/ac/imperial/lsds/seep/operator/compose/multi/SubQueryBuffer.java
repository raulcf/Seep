package uk.ac.imperial.lsds.seep.operator.compose.multi;

import uk.ac.imperial.lsds.seep.GLOBALS;

public class SubQueryBuffer {

	public static final int SUB_QUERY_BUFFER_CAPACITY = Integer.valueOf(GLOBALS.valueFor("subQueryBufferCapacity"));

	private MultiOpTuple[] elements;	
	
	private int start = 0;
	private int end = 0;
	private boolean full = false;
	
	private Object lock = new Object();
	
	private long processedTuples = 0L;
	
	/*
	 * ###############################################
	 * Constructors
	 * ###############################################
	 */
	public SubQueryBuffer () {
		this(SUB_QUERY_BUFFER_CAPACITY);
	}
	
	public long getProcessedTuples() { 
		return processedTuples; 
	}
	
	public SubQueryBuffer(int size) {
		if (size <= 0)
			throw new IllegalArgumentException
			("Buffer size must be greater than 0.");
		elements = new MultiOpTuple[size];
	}

	/*
	 * ###############################################
	 * Read access methods
	 * ###############################################
	 */
	public int normIndex(int i) {
		return (i%elements.length);
	}

	public int capacity() {
		return this.elements.length;
	}
	
	public boolean validIndex(int i) {
		if (i < 0) {
			System.out.println("ERROR smaller zero: " + i);
			return false;
		}
		
		if ((end < start) && (i > end) && (i < start)) {
			System.out.println("ERROR 1: " + i + " " + start + " " + end);
			return false;
		}			
		
		if ((start < end) && ((i >= end) || (i < start))) {
			System.out.println("ERROR 2: " + i + " " + start + " " + end);
			return false;
		}
		return true;
	}
	
	/**
	 * Non-synchronised access of the i-th element in the
	 * array. The given index is the actual index
	 * 
	 * @param i
	 * @return
	 */
	public MultiOpTuple get(int i) {
		int nI = normIndex(i); 
		// Checking would require synchronisation
		if (!validIndex(nI))
			throw new IllegalArgumentException();
		
		return elements[nI];
	}
	
	public int getStartIndex() {
		return this.start;
	}

	public int getIndexBefore(int i, int diff) {
		i -= diff;
		return normIndex(i + this.elements.length);
	}
	
	public MultiOpTuple[] getArray() {
		return getArray(this.start, getIndexBefore(this.end,1));
	}

	public MultiOpTuple[] getArray(int startIndex, int endIndex) {
		int nStartIndex = normIndex(startIndex);
		int nEndIndex = normIndex(endIndex);
		if (nEndIndex > nStartIndex) { 
			/* Normal mode */
			int length = nEndIndex - nStartIndex + 1;
			MultiOpTuple[] copy = new MultiOpTuple[length];
			System.arraycopy(elements, nStartIndex, copy, nStartIndex, length);
			return copy;
		} else {
			/* Copy in two parts */
			int lengthFirst = this.elements.length - nStartIndex;
			int lengthSecond = nEndIndex + 1;
			MultiOpTuple[] copy = new MultiOpTuple[lengthFirst + lengthSecond];
			System.arraycopy(elements, nStartIndex, copy, 0, lengthFirst);
			System.arraycopy(elements, 0, copy, lengthFirst-1, lengthSecond);
			return copy;
		}
	}

	/*
	 * ###############################################
	 * Update methods
	 * ###############################################
	 */

	public boolean add(MultiOpTuple element) {
		try {
			synchronized (lock) {
				while (full) 
					lock.wait();
			}
		} catch (InterruptedException e) {
			e.printStackTrace();
		}

		return insertElement(element);
	}

	public void setFreeUpToIndex(int i) {
		
		// Count how many tuples have been freed
		if (i < this.start)
			processedTuples += (i + elements.length - this.start);
		else
			processedTuples += (i - this.start);

//		System.out.println("Freeing up to " + i + "\t" + "start is: " + start + "\t new processed: " + processedTuples);

		// Set the mark for the data that can safely be overwritten
		this.start = normIndex(i + 1);
		full = false;

		// Notify the pushing thread
		synchronized (lock) {
			lock.notifyAll();
		}
	}

	private boolean insertElement(MultiOpTuple element) {
		elements[end] = element;
		end = normIndex(end + 1);
		full = (start == end);
		return true;
	}

	/*
	 * ###############################################
	 * OLD METHODS
	 * ###############################################
	 */

//	private void freeIndex(int i) {
//		int nI = normIndex(i); 
//		if (!validIndex(nI))
//			throw new IllegalArgumentException();
//		
//		freeElements[nI] = true;
//
//			while((freeElements[start]) && ((end != start) || (end == start && start == nI))) {
//				int free = start;
//				// first, move pointer
//				start = normIndex(start+1);
//				// second, reset the buffer
//				elements[free] = null;
//				freeElements[free] = false;
//			}
//			
//			if ((end != start) || (end == start && start == nI))
//				full = false;
//	}
//	
//	public void freeUpToIndex(int i) {
//		
//		// System.out.println("____________freeUpToIndex");
//		
//		int nI = normIndex(i); 
//		
//		synchronized (internalLock) {
//			
//			if (i < start) {
//				System.err.println("Error.");
//				System.exit(1);
//			}
//			
//			processedTuples += (i - start);
//			
//			int toFree = start;
//			do {
//				freeIndex(toFree);
//				toFree = normIndex(toFree + 1);
//			}
//			while (toFree != nI);
//		}
//		
//		synchronized (getExternalLock()) {
//			this.getExternalLock().notifyAll();
//		}
//	}
	
//	private boolean[] freeElements;	
//	private Object internalLock = new Object();

	
//	private boolean isFull() {
//		return this.full;
//	}
//
//	public int size () {
//		int size = 0;
//		
//		if (end  < start) 
//			size = elements.length - start + end;
//		else if (end == start) 
//			size = (full ? elements.length : 0);
//		else 
//			size = end - start;
//		
//		return size;
//	}
//	
//	public boolean isMoreRecentThan(int first, int second) {
//		int nFirst = normIndex(first); 
//		int nSecond = normIndex(second); 
//		if (start < end)
//			return (nFirst < nSecond);
//		else {
//			// (end <= start)
//			if ((nFirst >= start) && (nSecond >= start)) 
//				return (nFirst < nSecond);
//			if ((nFirst < end) && (nSecond < end)) 
//				return (nFirst < nSecond);
//			if ((nFirst >= start) && (nSecond < end)) 
//				return false;
//			// ((second >= start) && (first < end)) 
//				return true;		
//		}
//	}
//
//	public int capacity() {
//		return this.elements.length;
//	}
//
//	public int getEndIndex() {
//		return this.end;
//	}
}