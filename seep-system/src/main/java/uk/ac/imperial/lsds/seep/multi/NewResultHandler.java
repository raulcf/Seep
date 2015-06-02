package uk.ac.imperial.lsds.seep.multi;

import java.util.concurrent.Semaphore;
import java.util.concurrent.atomic.AtomicIntegerArray;

public class NewResultHandler {

	public final int SLOTS = Utils.TASKS * 4;

	public IQueryBuffer freeBuffer;
	
	/*
	 * Flags:
	 *  -1 - slot is free
	 *   0 - slot is being populated by a thread
	 *   
	 *   1 - slot is occupied, but "unlocked"; partial  results
	 *   3 - slot is occupied, but "unlocked"; complete results
	 *   
	 *   2 - slot is occupied, but "locked" (somebody is working on it)
	 */
	public AtomicIntegerArray slots;
	
	/*
	 * Structures to hold the actual data
	 */
	public IQueryBuffer [] results = new IQueryBuffer [SLOTS];
	public int [] offsets = new int [SLOTS];
	
	/* A query can have more than one downstream sub-queries. */
	public int [] latch = new int [SLOTS];
	
	public int [] mark  = new int [SLOTS];
	
	Semaphore semaphore; /* Protects next */
	int next;

	public int wraps = 0;
	
	private long totalOutputBytes = 0L;

	public NewResultHandler (IQueryBuffer freeBuffer, SubQuery query) {
		
		this.freeBuffer = freeBuffer;
		
		slots = new AtomicIntegerArray(SLOTS);
		
		for (int i = 0; i < SLOTS; i++) {
			slots.set(i, -1);
			offsets[i] = Integer.MIN_VALUE;
			
			latch[i] = 0;
			mark [i] =-1;
		}
		
		next = 0;
		semaphore = new Semaphore(1, false);
	}
	
	public long getTotalOutputBytes () {
		
		return totalOutputBytes;
	}
	
	public void incTotalOutputBytes (int bytes) {
		
		totalOutputBytes += (long) bytes;
	}
}