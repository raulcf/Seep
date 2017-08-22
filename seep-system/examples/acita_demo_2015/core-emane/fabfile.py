#!/usr/bin/env python

import time
from fabric.api import *

env.use_ssh_config = True

adhoc_hosts = [
	"191.168.181.107", # docpi2
	#"191.168.181.114", # docpiv1
	#"191.168.181.106", # docpi1
	"191.168.181.101", # mypi
	"191.168.181.108", # docpi3
	"191.168.181.110", # docpi4
]

ap_hosts = [
	#"192.168.0.107", # docpi2
	#"192.168.0.114", # docpiv1
	#"192.168.0.106", # docpi1
	#"192.168.0.101", # mypi ap
	#"192.168.0.111", # mypi wired
	#"192.168.0.108", # docpi3
	#"192.168.0.110", # docpi4
]

#env.hosts = ap_hosts
env.hosts = adhoc_hosts 

repo_root = "/home/pi/dev/seep-ita"
demo_root = repo_root + "/seep-system/examples/acita_demo_2015"

env.user = "pi"

def test():
	with cd("~"):
		run('echo "Hello World."')



def extraconf():
	with lcd(demo_root):
		with cd(demo_root):
			put('extraConfig.properties')	

@hosts('localhost')
def local_workers(numWorkers):
	with cd(demo_root + "/tmp"):
		#run('pkill "java" || /bin/true')
		run('rm -f worker*.log')
		put(demo_root + '/core-emane/vldb/config/olsrd-net-rates.sh', 'net-rates.sh')
		time.sleep(3)
		for i in range(0, int(numWorkers)):
			print "Starting worker on port 350%d"%(i+1)
			run('(nohup java -classpath "../lib/*" uk.ac.imperial.lsds.seep.Main Worker 350%d >worker350%d.log 2>&1 < /dev/null &) && /bin/true'%(i+1, i+1))
			time.sleep(2)
			

def workers(numWorkers):
	with lcd(demo_root):
		with cd(demo_root):
			put('extraConfig.properties', 'extraConfig.properties')	
			put('core-emane/vldb/config/olsrd-net-rates.sh', 'tmp/net-rates.sh')

	with cd(demo_root + "/tmp"):
		run('pkill "java" || /bin/true')
		run('rm -f worker*.log')
		time.sleep(1)
		for i in range(0, int(numWorkers)):
			print "Starting worker on port 350%d"%(i+1)
			run('(nohup java -classpath "../lib/*" uk.ac.imperial.lsds.seep.Main Worker 350%d >worker350%d.log 2>&1 < /dev/null &) && /bin/true'%(i+1, i+1))
			time.sleep(3)


def gather(expdir):
	with cd(demo_root + "/tmp"):
		with lcd(demo_root + "/core-emane/log/" + expdir):
			get('worker*.log')

@hosts('localhost')
def mkexpdir():
	with lcd(demo_root + "/core-emane/log"):
		expdir = local('date +%H-%M-%S-%a%d%m%y', capture=True)
		local('mkdir -p %s'%expdir)
		local('cp %s/*.log %s'%(demo_root+'/tmp', expdir))
		execute(gather, expdir)

def to_ap():
	sudo('cp /etc/network/interfaces.orig /etc/network/interfaces')

def to_adhoc():
	sudo('cp /etc/network/interfaces.adhoc /etc/network/interfaces')


def check_workers():
	run('ps aux | grep java')

def reboot():
	with settings(warn_only=True):
		sudo('reboot')

@hosts('localhost')
def clean():
	local('pkill "java" || /bin/true')
	execute(remote_clean)

def remote_clean():
	run('pkill "java" || /bin/true')

def ls(dir):
	run('ls %s'%dir)

def cat(file):
	run('cat %s'%file)

def git_pull():
	with cd(repo_root):
		run('git stash')
		run('git pull --rebase')
		#run('git stash pop')

@parallel
def rebuild():
	with cd(repo_root):
		run('./meander-bld.sh')

def git_status():
	with cd(repo_root):
		run('git status')

def git_diff():
	with cd(repo_root):
		run('git diff')

def port_range():
	run('cat /proc/sys/net/ipv4/ip_local_port_range')