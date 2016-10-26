#!/bin/bash

cores=$1;
shift;

reps=$1;
shift;

source scripts/lock_exec;
source scripts/config;

result_type=$1;

if [ "$result_type" = "max" ];
then
    run_script="./scripts/run_rep_max.sh $reps";
    echo "# Result from $reps repetitions: max";
    shift;

elif [ "$result_type" = "min" ];
then
    run_script="./scripts/run_rep_min.sh $reps";
    echo "# Result from $reps repetitions: min";
    shift;
elif [ "$result_type" = "median" ];
then
    run_script="./scripts/run_rep_med.sh $reps";
    echo "# Result from $reps repetitions: median";
    shift;
else
    run_script="./scripts/run_rep_max.sh $reps";
    echo "# Result from $reps repetitions: max (default). Available: min, max, median";
fi;

prog1="$1";
shift;
prog2="$1";
shift;
prog3="$1";
shift;
prog4="$1";
shift;
prog5="$1";
shift;
prog6="$1";
shift;
prog7="$1";
shift;
prog8="$1";
shift;
prog9="$1";
shift;
params="$@";
if true
then
prog=$prog1;
echo "$prog1"
echo "#cores  throughput  %linear scalability"
printf "%-8d" 1;
thr1a=$($run_script ./$prog $params -n1);
printf "%-12f" $thr1a;
printf "%-8.2f" 100.00;
printf "%-12d" 1;
printf "\n"
for c in $cores
do
    if [ $c -eq 1 ]
    then
    continue;
    fi;

    printf "%-8d" $c;

    prog=$prog1;
    thr1=$thr1a;

    thr=$($run_script ./$prog $params -n$c);
    printf "%-12f" $thr;
    scl=$(echo "$thr/$thr1" | bc -l);
    linear_p=$(echo "100*(1-(($c-$scl)/$c))" | bc -l);
    printf "%-8.2f" $linear_p;
    printf "%-12.2f" $scl;
    printf "\n"

    done;

printf "\n"

prog1=$prog2
echo "$prog1"
echo "#cores  throughput  %linear scalability"
prog=$prog1;
printf "%-8d" 1;
thr1a=$($run_script ./$prog $params -n1);
printf "%-12f" $thr1a;
printf "%-8.2f" 100.00;
printf "%-12d" 1;
printf "\n"
for c in $cores
do
    if [ $c -eq 1 ]
    then
    continue;
    fi;

    printf "%-8d" $c;

    prog=$prog1;
    thr1=$thr1a;

    thr=$($run_script ./$prog $params -n$c);
    printf "%-12f" $thr;
    scl=$(echo "$thr/$thr1" | bc -l);
    linear_p=$(echo "100*(1-(($c-$scl)/$c))" | bc -l);
    printf "%-8.2f" $linear_p;
    printf "%-12.2f" $scl;
    printf "\n"

    done;

printf "\n"

prog1=$prog3
echo "$prog1"
echo "#cores  throughput  %linear scalability"
prog=$prog1;
printf "%-8d" 1;
thr1a=$($run_script ./$prog $params -n1);
printf "%-12f" $thr1a;
printf "%-8.2f" 100.00;
printf "%-12d" 1;
printf "\n"
for c in $cores
do
    if [ $c -eq 1 ]
    then
    continue;
    fi;

    printf "%-8d" $c;

    prog=$prog1;
    thr1=$thr1a;

    thr=$($run_script ./$prog $params -n$c);
    printf "%-12f" $thr;
    scl=$(echo "$thr/$thr1" | bc -l);
    linear_p=$(echo "100*(1-(($c-$scl)/$c))" | bc -l);
    printf "%-8.2f" $linear_p;
    printf "%-12.2f" $scl;
    printf "\n"

    done;

printf "\n"

prog1=$prog4
echo "$prog1"
echo "#cores  throughput  %linear scalability"
prog=$prog1;
printf "%-8d" 1;
thr1a=$($run_script ./$prog $params -n1);
printf "%-12f" $thr1a;
printf "%-8.2f" 100.00;
printf "%-12d" 1;
printf "\n"
for c in $cores
do
    if [ $c -eq 1 ]
    then
    continue;
    fi;

    printf "%-8d" $c;

    prog=$prog1;
    thr1=$thr1a;

    thr=$($run_script ./$prog $params -n$c);
    printf "%-12f" $thr;
    scl=$(echo "$thr/$thr1" | bc -l);
    linear_p=$(echo "100*(1-(($c-$scl)/$c))" | bc -l);
    printf "%-8.2f" $linear_p;
    printf "%-12.2f" $scl;
    printf "\n"

    done;

printf "\n"

prog1=$prog5
echo "$prog1"
echo "#cores  throughput  %linear scalability"
prog=$prog1;
printf "%-8d" 1;
thr1a=$($run_script ./$prog $params -n1);
printf "%-12f" $thr1a;
printf "%-8.2f" 100.00;
printf "%-12d" 1;
printf "\n"
for c in $cores
do
    if [ $c -eq 1 ]
    then
    continue;
    fi;

    printf "%-8d" $c;

    prog=$prog1;
    thr1=$thr1a;

    thr=$($run_script ./$prog $params -n$c);
    printf "%-12f" $thr;
    scl=$(echo "$thr/$thr1" | bc -l);
    linear_p=$(echo "100*(1-(($c-$scl)/$c))" | bc -l);
    printf "%-8.2f" $linear_p;
    printf "%-12.2f" $scl;
    printf "\n"

    done;

printf "\n"

prog1=$prog6
echo "$prog1"
echo "#cores  throughput  %linear scalability"
prog=$prog1;
printf "%-8d" 1;
thr1a=$($run_script ./$prog $params -n1);
printf "%-12f" $thr1a;
printf "%-8.2f" 100.00;
printf "%-12d" 1;
printf "\n"
for c in $cores
do
    if [ $c -eq 1 ]
    then
    continue;
    fi;

    printf "%-8d" $c;

    prog=$prog1;
    thr1=$thr1a;

    thr=$($run_script ./$prog $params -n$c);
    printf "%-12f" $thr;
    scl=$(echo "$thr/$thr1" | bc -l);
    linear_p=$(echo "100*(1-(($c-$scl)/$c))" | bc -l);
    printf "%-8.2f" $linear_p;
    printf "%-12.2f" $scl;
    printf "\n"

    done;

    printf "\n"

prog1=$prog7
echo "$prog1"
echo "#cores  throughput  %linear scalability"
prog=$prog1;
printf "%-8d" 1;
thr1a=$($run_script ./$prog $params -n1);
printf "%-12f" $thr1a;
printf "%-8.2f" 100.00;
printf "%-12d" 1;
printf "\n"
for c in $cores
do
    if [ $c -eq 1 ]
    then
    continue;
    fi;

    printf "%-8d" $c;

    prog=$prog1;
    thr1=$thr1a;

    thr=$($run_script ./$prog $params -n$c);
    printf "%-12f" $thr;
    scl=$(echo "$thr/$thr1" | bc -l);
    linear_p=$(echo "100*(1-(($c-$scl)/$c))" | bc -l);
    printf "%-8.2f" $linear_p;
    printf "%-12.2f" $scl;
    printf "\n"

    done;

    printf "\n"

prog1=$prog8
echo "$prog1"
echo "#cores  throughput  %linear scalability"
prog=$prog1;
printf "%-8d" 1;
thr1a=$($run_script ./$prog $params -n1);
printf "%-12f" $thr1a;
printf "%-8.2f" 100.00;
printf "%-12d" 1;
printf "\n"
for c in $cores
do
    if [ $c -eq 1 ]
    then
    continue;
    fi;

    printf "%-8d" $c;

    prog=$prog1;
    thr1=$thr1a;

    thr=$($run_script ./$prog $params -n$c);
    printf "%-12f" $thr;
    scl=$(echo "$thr/$thr1" | bc -l);
    linear_p=$(echo "100*(1-(($c-$scl)/$c))" | bc -l);
    printf "%-8.2f" $linear_p;
    printf "%-12.2f" $scl;
    printf "\n"

    done;

    printf "\n"
fi
prog1=$prog9
echo "$prog1"
echo "#cores  throughput  %linear scalability"
prog=$prog1;
printf "%-8d" 1;
thr1a=$($run_script ./$prog $params -n1);
printf "%-12f" $thr1a;
printf "%-8.2f" 100.00;
printf "%-12d" 1;
printf "\n"
for c in $cores
do
    if [ $c -eq 1 ]
    then
    continue;
    fi;

    printf "%-8d" $c;

    prog=$prog1;
    thr1=$thr1a;

    thr=$($run_script ./$prog $params -n$c);
    printf "%-12f" $thr;
    scl=$(echo "$thr/$thr1" | bc -l);
    linear_p=$(echo "100*(1-(($c-$scl)/$c))" | bc -l);
    printf "%-8.2f" $linear_p;
    printf "%-12.2f" $scl;
    printf "\n"

    done;

source scripts/unlock_exec;
