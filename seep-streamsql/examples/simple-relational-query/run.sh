#!/bin/bash

USAGE="usage: ./run.sh [class name]"

# Classpath
JCP="."
JCP=${JCP}:src
JCP=${JCP}:lib/seep-streamsql-0.0.1-SNAPSHOT.jar
JCP=${JCP}:lib/seep-system-0.0.1-SNAPSHOT.jar

# OPTS="-Xloggc:test-gc.out"
OPTS="-server -XX:+UseConcMarkSweepGC -XX:NewRatio=2 -XX:SurvivorRatio=16 -Xms48g -Xmx48g"

# if [ $# -gt 1 ]; then
#    echo $USAGE
#    exit 1
# fi

CLASS=$1

java $OPTS -cp $JCP $CLASS 32 row 1024 1024 6 # 2>&1 >dummy.out
# $OPTS -cp $JCP $CLASS $2 # 2>&1 >test.out &

# seep_pid=$!
# top -b -n120 -d 1 | grep "Cpu" >> test-cpu.out
# kill -9 $seep_pid

echo "Done."
sleep 1
echo "Bye."

exit 0
