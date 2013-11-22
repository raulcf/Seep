/*******************************************************************************
 * Copyright (c) 2013 Imperial College London.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 * 
 * Contributors:
 *     Raul Castro Fernandez - initial design and implementation
 ******************************************************************************/
package uk.ac.imperial.lsds.seep.api;

import java.util.List;

import uk.ac.imperial.lsds.seep.infrastructure.master.Node;
import uk.ac.imperial.lsds.seep.operator.Connectable;
import uk.ac.imperial.lsds.seep.operator.OperatorCode;
import uk.ac.imperial.lsds.seep.state.StateWrapper;

public class QueryBuilder {
	
	private static QueryPlan qp = new QueryPlan();
	
	public static QueryPlan build(){
		return qp;
	}
	
	public static Connectable newStatefulSource(OperatorCode op, int opId, StateWrapper s, List<String> attributes){
		return qp.newStatefulSource(op, opId, s, attributes);
	}
	
	public static Connectable newStatelessSource(OperatorCode op, int opId, List<String> attributes){
		return qp.newStatelessSource(op, opId, attributes);
	}
	
	public static Connectable newStatefulOperator(OperatorCode op, int opId, StateWrapper s, List<String> attributes){
		return qp.newStatefulOperator(op, opId, s, attributes);
	}
	
	public static Connectable newStatelessOperator(OperatorCode op, int opId, List<String> attributes){
		return qp.newStatelessOperator(op, opId, attributes);
	}
	
	public static Connectable newStatefulSink(OperatorCode op, int opId, StateWrapper s, List<String> attributes){
		return qp.newStatefulSink(op, opId, s, attributes);
	}
	
	public static Connectable newStatelessSink(OperatorCode op, int opId, List<String> attributes){
		return qp.newStatelessSink(op, opId, attributes);
	}

	public static void scaleOut(int opToScaleOut, int numPartitions){
		qp.scaleOut(opToScaleOut, numPartitions);
	}
	
	public static void scaleOut(int opToScaleOut, int newOpId, Node newProvisionedNode){
		qp.scaleOut(opToScaleOut, newOpId, newProvisionedNode);
	}
	
}