import java.nio.ByteBuffer;
import java.nio.ByteOrder;

import java.util.ArrayList;

import java.io.File;
import java.io.BufferedReader;
import java.io.DataInputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.BufferedOutputStream;

public class SmartGridCompressor {
	
	private static final String usage = "usage: java SmartGridCompressor";
	
	public static void main (String[] args) {
		
		int tupleSize = 32;
		int bundle = 512;
		
		String filename = "sorted.cvs";
		
		FileInputStream f;
		DataInputStream d;
		BufferedReader  b;
		
		String line = null;
		long lines = 0;
		long MAX_LINES = 974800518L;
		long percent_ = 0L, _percent = 0L;
		
		/* Time measurements */
		long start = 0L;
		long bytes = 0L;
		double dt;
		double rate; /* tuples/sec */
		double _1MB = 1024. * 1024.;
		double MBps; /* MB/sec */
		long totalTuples = 0;
		
		/* Parse command line arguments */
		int i, j;
		for (i = 0; i < args.length; ) {
			if ((j = i + 1) == args.length) {
				System.err.println(usage);
				System.exit(1);
			}
			if (args[i].equals("-b")) { 
				bundle = Integer.parseInt(args[j]);
			} else
			if (args[i].equals("-f")) { 
				filename = args[j];
			} else {
				System.err.println(String.format("error: unknown flag %s %s", args[i], args[j]));
				System.exit(1);
			}
			i = j + 1;
		}
		
		ByteBuffer data = ByteBuffer.allocate(tupleSize * bundle);
		byte [] compressed;
		ArrayList<ByteBuffer> bundles = new ArrayList<ByteBuffer>();
		
		SmartGridTuple tuple = new SmartGridTuple ();
		
		long __ts_init = 0;
		
		try {
			/* Load file into memory */
			f = new FileInputStream(filename);
			d = new DataInputStream(f);
			b = new BufferedReader(new InputStreamReader(d));
			
			start = System.currentTimeMillis();
			long tuple_counter = 0L;
			long compressedBytes = 0L;
			
			while ((line = b.readLine()) != null) {
				lines += 1;
				bytes += line.length() + 1; // +1 for '\n'
				
				percent_ = (lines * 100) / MAX_LINES;
				if (percent_ == (_percent + 1)) {
					System.out.print(String.format("Loading file...%3d%%\r", percent_));
					_percent = percent_;
				}
				
				SmartGridTuple.parse(line, tuple);
				
				totalTuples += 1;
				
				if (totalTuples == 1)
					__ts_init = tuple.getTimestamp();
				
				/* Populate data */
				data.putLong  ((tuple.getTimestamp() - __ts_init)); /* Normalised */
				data.putFloat (tuple.getValue()    );
				data.putInt   (tuple.getProperty() );
				data.putInt   (tuple.getPlug()     );
				data.putInt   (tuple.getHousehold());
				data.putInt   (tuple.getHouse()    ); /* Tuple size is 28 bytes */
				data.putInt   (0); /* Padding 4 bytes */
				
				++tuple_counter;
				if (data.remaining() == 0) {
					/* Assert that tuple counter equals bundle size */
					if (tuple_counter != bundle) {
						System.err.println("error: invalid bundle size");
						System.exit(1);
					}
					/* Compress the data */
					compressed = SmartGridUtils.compress(data.array());
					compressedBytes += compressed.length;
					ByteBuffer buffer = ByteBuffer.wrap(compressed);
					bundles.add(buffer);
					/* Reset state */
					data.clear();
					tuple_counter = 0L;
				}
			}
			
			d.close();
			dt = (double ) (System.currentTimeMillis() - start) / 1000.;
			/* Statistics */
			rate =  (double) (lines) / dt;
			MBps = ((double) bytes / _1MB) / dt;
			
			System.out.println(String.format("[DBG] %12d lines read", lines));
			System.out.println(String.format("[DBG] %12d bytes read", bytes));
			System.out.println(String.format("[DBG] %12d tuples", totalTuples));
			System.out.println();
			System.out.println(String.format("[DBG] %12d compressed bytes", compressedBytes));
			System.out.println(String.format("[DBG] %12d bundles", bundles.size()));
            System.out.println();
			System.out.println(String.format("[DBG] %10.1f seconds", (double) dt));
			System.out.println(String.format("[DBG] %10.1f tuples/s", rate));
			System.out.println(String.format("[DBG] %10.1f MB/s", MBps));
			System.out.println();
			
			/* Writing compressed data to file */
			System.out.println("[DBG] writing compressed data...");
			
			File datafile = new File (String.format("compressed-%d.dat", bundle));
			FileOutputStream f_ = new FileOutputStream (datafile);
			BufferedOutputStream output_ = new BufferedOutputStream(f_);
			long written = 0L;
			long offsets = 0L;
			
			for (ByteBuffer buffer: bundles) {
				int length = buffer.array().length;
				ByteBuffer L = ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN);
				L.putInt(length);
				output_.write(L.array());
				output_.write(buffer.array());
				output_.flush();
				written += length;
				offsets += L.array().length;
			}
			output_.close();
			System.out.println(String.format("[DBG] %12d compressed bytes written (%d)", 
				written, (written + offsets)));
			
			System.out.println("Bye.");
			
		} catch (Exception e) {
			System.err.println(String.format("error: %s", e.getMessage()));
			e.printStackTrace();
			System.exit(1);
		}
	}
}
