/*******************************************************************************
 * Copyright (c) 2014 Imperial College London
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 * 
 * Contributors:
 *     Raul Castro Fernandez - initial API and implementation
 ******************************************************************************/
package query.operators;

import java.util.List;
import java.util.Map;

import uk.ac.imperial.lsds.seep.comm.NodeManagerCommunication;
import uk.ac.imperial.lsds.seep.comm.serialization.DataTuple;
import uk.ac.imperial.lsds.seep.comm.serialization.messages.TuplePayload;
import uk.ac.imperial.lsds.seep.operator.StatelessOperator;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
public class Source implements StatelessOperator {
	Logger LOG = LoggerFactory.getLogger(Source.class);

	private static final long serialVersionUID = 1L;
	
	public void setUp() {
		
	}
	
	public void processData(DataTuple dt) {
		Map<String, Integer> mapper = api.getDataMapper();
		DataTuple data = new DataTuple(mapper, new TuplePayload());
		int value1 = 0;
		int value2 = 0;
		int value3 = 0;

		
		while(true){
			value1 = value1+1;
			value2 = value2+2;
			value3 = value3+3;
			
			DataTuple output = data.newTuple(value1, value2, value3);
			
			api.send(output);

			try {
				Thread.sleep(1000);
			} 
			catch (InterruptedException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
		}
	}
	
	public void processData(List<DataTuple> arg0) {
		// TODO Auto-generated method stub
		
	}
}
