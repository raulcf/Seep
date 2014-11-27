package uk.ac.imperial.lsds.streamsql.op.stateless;

import uk.ac.imperial.lsds.seep.multi.IMicroOperatorCode;
import uk.ac.imperial.lsds.seep.multi.IQueryBuffer;
import uk.ac.imperial.lsds.seep.multi.IWindowAPI;
import uk.ac.imperial.lsds.seep.multi.ITupleSchema;
import uk.ac.imperial.lsds.seep.multi.UnboundedQueryBufferFactory;
import uk.ac.imperial.lsds.seep.multi.WindowBatch;
import uk.ac.imperial.lsds.streamsql.op.IStreamSQLOperator;
import uk.ac.imperial.lsds.streamsql.predicates.IPredicate;
import uk.ac.imperial.lsds.streamsql.visitors.OperatorVisitor;

public class Selection implements IStreamSQLOperator, IMicroOperatorCode {

	private IPredicate predicate;

	public Selection(IPredicate predicate) {
		this.predicate = predicate;
	}
	
	@Override
	public String toString() {
		final StringBuilder sb = new StringBuilder();
		sb.append("Selection (");
		sb.append(predicate.toString());
		sb.append(")");
		return sb.toString();
	}

	@Override
	public void accept(OperatorVisitor ov) {
		ov.visit(this);
	}

	public IPredicate getPredicate() {
		return this.predicate;
	}
	
	@Override
	public void processData(WindowBatch windowBatch, IWindowAPI api) {
		
		int[] startPointers = windowBatch.getWindowStartPointers();
		int[] endPointers = windowBatch.getWindowEndPointers();
		
		IQueryBuffer inBuffer = windowBatch.getBuffer();
		IQueryBuffer outBuffer = UnboundedQueryBufferFactory.newInstance();
		ITupleSchema schema = windowBatch.getSchema();

		int byteSizeOfTuple = schema.getByteSizeOfTuple();
		
		for (int currentWindow = 0; currentWindow < startPointers.length; currentWindow++) {
			int inWindowStartOffset = startPointers[currentWindow];
			int inWindowEndOffset = endPointers[currentWindow];

			/*
			 * If the window is empty, we skip it 
			 */
			if (inWindowStartOffset != -1) {
				
				startPointers[currentWindow] = outBuffer.getByteBuffer().position();
				// for all the tuples in the window
				while (inWindowStartOffset <= inWindowEndOffset) {
					if (this.predicate.satisfied(inBuffer, schema, inWindowStartOffset)) {
						inBuffer.appendBytesTo(inWindowStartOffset, byteSizeOfTuple, outBuffer);
					}
					inWindowStartOffset += byteSizeOfTuple;
				}
				endPointers[currentWindow] = outBuffer.getByteBuffer().position();
			}
		}
		
		// release old buffer (will return Unbounded Buffers to the pool)
		inBuffer.release();
		// reuse window batch by setting the new buffer
		windowBatch.setBuffer(outBuffer);
		api.outputWindowBatchResult(-1, windowBatch);
	}

}
