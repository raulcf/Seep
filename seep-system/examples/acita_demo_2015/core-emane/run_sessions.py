#!/usr/bin/python

import sys,os,time,re,argparse,math,shutil
from core import pycore
from core.constants import *
from core.mobility import BasicRangeModel
from core.mobility import Ns2ScriptedMobility 
from core.emane.ieee80211abg import EmaneIeee80211abgModel
from core.misc.xmlutils import savesessionxml
from util import chmod_dir

script_dir = os.path.dirname(os.path.realpath(__file__))
#script_dir = '/home/dan/dev/seep-ita/seep-system/examples/acita_demo_2015/core-emane'

print 'Appending script_dir to path'
#sys.path.append(script_dir)
from gen_mobility_trace import gen_trace

#repo_dir = '%s/../../../..'
#svc_dir='/data/dev/seep-github/seep-system/examples/acita_demo_2015/core-emane/vldb/myservices'
svc_dir='%s/vldb/myservices'%script_dir
#conf_dir='/data/dev/seep-github/seep-system/examples/acita_demo_2015/core-emane/vldb/config'
conf_dir='%s/vldb/config'%script_dir
seep_jar = "seep-system-0.0.1-SNAPSHOT.jar"
mobility_params = [('file','%s/rwpt.ns_movements'%conf_dir),('refresh_ms',500),
        ('loop',1),('autostart',1.0),('map',''),('script_start',''),('script_pause',''),('script_stop','')]

datacollect_template = '''#!/bin/bash
# session hook script; write commands here to execute on the host at the
# specified state


#echo "`hostname`:`pwd`" > /tmp/datacollect.log
#if [ -z "$SEEP_GITHUB_DIR" ]; then
#	echo "SEEP_GITHUB_DIR not set." >> /tmp/datacollect.log
#	SEEP_GITHUB_DIR=/data/dev/seep-github
#fi

#scriptDir=$SEEP_GITHUB_DIR/seep-system/examples/acita_demo_2015/core-emane
scriptDir=%s
timeStr=%s
k=%dk
mob=%.2fm
session=%ds
#resultsDir=$scriptDir/log/$timeStr
resultsDir=$scriptDir/log/$timeStr/$k/$mob/$session

expDir=$(pwd)

echo $expDir >> /tmp/datacollect.log
echo $scriptDir >> /tmp/datacollect.log
echo $timeStr >> /tmp/datacollect.log
echo $resultsDir >> /tmp/datacollect.log

mkdir -p $resultsDir

# Copy all log files to results dir
for d in n*.conf 
do
	cp $d/log1/*.log $resultsDir	
    if [ -e "$d/log2" ]
    then
        cp $d/log2/*.log $resultsDir	
    fi
	cp $d/mappingRecordOut.txt $resultsDir	
	cp $d/mappingRecordOut.txt $scriptDir/log/$timeStr/session${session}MappingRecord.txt
done

#Copy mobility params if they exist
cp r_waypoints.params $resultsDir

cd $scriptDir
#./gen_core_results.py --expDir log/$timeStr 
./gen_core_results.py --expDir $resultsDir
chmod -R go+rw $resultsDir
cd $expDir
'''
def run_sessions(time_str, k, mob, sessions, params):
    for session in sessions:
        print '*** Running session %d ***'%session
        run_session(time_str, k, mob, session, params)

def run_session(time_str, k, mob, exp_session, params):
    try:

        session_cfg = {'custom_services_dir':svc_dir, 'emane_log_level':'1', 'verbose':params.get('verbose', "False")} 
        if params['preserve']: session_cfg['preservedir'] = '1' 
        if params.get('controlnet'): session_cfg['controlnet'] = params['controlnet'] 
        print 'params=',params
        session = pycore.Session(cfg=session_cfg, persistent=True)
        #session = pycore.Session(cfg={'custom_services_dir':svc_dir}, persistent=True)

        if not add_to_server(session): 
            print 'Could not add to server'

        #This is so broken, should find a better way...
        write_replication_factor(k, session.sessiondir)
        write_chain_length(params['h'], session.sessiondir)
        write_query_type(params['query'], session.sessiondir)
        write_extra_params(params, session.sessiondir)
        write_session_params(params, session.sessiondir)

        copy_seep_jar(session.sessiondir)
        trace_file = None
        if mob > 0.0:
            trace_params = dict(params)
            trace_params['h'] = mob + 1.0
            trace_params['l'] = mob - 1.0
            trace_file = gen_trace(session.sessiondir, exp_session, trace_params)
            print 'Trace file=',trace_file


        model = params.get('model', None)
        print 'Model=', model
        if not model:
            # Gives ping range of ~915m with 1:1 pixels to m and default 802.11
            # settings (2ray).
            session.master = True
            session.location.setrefgeo(47.5791667,-122.132322,2.00000)
            session.location.refscale = 100.0
            session.cfg['emane_models'] = "RfPipe, Ieee80211abg, Bypass"
            session.emane.loadmodels()

            #prefix = ipaddr.IPv4Prefix("10.0.0.0/32")
            #tmp.newnetif(net, ["%s/%s" % (prefix.addr(i), prefix.prefixlen)])
            # set increasing Z coordinates
            wlan1 = session.addobj(cls = pycore.nodes.EmaneNode, name = "wlan1", objid=1, verbose=False)
            wlan1.setposition(x=80,y=50)
            names = EmaneIeee80211abgModel.getnames()
            values = list(EmaneIeee80211abgModel.getdefaultvalues())
            print 'Emane Model default names: %s'%(str(names))
            print 'Emane Model default values: %s'%(str(values))
            # TODO: change any of the EMANE 802.11 parameter values here
            values[ names.index('mode') ] = '3'
            values[ names.index('propagationmodel') ] = '2ray'
            #values[ names.index('pathlossmode') ] = '2ray'
            #values[ names.index('multicastrate') ] = '12'
            values[ names.index('multicastrate') ] = '4'
            #values[ names.index('unicastrate') ] = '12'
            #values[ names.index('distance') ] = '500'
            values[ names.index('unicastrate') ] = '4'
            values[ names.index('txpower') ] = '-10.0'
            values[ names.index('flowcontrolenable') ] = 'on'
            #values[ names.index('flowcontroltokens') ] = '1'
            print 'Emane Model overridden values: %s'%(str(list(values)))
            session.emane.setconfig(wlan1.objid, EmaneIeee80211abgModel._name, values)
        else:
            wlan1 = session.addobj(cls = pycore.nodes.WlanNode, name="wlan1",objid=1, verbose=False)
            wlan1.setposition(x=80,y=50)
            print 'Basic Range Model default values: %s'%(str(BasicRangeModel.getdefaultvalues()))
            model_cfg = list(BasicRangeModel.getdefaultvalues())
            model_cfg[0] = '500' #Similar to default effective emane range.
            model_cfg[1] = '11000' #Similar to default emane bandwidth.
            print 'Basic Range configured values: %s'%(str(model_cfg))
            wlan1.setmodel(BasicRangeModel, tuple(model_cfg))

        #Copy appropriate mapping constraints.
        exp_results_dir = '%s/log/%s'%(script_dir, time_str)
        session_constraints = '%s/session%dsMappingRecord.txt'%(exp_results_dir, exp_session)
        if os.path.exists(session_constraints):
            shutil.copy(session_constraints, '%s/mappingRecordIn.txt'%session.sessiondir)
        elif params['constraints']:
            session_constraints = '%s/static/%s'%(script_dir, params['constraints'])
            if not os.path.exists(session_constraints):
                raise Exception("Could not find sessions constraints: %s"%session_constraints)
            shutil.copy(session_constraints, '%s/mappingRecordIn.txt'%session.sessiondir)

        #services_str = "IPForward|SSH|%s"%params['net-routing']
        services_str = "IPForward|SSH"


        master = create_node(2, session, "%s|MeanderMaster"%services_str, wlan1,
                gen_grid_position(2+params['nodes'], params['nodes'] - 1), addinf=False)

        services_str += "|%s"%params['net-routing']
        workers = []
        num_workers = get_num_workers(k, params)
        print 'num_workers=', num_workers
        """
        Num_workers = 2 + (k * params['h'])
        if params['query'] == 'join':
            if params['h'] != 1: raise Exception('Only support query of height 1 for join')
            num_workers += 1
        else: raise Exception("Temp.")
        """

        placements = get_initial_placements(params['placement'], mob)
        print 'Creating workers.'
        for i in range(3,3+len(num_workers)):
            if placements:
                pos = placements[i]
            else:
                pos = gen_grid_position(i, params['nodes']-1)
            worker_services = "|".join(["MeanderWorker%d"%lwid for lwid in range(1, num_workers[i-3]+1)])
            workers.append(create_node(i, session, "%s|%s"%(services_str, worker_services), wlan1, pos)) 
       
        routers = []
        print 'Creating routers.'
        # Create auxiliary 'router' nodes if any left
        for i in range(3+len(num_workers), 2+params['nodes']):
            if placements:
                pos = placements[i]
            else:
                pos = gen_grid_position(i, params['nodes']-1)
            routers.append(create_node(i, session, "%s"%services_str, wlan1, pos))

        if trace_file:
            #node_map = create_node_map(range(0,6), workers)
            node_map = create_node_map(range(0,params['nodes']-1), workers+routers)
            print 'Node map=%s'%node_map
            mobility_params[4] = ('map', node_map)
            mobility_params[0] = ('file','%s/%s'%(session.sessiondir, trace_file))
            refresh_ms = int(params.get('refresh_ms', 500))
            mobility_params[1] = ('refresh_ms', refresh_ms)
            session.mobility.setconfig_keyvalues(wlan1.objid, 'ns2script', mobility_params)

        datacollect_hook = create_datacollect_hook(time_str, k, mob, exp_session) 
        session.sethook("hook:5","datacollect.sh",None,datacollect_hook)
        session.node_count="%d"%(1+params['nodes'])
        if params['saveconfig']:
            print 'Saving session config.'
            savesessionxml(session, '%s/session.xml'%session.sessiondir)
            #savesessionxml(session, '/tmp/session.xml')

        print 'Instantiating session:',exp_session
        session.instantiate()

        chmod_dir(session.sessiondir)
        for n in range(2,3+len(num_workers)):
            node_dir = '%s/n%d.conf'%(session.sessiondir,n)
            chmod_dir('%s/var.run/sshd'%node_dir, 0655)
            chmod_dir('%s/var.run.sshd'%node_dir, 0655)
            while not os.path.exists('%s/etc.ssh/ssh_host_rsa_key'%node_dir):
                time.sleep(1)
            os.chmod('%s/etc.ssh/ssh_host_rsa_key'%node_dir, 0700)

        print 'Waiting for a meander worker/master to terminate'
        watch_meander_services(session.sessiondir, map(lambda n: "n%d"%n,
            range(2,3 + sum(num_workers))))
        #time.sleep(30)
        print 'Collecting data'
        session.datacollect()
        time.sleep(5)
        print 'Shutting down'

    finally:
        print 'Shutting down session.'
        if session:
            session.shutdown()

def get_num_workers(k, params):
    q = params['query']
    if q == 'chain' or q == 'fr' or q == 'join': 
        num_workers = [1] * (2 + (k * params['h']))
        if params['query'] == 'join':
            if params['h'] != 1: raise Exception('Only support query of height 1 for join')
            num_workers.append(1)
    elif q == 'debsGC13':
        if k > 2 or h > 1: raise Exception('Only support replication factors <= 2 for debs_gc_13') 
        num_workers = [1] * 23 
        if h > 0:
            num_workers[1] += 1 #A RB
            num_workers[5] += 1 #A RM
            num_workers[9] += 1 #A RF
            num_workers[12] += 1 #B RB
            num_workers[16] += 1 #B RM
            num_workers[20] += 1 #B RF
            if k > 1:
                num_workers[2] += 1 #A RCB
                num_workers[6] += 1 #A RCM
                num_workers[10] += 1 #A LF
                num_workers[13] += 1 #B RCB
                num_workers[17] += 1 #B RCM
                num_workers[21] += 1 #B LF

    elif q == 'nameAssist':
        num_workers = [2]+([1]*(1+ (k*3))) 
    elif q == 'heatMap':
        sources =  int(params['sources'])
        sinks = int(params['sinks'])
        fan_in = int(params['fanin'])
        height = int(math.ceil(math.log(sources, fan_in)))
        children = sources
        join_ops = 0 
        for i in range(0, height):
            parents = children / fan_in
            if children % fan_in > 0: parents += 1
            join_ops += parents
            children = parents
        print 'height=%d, join_ops=%d'%(height, join_ops)
        worker_nodes = params['nodes'] - 2
        if worker_nodes >= sources + k*(join_ops + sinks):
            num_workers = [1] * (sources + k*(join_ops + sinks))
        else:
            #Need to have multiple workers on some nodes.
            #N.B. Don't want to colocate replicas of the
            #same operator or the source.
            raise Exception("TODO")
    else: 
        raise Exception('Unknown query type: %s'%q)

    return num_workers

def create_node(i, session, services_str, wlan, pos, ip_offset=-1, addinf=True):
#def create_node(i, session, services_str, wlan, pos, ip_offset=8):
    tstart = time.time() 
    n = session.addobj(cls = pycore.nodes.CoreNode, name="n%d"%i, objid=i)
    taddobj = time.time() - tstart
    n.setposition(x=pos[0], y=pos[1])
    session.services.addservicestonode(n, "", services_str, verbose=False)
    taddservices = time.time() - tstart
    if addinf:
        ip = i + ip_offset 
        n.newnetif(net=wlan, addrlist=["10.0.0.%d/32"%(ip)], ifindex=0)
        taddnetif = time.time() - tstart
        n.cmd([SYSCTL_BIN, "net.ipv4.icmp_echo_ignore_broadcasts=0"])
        tcmd = time.time() - tstart
        print 'taddobj=%.3f,taddserv=%.3f,taddnet=%.3f,tcmd=%.3f'%(taddobj,taddservices,taddnetif,tcmd)
        print 'Created node n%d (10.0.0.%d) with initial pos=(%.1f,%.1f)'%(i,ip,pos[0], pos[1])
    else:
        print 'Created node n%d (no inf) with initial pos=(%.1f,%.1f)'%(i,pos[0], pos[1])
    #n.cmd([SYSCTL_BIN, "net.ipv4.ip_forward=1"])
    #n.cmd([SYSCTL_BIN, "net.ipv4.conf.all.forwarding=1"])
    #n.cmd([SYSCTL_BIN, "net.ipv6.conf.all.forwarding=1"])
    #n.cmd([SYSCTL_BIN, "net.ipv4.conf.all.rp_filter=0"])
    #n.cmd([SYSCTL_BIN, "net.ipv4.conf.default.rp_filter=0"])

    return n

def create_node_map(ns_nums, nodes):
    if len(ns_nums) != len(nodes): 
        raise Exception("Invalid node mapping.")
    print 'ns_nums=%s'%str(ns_nums)
    print 'nodes=%s'%str(nodes)
    return ",".join(map(lambda (ns_num, node) : "%d:%d"%(ns_num,node.objid), zip(ns_nums, nodes)))

def get_initial_placements(placements, mobility):
    if not placements or mobility > 0.0:
        return None
    else:
        result = {}
        placements_path = '%s/static/%s'%(script_dir, placements)
        with open(placements_path, 'r') as pf:
            for line in pf:
                els = map(int, line.split(','))
                result[els[0]] = (els[1], els[2])

        return result

def gen_linear_position(i):
    return (50 * i, 100)

def gen_grid_position(i, nodes, offset=3, spacing=600):
    if i < offset: raise Exception("Invalid offset for %d: %d"%(i,offset))
    dim = math.ceil(math.sqrt(nodes))
    num_x = (i-offset) % dim 
    num_y = math.floor((i-offset) / dim)
    return (int(spacing * num_x), int(spacing * num_y)) 

def add_to_server(session):
    global server
    try:
        server.addsession(session)
        return True
    except NameError:
        print 'Name error'
        return False

def create_datacollect_hook(time_str, k, mob, exp_session):
    print 'Script dir = %s'%script_dir
    return datacollect_template%(script_dir, time_str, k, mob, exp_session)

def watch_meander_services(sessiondir, node_names):
    while True:
        for name in node_names:
            for process in ['worker1', 'worker2', 'master']:
                if os.path.exists("%s/%s.conf/%s.shutdown"%(sessiondir, name, process)):
		    print 'Shutdown file exists for node %s (%s) - exiting'%(name, process)
		    return

        time.sleep(0.5)

def write_replication_factor(k, session_dir):
    with open('%s/k.txt'%session_dir, 'w') as f:
        f.write(str(k))

def write_chain_length(h, session_dir):
    with open('%s/h.txt'%session_dir, 'w') as f:
        f.write(str(h))

def write_query_type(query, session_dir):
    with open('%s/query.txt'%session_dir, 'w') as f:
        f.write(str(query))

def write_extra_params(params, session_dir):
    with open('%s/extra_params.txt'%session_dir, 'w') as f:
        f.write('sources=%s\n'%str(params['sources']))
        f.write('sinks=%s\n'%str(params['sinks']))
        f.write('fanin=%s\n'%str(params['fanin']))

def write_session_params(params, session_dir):
    with open('%s/session_params.txt'%session_dir, 'w') as f:
        for k in params:
            f.write('%s=%s\n'%(k,str(params[k])))

def copy_seep_jar(session_dir):
    dest = '%s/lib'%session_dir
    os.mkdir(dest)
    shutil.copy('%s/../lib/%s'%(script_dir,seep_jar), dest)

#def exists_mobility_trace(time_str, session):
#    return os.path.isfile(

def regen_sessions(time_str):
    raise Exception("TODO")

if __name__ == "__main__" or __name__ == "__builtin__":
    parser = argparse.ArgumentParser(description='Run several meander experiments on CORE')
    parser.add_argument('--k', dest='k', default='2', help='replication factors (2)')
    parser.add_argument('--h', dest='h', default='2', help='chain length (2)')
    parser.add_argument('--query', dest='query', default='chain', help='query type: (chain), join')
    parser.add_argument('--pausetime', dest='pt', default='2.0', help='pause time (2.0)')
    parser.add_argument('--sessions', dest='sessions', default='1', help='number of sessions to run')
    parser.add_argument('--specific', dest='specific', default=False, action='store_true', help='Run a specific session')
    parser.add_argument('--plotOnly', dest='plot_time_str', default=None, help='time_str of run to plot (hh-mm-DDDddmmyy)[None]')
    parser.add_argument('--nodes', dest='nodes', default='10', help='Total number of core nodes in network')
    parser.add_argument('--disableCtrlNet', dest='disable_ctrl_net', action='store_true', help='Disable ctrl network')
    parser.add_argument('--model', dest='model', default=None, help='Wireless model (Basic, Emane)')
    parser.add_argument('--routing', dest='routing', default='OLSRETX',
            help='Net layer routing alg (OLSR, OLSRETX)')
    parser.add_argument('--preserve', dest='preserve', default=False, action='store_true', help='Preserve session directories')
    parser.add_argument('--saveconfig', dest='saveconfig', default=False, action='store_true', help='Export the session configuration to an XML file')
    parser.add_argument('--constraints', dest='constraints', default='', help='Export the session configuration to an XML file')
    parser.add_argument('--placement', dest='placement', default='', help='Explicit static topology to use for all sessions')
    args=parser.parse_args()

    k=int(args.k)
    pt=float(args.pt)
    params = {'nodes':int(args.nodes)}
    if not args.disable_ctrl_net: params['controlnet']='172.16.0.0/24'
    # placements=map(lambda x: str(int(x)), [] if not args.placements else args.placements.split(','))
    if args.model: params['model']=args.model
    params['net-routing']=args.routing
    params['specific']=args.specific
    params['preserve']=args.preserve
    params['h']=int(args.h)
    params['query']=args.query
    params['saveconfig']=args.saveconfig
    params['constraints']=args.constraints
    params['placement']=args.placement

    sessions = int(args.sessions)
    session_ids = [sessions] if args.specific else range(0,sessions)
    if args.plot_time_str:
        time_str = args.plot_time_str
        regen_sessions(time_str)
    else:
        time_str = time.strftime('%H-%M-%S-%a%d%m%y')
        run_sessions(time_str, k, pt, session_ids,params)
