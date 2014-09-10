package uk.ac.imperial.lsds.seep.operator.compose.window;


public abstract class WindowBatch implements IWindowBatch {

	protected int[] windowStartPointers;
	protected int[] windowEndPointers;

	protected long startTimestamp = -1;
	protected long endTimestamp = -1;

	@Override
	public void setStartTimestamp(long startTimestamp) {
		this.startTimestamp = startTimestamp;
	}

	@Override
	public void setEndTimestamp(long endTimestamp) {
		this.endTimestamp = endTimestamp;
	}
	
	@Override
	public long getStartTimestamp() {
		return this.startTimestamp;
	}

	@Override
	public long getEndTimestamp() {
		return this.endTimestamp;
	}

	@Override
	public int[] getWindowStartPointers() {
		return this.windowStartPointers;
	}

	@Override
	public int[] getWindowEndPointers() {
		return this.windowEndPointers;
	}
	
	@Override
	public void performIncrementalComputation(
			IMicroIncrementalComputation incrementalComputation, IWindowAPI api) {
		
		int prevWindowStart = -1;
		int prevWindowEnd = -1;
		for (int currentWindow = 0; currentWindow < this.windowStartPointers.length; currentWindow++) {
			int windowStart = this.windowStartPointers[currentWindow];
			int windowEnd = this.windowEndPointers[currentWindow];

			// empty window?
			if (windowStart == -1) {
				if (prevWindowStart != -1) 
					for (int i = prevWindowStart; i < windowStart; i++)
						incrementalComputation.exitedWindow(this.get(i));
				
				incrementalComputation.evaluateWindow(api);
			}
			else {
				/*
				 * Tuples in current window that have not been in the previous window
				 */
				if (prevWindowStart != -1) {
					for (int i = prevWindowEnd; i <= windowEnd; i++) 
						incrementalComputation.enteredWindow(this.get(i));
				}
				else {
					for (int i = windowStart; i <= windowEnd; i++) 
						incrementalComputation.enteredWindow(this.get(i));
				}

				/*
				 * Tuples in previous window that are not in current window
				 */
				if (prevWindowStart != -1) 
					for (int i = prevWindowStart; i < windowStart; i++)
						incrementalComputation.exitedWindow(this.get(i));
			
				incrementalComputation.evaluateWindow(api);
			
				prevWindowStart = windowStart;
				prevWindowEnd = windowEnd;
			}
		}
	}

}