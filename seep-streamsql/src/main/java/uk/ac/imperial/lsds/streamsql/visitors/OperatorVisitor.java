package uk.ac.imperial.lsds.streamsql.visitors;

import uk.ac.imperial.lsds.streamsql.op.gpu.stateful.GPUMicroAggregation;
import uk.ac.imperial.lsds.streamsql.op.gpu.stateless.GPUSelection;
import uk.ac.imperial.lsds.streamsql.op.stateful.Aggregation;
import uk.ac.imperial.lsds.streamsql.op.stateful.Distinct;
import uk.ac.imperial.lsds.streamsql.op.stateful.MicroAggregation;
import uk.ac.imperial.lsds.streamsql.op.stateful.MicroPaneAggregation;
import uk.ac.imperial.lsds.streamsql.op.stateful.ThetaJoin;
import uk.ac.imperial.lsds.streamsql.op.stateless.Projection;
import uk.ac.imperial.lsds.streamsql.op.stateless.Selection;

public interface OperatorVisitor {

//	public void visit(AggregateOperator aggregation);
//
//	public void visit(ChainOperator chain);

	public void visit(Distinct distinct);

	public void visit(Projection projection);
	
	public void visit(Selection selection);

	public void visit(Aggregation aggregation);

	public void visit(MicroAggregation aggregation);

	public void visit(MicroPaneAggregation aggregation);

	public void visit(ThetaJoin join);
	
	public void visit(GPUSelection selection);
	
	public void visit(GPUMicroAggregation aggregation);
}
