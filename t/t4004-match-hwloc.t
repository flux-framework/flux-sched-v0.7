#!/bin/sh
#set -x

test_description='Test resource-match using hwloc resource information

Ensure that the match (allocate) handler within the resource module works
'

ORIG_HOME=${HOME}

. `dirname $0`/sharness.sh

#
# sharness modifies $HOME environment variable, but this interferes
# with python's package search path, in particular its user site package.
#
HOME=${ORIG_HOME}

jobspec_basepath=`readlink -e ${SHARNESS_TEST_SRCDIR}/data/resource/jobspecs/`
# slot[1]->core[1]
jobspec_1core="${jobspec_basepath}/basics/test008.yaml"
# node[1]->slot[2]->numanode[1]->socket[1]
jobspec_2socket="${jobspec_basepath}/basics/test009.yaml"

hwloc_basepath=`readlink -e ${SHARNESS_TEST_SRCDIR}/data/hwloc-data`
# 1 broker: 1 node, 1 socket, 4 cores
hwloc_4core="${hwloc_basepath}/001N/exclusive/04-brokers/0.xml"
# 4 brokers, each (exclusively) have: 1 node, 2 sockets, 16 cores (8 per socket)
excl_4N4B="${hwloc_basepath}/004N/exclusive/04-brokers"
# 1 broker: 1 node, 2 numanodes, 4 gpus, 44 cores, 176 PUs
hwloc_4gpu="${hwloc_basepath}/004N/exclusive/04-brokers-sierra2/0.xml"

query="../../resource/utilities/resource-query"
cmd_dir="${SHARNESS_TEST_SRCDIR}/data/resource/commands/basics"
exp_dir="${SHARNESS_TEST_SRCDIR}/data/resource/expected/basics"

#
# test_under_flux is under sharness.d/
#
test_under_flux 4

#
# print only with --debug
#
test_debug '
    echo ${jobspec_1core} &&
    echo ${jobspec_2socket} &&
    echo ${hwloc_4core} &&
    echo ${excl_4N4B}
'

# Test using the resource query command
test_expect_success 'resource-query works on simple query using xml file' '
    sed "s~@TEST_SRCDIR@~${SHARNESS_TEST_SRCDIR}~g" ${cmd_dir}/cmds09.in > cmds09 &&
    ${query} -X ${hwloc_4core} -S CA -P high -t 017.R.out < cmds09 &&
    test_cmp 017.R.out ${exp_dir}/017.R.out
'

test_expect_success 'resource-query works on gpu query using xml file' '
    sed "s~@TEST_SRCDIR@~${SHARNESS_TEST_SRCDIR}~g" ${cmd_dir}/cmds10.in > cmds10 &&
    ${query} -X ${hwloc_4gpu} -S CA -P high -t 018.R.out < cmds10 &&
    test_cmp 018.R.out ${exp_dir}/018.R.out
'

# Test using the full resource matching service
test_expect_success 'loading resource module with a tiny hwloc xml file works' '
    flux module load -r 0 resource hwloc-xml=${hwloc_4core}
'

test_expect_success 'match-allocate works with four one-core jobspecs' '
    flux resource match allocate ${jobspec_1core} &&
    flux resource match allocate ${jobspec_1core} &&
    flux resource match allocate ${jobspec_1core} &&
    flux resource match allocate ${jobspec_1core}
'

test_expect_success 'match-allocate fails when all resources are allocated' '
    test_expect_code 16 flux resource match allocate ${jobspec_1core} &&
    test_expect_code 16 flux resource match allocate ${jobspec_1core} &&
    test_expect_code 16 flux resource match allocate ${jobspec_1core} &&
    test_expect_code 16 flux resource match allocate ${jobspec_1core}
'

test_expect_success 'removing resource works' '
    flux module remove resource
'

test_expect_success 'reloading session/hwloc information with test data' '
    flux hwloc reload ${excl_4N4B}
'

test_expect_success 'loading resource module with default resource info source' '
    flux module load -r 0 resource subsystems=containment policy=high
'

test_expect_success 'match-allocate works with four two-socket jobspecs' '
    flux resource match allocate ${jobspec_2socket} &&
    flux resource match allocate ${jobspec_2socket} &&
    flux resource match allocate ${jobspec_2socket} &&
    flux resource match allocate ${jobspec_2socket}
'

test_expect_success 'match-allocate fails when all resources are allocated' '
    test_expect_code 16 flux resource match allocate ${jobspec_2socket} &&
    test_expect_code 16 flux resource match allocate ${jobspec_2socket} &&
    test_expect_code 16 flux resource match allocate ${jobspec_2socket} &&
    test_expect_code 16 flux resource match allocate ${jobspec_2socket}
'

test_expect_success 'removing resource works' '
    flux module remove resource
'

test_done
