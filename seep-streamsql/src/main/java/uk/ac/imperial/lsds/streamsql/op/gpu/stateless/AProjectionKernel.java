package uk.ac.imperial.lsds.streamsql.op.gpu.stateless;

import uk.ac.imperial.lsds.seep.multi.IMicroOperatorCode;
import uk.ac.imperial.lsds.seep.multi.IQueryBuffer;
import uk.ac.imperial.lsds.seep.multi.ITupleSchema;
import uk.ac.imperial.lsds.seep.multi.IWindowAPI;
import uk.ac.imperial.lsds.seep.multi.TheGPU;
import uk.ac.imperial.lsds.seep.multi.UnboundedQueryBufferFactory;
import uk.ac.imperial.lsds.seep.multi.Utils;
import uk.ac.imperial.lsds.seep.multi.WindowBatch;
import uk.ac.imperial.lsds.streamsql.expressions.Expression;
import uk.ac.imperial.lsds.streamsql.expressions.ExpressionsUtil;
import uk.ac.imperial.lsds.streamsql.op.IStreamSQLOperator;
import uk.ac.imperial.lsds.streamsql.op.gpu.KernelCodeGenerator;
import uk.ac.imperial.lsds.streamsql.visitors.OperatorVisitor;

public class AProjectionKernel implements IStreamSQLOperator, IMicroOperatorCode {
	/*
	 * This input size must be greater or equal to the size of the byte array backing
	 * an input window batch.
	 */
	
	private static final int THREADS_PER_GROUP = 128;
	
	private static final int PIPELINES = 2;
	
	private int [] taskIdx;
	private int [] freeIdx;
	
	private Expression [] expressions;
	private ITupleSchema inputSchema, outputSchema;
	
	private static String filename = "/home/akolious/seep/seep-system/clib/templates/Projection.cl";
	
	private int qid;
	
	private int [] args;
	
	private int tuples;
	
	private int [] threads;
	private int [] tgs; /* Threads/group */
	
	/* GPU global and local memory sizes */
	private int  inputSize = -1,  localInputSize;
	private int outputSize = -1, localOutputSize;
	
	public AProjectionKernel(Expression[] expressions, ITupleSchema inputSchema) {
		
		this.expressions = expressions;
		this.inputSchema = inputSchema;
		
		this.outputSchema = 
				ExpressionsUtil.getTupleSchemaForExpressions(expressions);
		
		/* Task pipelining internal state */
		
		taskIdx = new int [PIPELINES];
		freeIdx = new int [PIPELINES];
		for (int i = 0; i < PIPELINES; i++) {
			taskIdx[i] = -1;
			freeIdx[i] = -1;
		}
	}
	
	public void setInputSize (int inputSize) {
		this.inputSize = inputSize;
	}
	
	public void setup() {
		
		/* Configure kernel arguments */
		
		if (inputSize < 0) {
			System.err.println("error: invalid input size");
			System.exit(1);
		}
		this.tuples = inputSize / inputSchema.getByteSizeOfTuple();
		
		this.threads = new int [1];
		threads[0] = tuples;
		
		this.tgs = new int [1];
		tgs[0] = THREADS_PER_GROUP;
		
		this.outputSize = tuples * outputSchema.getByteSizeOfTuple();
		
		this.localInputSize  = tgs[0] *  inputSchema.getByteSizeOfTuple();
		this.localOutputSize = tgs[0] * outputSchema.getByteSizeOfTuple();
		
		args = new int [4];
		args[0] = tuples;
		args[1] = inputSize;
		args[2] = localInputSize;
		args[3] = localOutputSize;
		
		String source = KernelCodeGenerator.getProjection(inputSchema, outputSchema, filename);
		
		qid = TheGPU.getInstance().getQuery(source, 1, 1, 1);
		
		TheGPU.getInstance().setInput (qid, 0,  inputSize);
		TheGPU.getInstance().setOutput(qid, 0, outputSize, 1, 0, 0, 1);
		
		TheGPU.getInstance().setKernelProject (qid, args);
	}
	
	@Override
	public String toString () {
		StringBuilder sb = new StringBuilder();
		sb.append("Projection (");
		for (Expression expr : expressions)
			sb.append(expr.toString() + " ");
		sb.append(")");
		return sb.toString();
	}
	
	@Override
	public void processData (WindowBatch windowBatch, IWindowAPI api) {
		
		int currentTaskIdx = windowBatch.getTaskId();
		int currentFreeIdx = windowBatch.getFreeOffset();
		
		/* Set input */
		byte [] inputArray = windowBatch.getBuffer().array();
		int start = windowBatch.getBatchStartPointer();
		int end   = windowBatch.getBatchEndPointer();
		
		TheGPU.getInstance().setInputBuffer(qid, 0, inputArray, start, end);
		
		/* Set output */
		IQueryBuffer outputBuffer = UnboundedQueryBufferFactory.newInstance();
		TheGPU.getInstance().setOutputBuffer(qid, 0, outputBuffer.array());
		
		/* Execute */
		TheGPU.getInstance().execute(qid, threads, tgs);
		
		windowBatch.setBuffer(outputBuffer);
		
		windowBatch.setTaskId     (taskIdx[0]);
		windowBatch.setFreeOffset (freeIdx[0]);
		
		for (int i = 0; i < taskIdx.length - 1; i++) {
			taskIdx[i] = taskIdx [i + 1];
			freeIdx[i] = freeIdx [i + 1];
		}
		taskIdx [taskIdx.length - 1] = currentTaskIdx;
		freeIdx [freeIdx.length - 1] = currentFreeIdx;
		
		api.outputWindowBatchResult(-1, windowBatch);
	}
	
	@Override
	public void accept(OperatorVisitor visitor) {
		visitor.visit(this);
	}

	@Override
	public void processData(WindowBatch firstWindowBatch,
			WindowBatch secondWindowBatch, IWindowAPI api) {
		throw new UnsupportedOperationException("ProjectionKernel operates on a single stream only");
	}
}
