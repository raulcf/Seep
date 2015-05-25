#!/usr/bin/python

import subprocess,os,time,re,argparse

from compute_stats import compute_stats,median,compute_relative_raw_vals
from run_sessions import run_sessions

ticksPerSecond = 1000.0 * 1000.0 * 1000.0
maxWaitSeconds = 1000000000
latency_percentile = '95'

def main(ks,mobilities,sessions,params,plot_time_str=None):

    script_dir = os.path.dirname(os.path.realpath(__file__))
    data_dir = '%s/log'%script_dir

    session_ids = [sessions] if params['specific'] else range(0,sessions)
    if plot_time_str:
        time_str = plot_time_str
    else:
        time_str = time.strftime('%H-%M-%S-%a%d%m%y')
        run_experiment(ks, mobilities, session_ids, params, time_str, data_dir )

    record_statistics(ks, mobilities, session_ids, time_str, data_dir, 'tput', get_tput)
    record_statistics(ks, mobilities, session_ids, time_str, data_dir, 'lat', get_latency)

    for p in ['tput_vs_mobility', 'median_tput_vs_mobility', 
		'latency_vs_mobility', 'tput_vs_mobility_stddev', 
		'latency_vs_mobility_stddev', 'rel_tput_vs_mobility_stddev',
		'rel_latency_vs_mobility_stddev', 'tput_vs_netsize_stddev']:
        plot(p, time_str, script_dir, data_dir)

	
    os.chmod('%s/%s'%(data_dir, time_str), 0777)
    for root,dirs,files in os.walk('%s/%s'%(data_dir, time_str)):
        for d in dirs:
            os.chmod(os.path.join(root,d), 0777)
        for f in files:
            os.chmod(os.path.join(root,f), 0777)

def get_session_dir(k, mob, session, time_str, data_dir):
    return '%s/%s/%dk/%.2fm/%ds'%(data_dir, time_str, k, mob, session)

def create_exp_dirs(ks, mobilities, sessions, time_str, data_dir):
    root_exp_dir = '%s/%s'%(data_dir,time_str)
    os.mkdir(root_exp_dir)
    for k in ks:
        k_dir = '%s/%dk'%(root_exp_dir, k)
        os.mkdir(k_dir)
        for mob in mobilities:
            mob_dir = '%s/%.2fm'%(k_dir, mob)
            for session in range(0, sessions):
                os.mkdir('%s/%ds'%(mob_dir, session))

def run_experiment(ks, mobilities, sessions, params, time_str, data_dir):
    #TODO: Not using data dir here!
    for k in ks:
        for mob in mobilities:
            run_sessions(time_str, k,mob,sessions, params)
                    

def record_statistics(ks, mobilities, sessions, time_str, data_dir, metric_suffix, get_metric_fn):
    raw_vals = {}
    for k in ks:
        raw_vals[k] = {}
        for (i_mob, mob) in enumerate(mobilities):
            writeHeader = i_mob == 0
            metrics = get_metrics(k, mob, sessions, time_str, data_dir, get_metric_fn)
            raw_vals[k][mob] = metrics 
            meanVal,stdDevVal,maxVal,minVal,medianVal,lqVal,uqVal = compute_stats(metrics.values())  

            # record stats vs mobility 
            with open('%s/%s/%dk-%s.data'%(data_dir,time_str,k, metric_suffix),'w' if writeHeader else 'a') as rx_vs_mob_plotdata:
                if writeHeader: 
                    rx_vs_mob_plotdata.write('#k=%d\n'%k)
                    rx_vs_mob_plotdata.write('#mob mean ? stdDev max min med lq uq\n')
                rx_vs_mob_plotdata.write('%.4f %.1f %d %.1f %.1f %.1f %.1f %.1f %.1f\n'%(mob,meanVal, 1, stdDevVal, maxVal, minVal, medianVal, lqVal, uqVal))
	#TODO Do relative weights with raw_vals.
    if 1 in ks:
        relative_raw_vals = compute_relative_raw_vals(raw_vals)
        for k in ks:
            for (i_mob, mob) in enumerate(mobilities):
                writeHeader = i_mob == 0
                metrics = relative_raw_vals[k][mob]
                meanVal,stdDevVal,maxVal,minVal,medianVal,lqVal,uqVal = compute_stats(metrics.values())  

                #record relative stats vs mobility
                with open('%s/%s/%dk-rel-%s.data'%(data_dir,time_str,k,metric_suffix), 'w' if writeHeader else 'a') as rel_rx_vs_mob_plotdata:	
                    if writeHeader:
                        rel_rx_vs_mob_plotdata.write('#k=%d\n'%k)
                        rel_rx_vs_mob_plotdata.write('#mob mean ? stdDev max min med lq uq\n')
                    rel_rx_vs_mob_plotdata.write('%.4f %.1f %d %.1f %.1f %.1f %.1f %.1f %.1f\n'%(mob,meanVal, 1, stdDevVal, maxVal, minVal, medianVal, lqVal, uqVal))
	

def get_metrics(k, mob, sessions, time_str, data_dir, get_metric_fn):
    metrics = {} 
    for session in sessions:
        logdir = '%s'%(get_session_dir(k,mob,session,time_str,data_dir))
        metric = get_metric_fn(logdir)
        metrics[session] = metric 

    return metrics

def get_tput(logdir):
    #regex = re.compile('src_sink_mean_tput=(\d+)')
    regex = re.compile('sink_sink_mean_tput=(\d+)')
    with open('%s/tput.txt'%logdir, 'r') as tput_log:
        for line in tput_log:
            match = re.search(regex, line)
            if match:
                return float(match.group(1))
            
    raise Exception("Could not find tput in %s"%logfilename)

def get_latency(logdir):
    #TODO: Handle float for latency in regex!
    regex = re.compile('%s%%_lat=(\d+)'%(latency_percentile))
    with open('%s/latency.txt'%logdir, 'r') as latency_log:
        for line in latency_log:
            match = re.search(regex, line)
            if match:
                return float(match.group(1))
            
    raise Exception("Could not find %s% latency in %s"%(latency_percentile, logfilename))

def plot(p, time_str, script_dir, data_dir):
    exp_dir = '%s/%s'%(data_dir,time_str)
    print exp_dir
    plot_proc = subprocess.Popen(['gnuplot', '-e',
'timestr=\'%s\';outputdir=\'%s\''%(time_str,data_dir),
script_dir+'/vldb/config/%s.plt'%p], cwd=exp_dir)
    plot_proc.wait()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Run simulations.')
    parser.add_argument('--ks', dest='ks', default='1,2,3,5', help='replication factors [1,2,3,5]')
    parser.add_argument('--h', dest='h', default='2', help='chain length (2)')
    parser.add_argument('--pausetimes', dest='pts', default='0.0,2.0,4.0,6.0,8.0', help='pause times [0.0,2.0,4.0,6.0,8.0]')
    parser.add_argument('--sessions', dest='sessions', default='2', help='number of sessions (2)')
    parser.add_argument('--specific', dest='specific', default=False, action='store_true')
    #parser.add_argument('--mobility', dest='mobility', default='static', help='mobility model: static,waypoint')
    #parser.add_argument('--query', dest='query_type', default='linear', help='query type: linear,join,mixed,parallel')
    parser.add_argument('--plotOnly', dest='plot_time_str', default=None, help='time_str of run to plot (hh-mm-DDDddmmyy)[None]')
    parser.add_argument('--nodes', dest='nodes', default='10', help='Total number of core nodes in network')
    parser.add_argument('--disableCtrlNet', dest='disable_ctrl_net', action='store_true', help='Disable ctrl network')
    parser.add_argument('--model', dest='model', default=None, help='Wireless model (Basic, Emane)')
    parser.add_argument('--routing', dest='routing', default='OLSR',
            help='Net layer routing alg (OLSR, OLSRETX)')
    parser.add_argument('--preserve', dest='preserve', default=False, action='store_true', help='Preserve session directories')
    parser.add_argument('--saveconfig', dest='saveconfig', default=False, action='store_true', help='Export the session configuration to an XML file')
    parser.add_argument('--constraints', dest='constraints', default='', help='Initial mapping constraints for each session ')
    parser.add_argument('--placement', dest='placement', default='', help='Explicit static topology to use for all sessions')

    #parser.add_argument('--placements', dest='placements', default='', help='placements 0,1,2,...')
    args=parser.parse_args()

    ks=map(lambda x: int(x), args.ks.split(','))
    pts=map(lambda x: float(x), args.pts.split(','))
    #mobs=map(lambda x: float(x), [] if args.mobility in 'static' else args.ds.split(','))
    sessions=int(args.sessions)
    params = {'nodes':int(args.nodes)}
    if not args.disable_ctrl_net: params['controlnet']='172.16.0.0/24'
    # placements=map(lambda x: str(int(x)), [] if not args.placements else args.placements.split(','))
    if args.model: params['model']=args.model
    params['net-routing']=args.routing
    params['specific']=args.specific
    params['preserve']=args.preserve
    params['h']=int(args.h)
    params['saveconfig']=args.saveconfig
    params['constraints']=args.constraints
    params['placement']=args.placement

    main(ks,pts,sessions,params,plot_time_str=args.plot_time_str)
